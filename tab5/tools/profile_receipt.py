"""Verify and package one actual IDF profile; no ports, sockets or flashing.

Run after build.cmd PROFILE and run_ui_stack_guard.py for that build directory.
Receipts fail closed if descriptor, revision, GPIO object, manifests or stack
evidence disagree. A default image cannot be packaged as a controls profile.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shlex
import shutil
import struct
import subprocess
import zipfile

TAB5 = Path(__file__).resolve().parents[1]
ROOT = TAB5.parent
PROFILES = {
    'default': ('build', False, 100, 199, 'Tab5-1.8.6-P4-rev1.zip'),
    'controls': ('build-controls', True, 100, 199, 'Tab5-1.8.6-controls-P4-rev1.zip'),
    'rev3': ('build-rev3', False, 300, 399, 'Tab5-1.8.6-P4-rev3.zip'),
    'controls-rev3': ('build-controls-rev3', True, 300, 399, 'Tab5-1.8.6-controls-P4-rev3.zip'),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def image_header(data):
    assert data[0] == 0xE9, 'Not an ESP image'
    return {'chip': struct.unpack_from('<H', data, 12)[0],
            'revision_min': struct.unpack_from('<H', data, 15)[0] or data[14]*100,
            'revision_max': struct.unpack_from('<H', data, 17)[0]}


def verify_sums(folder):
    found = {}
    for line in (folder/'SHA256SUMS.txt').read_text(encoding='ascii').splitlines():
        digest, name = line.split('  ', 1)
        path = (folder/name).resolve()
        assert folder.resolve() in path.parents
        assert sha(path.read_bytes()) == digest, name
        found[name] = digest
    return found


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('profile', choices=PROFILES)
    p.add_argument('--out', type=Path, default=TAB5/'build-host/release-1.8.6')
    a = p.parse_args()
    build_name, controls, minimum, maximum, archive_name = PROFILES[a.profile]
    build = TAB5/build_name
    out = a.out.resolve()/a.profile
    out.mkdir(parents=True, exist_ok=True)
    stack_path = out/'ui-stack/result.json'
    stack = json.loads(stack_path.read_text(encoding='utf-8'))
    assert stack['passed'], 'UI stack guard failed'
    assert stack['ui_sha256'] == sha((TAB5/'main/tab5_ui.c').read_bytes())
    assert all(x['returncode'] == 0 for x in stack['compiles'])
    main_stack_compile = next(x for x in stack['compiles'] if x['unit'] == 'main.c')
    assert (build/'config').as_posix().lower() in ' '.join(main_stack_compile['command']).replace('\\', '/').lower()

    sdk = TAB5/'sdkconfig' if a.profile == 'default' else build/'sdkconfig'
    config = dict(re.findall(r'^(CONFIG_\w+)=(.*)$', sdk.read_text(encoding='utf-8'), re.M))
    assert (config.get('CONFIG_TAB5_INPUT_GPIO_ENABLE') == 'y') == controls
    assert int(config['CONFIG_ESP32P4_REV_MIN_FULL']) == minimum
    assert int(config['CONFIG_ESP32P4_REV_MAX_FULL']) == maximum
    assert (config.get('CONFIG_ESP32P4_SELECTS_REV_LESS_V3') == 'y') == (minimum < 300)
    source_package = build/'package'
    meta = json.loads((source_package/'krenomer-esp32.json').read_text(encoding='utf-8'))
    app = (build/'krenomer-esp32.bin').read_bytes()
    descriptor = {
        'magic': struct.unpack_from('<I', app, 32)[0],
        'version': app[48:80].split(b'\0')[0].decode('utf-8'),
        'project': app[80:112].split(b'\0')[0].decode('utf-8'),
        'idf_version': app[144:176].split(b'\0')[0].decode('utf-8'),
    }
    assert descriptor == {'magic': 0xABCD5432, 'version': '1.8.6-esp32',
                          'project': 'krenomer-esp32', 'idf_version': 'v5.5.2'}
    elf_sha256 = sha((build/'krenomer-esp32.elf').read_bytes())
    assert app[176:208].hex() == elf_sha256, 'Image does not belong to this production ELF'
    expected_header = {'chip': 18, 'revision_min': minimum, 'revision_max': maximum}
    assert image_header(app) == expected_header
    assert image_header((build/'bootloader/bootloader.bin').read_bytes()) == expected_header
    assert meta['version'] == descriptor['version'] and meta['size'] == len(app)
    assert meta['sha256'] == sha(app)
    assert meta['revision_min'] == minimum and meta['revision_max'] == maximum
    assert b'file_media_cap\0' in app, 'Missing final explicit-media capability'
    assert b'instrument_control_cap\0' in app and b'relative_data_cap\0' in app

    # Symbols in the actual production main object establish that the enabled
    # profile retained encoder/button/record callbacks. Do not infer from a ZIP name.
    commands = json.loads((build/'compile_commands.json').read_text(encoding='utf-8'))
    entry = next(x for x in commands if Path(x['file']).name == 'main.c')
    args = shlex.split(entry['command'].replace('\\', '/'))
    nm = Path(args[0]).with_name('riscv32-esp-elf-nm.exe')
    obj = build/'esp-idf/main/CMakeFiles/__idf_main.dir/main.c.obj'
    symbols = subprocess.check_output([str(nm), '-a', str(obj)], text=True)
    (out/'main-symbols.txt').write_text(symbols, encoding='utf-8')
    callbacks = {name: bool(re.search(r'\b'+name+r'$', symbols, re.M)) for name in
                 ('physical_detent', 'physical_press', 'physical_record')}
    assert all(value == controls for value in callbacks.values()), callbacks
    if controls:
        assert re.search(r'\bU tab5_input_gpio_start$', symbols, re.M), 'GPIO startup call missing'
    elf_symbols = subprocess.check_output([str(nm), '-a', str(build/'krenomer-esp32.elf')], text=True)
    (out/'elf-symbols.txt').write_text(elf_symbols, encoding='utf-8')
    linked_callbacks = {name: bool(re.search(r'\b'+name+r'$', elf_symbols, re.M)) for name in callbacks}
    assert all(value == controls for value in linked_callbacks.values()), linked_callbacks
    if controls:
        assert re.search(r'\b[Tt] tab5_input_gpio_start$', elf_symbols, re.M), 'GPIO startup not linked'

    flash = json.loads((build/'flasher_args.json').read_text(encoding='utf-8'))['flash_files']
    assert set(flash) == set(meta['factory_parts']) == {'0x2000', '0x8000', '0xf000', '0x20000'}
    for address, source in flash.items():
        name = meta['factory_parts'][address]
        raw = (build/source).read_bytes()
        assert raw == (source_package/name).read_bytes()
        assert sha(raw) == meta['parts_sha256'][name]
    assert verify_sums(source_package) == {
        name: sha((source_package/name).read_bytes()) for name in
        {*meta['factory_parts'].values(), 'krenomer-esp32.json'}}

    # Stage each profile separately. Never use a fallback package directory.
    stage = out/'package'
    stage.mkdir(exist_ok=True)
    prefix = 'p4-1x' if minimum == 100 else 'p4-3x'
    payload = stage/prefix
    payload.mkdir(exist_ok=True)
    for name in meta['factory_parts'].values():
        shutil.copy2(source_package/name, payload/name)
    meta.update(profile=a.profile, controls_enabled=controls,
                file_media_cap=True,
                controls_wiring={'a': 3, 'b': 4, 'press': 2, 'record': 48} if controls else None)
    (payload/'krenomer-esp32.json').write_text(json.dumps(meta, indent=2)+'\n', encoding='utf-8')
    (payload/'SHA256SUMS.txt').write_text(''.join(
        sha(path.read_bytes())+'  '+path.name+'\n' for path in sorted(payload.iterdir())
        if path.is_file() and path.name != 'SHA256SUMS.txt'), encoding='ascii')
    verify_sums(payload)
    readme = (
        f'M5Stack Tab5 — 1.8.6-esp32 — {a.profile}, ESP32-P4 rev{minimum}–{maximum}\n\n'
        f'Профиль внешних GPIO: {"CONTROLS ВКЛЮЧЁН" if controls else "выключен"}.\n'
        'USB/TCP: instrument heel|level; relative roles/axes/zero/reset/status.\n'
        'Явный выбор SD/USB объявляется file_media_cap:true в обычном status.\n'
        'Старые REL CSV и семантика записи сохранены.\n\n'
    )
    if controls:
        readme += (
            'Энкодер A=GPIO3, B=GPIO4, кнопка PRESS=GPIO2, запись REC=GPIO48.\n'
            'Сохранён опрос GPIO 1 мс, debounce 30 мс и длительное нажатие 600 мс.\n'
            'ON при загрузке не запускает запись автоматически. Ручной START\n'
            'доступен при OFF; ручной STOP при ON не вызывает повторный START.\n'
            'Диагностика входов: input status. Подключение — по прежней схеме\n'
            'controls; механическое подключение/электрические уровни здесь не проверялись.\n\n'
        )
    readme += (
        f'В {prefix}/ находятся OTA-приложение, четыре factory-части, manifest и SHA256.\n'
        f'Ограничение ревизий: {minimum}..{maximum}, проверьте фактическую ревизию прибора.\n'
        'Factory-части не являются одним OTA-образом. NVS стирать не требуется.\n'
        'Образ собран штатным ESP-IDF 5.5.2; app descriptor, headers, SHA и\n'
        'UI stack guard проверены. Аппаратная проверка не выполнялась;\n'
        'COM/оборудование/прошивка не использовались. PC поставляется parent отдельно.\n'
    )
    (stage/'README.txt').write_text(readme, encoding='utf-8')
    stage_files = sorted(path for path in stage.rglob('*') if path.is_file() and path != stage/'SHA256SUMS.txt')
    (stage/'SHA256SUMS.txt').write_text(''.join(
        sha(path.read_bytes())+'  '+path.relative_to(stage).as_posix()+'\n' for path in stage_files), encoding='ascii')
    verify_sums(stage)
    archive = a.out.resolve()/archive_name
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(stage.rglob('*')):
            if path.is_file():
                z.write(path, path.relative_to(stage).as_posix())
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        for path in stage.rglob('*'):
            if path.is_file():
                assert z.read(path.relative_to(stage).as_posix()) == path.read_bytes()
    receipt = {
        'status': 'PASS', 'profile': a.profile, 'controls_enabled': controls,
        'code_commit': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
        'build_directory': str(build), 'sdkconfig': str(sdk), 'sdkconfig_sha256': sha(sdk.read_bytes()),
        'descriptor': descriptor, 'app_header': image_header(app),
        'bootloader_header': image_header((build/'bootloader/bootloader.bin').read_bytes()),
        'image_bytes': len(app), 'image_sha256': sha(app), 'elf_sha256': elf_sha256,
        'main_object_sha256': sha(obj.read_bytes()),
        'gpio_callbacks_present': callbacks, 'gpio_callbacks_linked': linked_callbacks,
        'file_media_cap_in_image': True,
        'factory_parts': meta['factory_parts'], 'parts_sha256': meta['parts_sha256'],
        'manifest': str(payload/'krenomer-esp32.json'),
        'manifest_sha256': sha((payload/'krenomer-esp32.json').read_bytes()),
        'stack_guard': str(stack_path), 'stack_frames': stack['observed_bytes'],
        'stack_selected_chain_bytes': stack['selected_chain_bytes'],
        'archive': str(archive), 'archive_bytes': archive.stat().st_size,
        'archive_sha256': sha(archive.read_bytes()),
        'sources': {str(path.relative_to(ROOT)): sha(path.read_bytes()) for path in
                    [TAB5/'main/main.c', TAB5/'main/tab5_ui.h', TAB5/'main/tab5_services.c',
                     TAB5/'CMakeLists.txt', TAB5/'sdkconfig.defaults', TAB5/'sdkconfig.controls',
                     TAB5/'sdkconfig.rev3', Path(__file__)]},
        'hardware_tested': False, 'flashed': False,
    }
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k: receipt[k] for k in ['status', 'profile', 'controls_enabled', 'image_bytes',
                     'image_sha256', 'stack_frames', 'archive', 'archive_sha256']}, indent=2))


if __name__ == '__main__':
    main()
