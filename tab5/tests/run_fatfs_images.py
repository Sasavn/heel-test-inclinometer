"""Compile the production pinned FatFs generator output against RAM-only diskio.

Default configuration is the actual IDF build header. --defaults-preview applies
the project's sdkconfig.defaults to sdkconfig for early testing, explicitly marked
as a preview (never evidence that the firmware build has the same ABI).
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sdkconfig', type=Path, default=ROOT / 'tab5/build/config/sdkconfig.h')
    ap.add_argument('--idf-fatfs', type=Path, default=ROOT / '.tools/esp-idf/components/fatfs')
    ap.add_argument('--cc', default=shutil.which('gcc'))
    ap.add_argument('--defaults-preview', action='store_true')
    ap.add_argument('--generated-root', type=Path, default=ROOT / 'tab5/build/tab5-fatfs-generated')
    args = ap.parse_args()
    out = ROOT / '.tools/storage-1.6.15/fatfs-images'
    out.mkdir(parents=True, exist_ok=True)
    helper = ROOT / 'tab5/components/fatfs/prepare.py'
    spec = importlib.util.spec_from_file_location('project_fatfs_prepare', helper)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    manifest = module.prepare(args.idf_fatfs, out / 'generated')
    config = args.sdkconfig.read_text(encoding='utf-8')
    config_sources = {str(args.sdkconfig.resolve()): sha(args.sdkconfig)}
    if args.defaults_preview:
        values = {}
        for path in (ROOT / 'tab5/sdkconfig', ROOT / 'tab5/sdkconfig.defaults'):
            config_sources[str(path)] = sha(path)
            for line in path.read_text(encoding='utf-8').splitlines():
                m = re.fullmatch(r'(CONFIG_\w+)=(.*)', line)
                if m:
                    values[m[1]] = m[2]
        config = '\n'.join('#define ' + k + ' ' + ('1' if v == 'y' else v)
                           for k, v in values.items() if v != 'n') + '\n'
    report = {'configuration_mode': 'project_defaults_preview' if args.defaults_preview else 'idf_build_header',
              'config_sources_sha256': config_sources, 'generator_sha256': sha(helper),
              'source_manifest': manifest, 'passed': False,
              'host_disabled_bool_normalization': {},
              'scope': 'FatFs RAM images; no physical media, service integration or large-file transport test',
              'test_sources_sha256': {p.name: sha(p) for p in (Path(__file__), ROOT / 'tab5/tests/fatfs_image_test.c')}}
    report['compiler_version'] = subprocess.run([args.cc, '--version'], capture_output=True, text=True, check=True).stdout
    report['used_sdkconfig_sha256'] = hashlib.sha256(config.encode('utf-8')).hexdigest()
    generated = args.generated_root.resolve()
    report['diskio_header_sha256'] = sha(generated / 'diskio.h')
    report['compiled_generated_root'] = str(generated)
    report['compiled_generated_sha256'] = {name: sha(generated / name) for name in manifest['generated_sha256']}
    if report['compiled_generated_sha256'] != manifest['generated_sha256']:
        raise RuntimeError('Build generated sources differ from current production generator output')
    services = ROOT / 'tab5/main/tab5_services.c'
    service_text = services.read_text(encoding='utf-8')
    start = service_text.index('static bool supported_drive(')
    brace = service_text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (service_text[end] == '{') - (service_text[end] == '}')
        end += 1
    policy = service_text[start:end] + '\n'
    report['production_policy'] = {'source': str(services), 'source_sha256': sha(services),
                                  'function': 'supported_drive',
                                  'extracted_sha256': hashlib.sha256(policy.encode()).hexdigest(),
                                  'enum_header_sha256': sha(ROOT / 'tab5/main/tab5_services.h')}
    (out / 'production_fs_policy.h').write_text(policy, encoding='utf-8')
    # Temporary directories are deliberately restricted to this workspace.
    with tempfile.TemporaryDirectory(prefix='host-', dir=out) as tmp:
        tmp = Path(tmp)
        (tmp / 'freertos').mkdir()
        (tmp / 'sys').mkdir()
        (tmp / 'sdkconfig.h').write_text(config, encoding='utf-8')
        (tmp / 'production_fs_policy.h').write_text(policy, encoding='utf-8')
        (out / 'used-sdkconfig.h').write_text(config, encoding='utf-8')
        # Portability includes only: no FatFs feature or ABI macro overrides.
        (tmp / 'freertos/FreeRTOS.h').write_text('#define portTICK_PERIOD_MS 1\n')
        (tmp / 'freertos/semphr.h').write_text('/* single threaded host */\n')
        (tmp / 'sys/param.h').write_text('#define MIN(a,b) ((a)<(b)?(a):(b))\n#define MAX(a,b) ((a)>(b)?(a):(b))\n')
        exe = tmp / 'fatfs-image-test.exe'
        cmd = [args.cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-H', '-I'+str(generated),
               '-I'+str(tmp), '-I'+str(ROOT / 'tab5/main'),
               str(ROOT / 'tab5/tests/fatfs_image_test.c'), str(generated / 'ff.c'),
               str(generated / 'ffunicode.c'), '-o', str(exe)]
        report['compile_command'] = cmd
        result = subprocess.run(cmd, capture_output=True, text=True)
        (out / 'compile.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        trace = [line for line in result.stderr.splitlines() if re.match(r'^\.+ ', line)]
        (out / 'dependencies.log').write_text('\n'.join(trace) + '\n', encoding='utf-8')
        dependencies = sorted({line.split(' ', 1)[1].replace('\\', '/') for line in trace})
        report['compile_dependencies'] = dependencies
        abi_headers = [p for p in dependencies if Path(p).name in ('ff.h', 'ffconf.h', 'diskio.h')]
        report['abi_header_dependencies'] = abi_headers
        expected_headers = {str((generated / name).resolve()).replace('\\', '/') for name in ('ff.h', 'ffconf.h', 'diskio.h')}
        report['abi_dependency_check_passed'] = set(abi_headers) == expected_headers
        sdk_src = str((args.idf_fatfs / 'src').resolve()).replace('\\', '/').lower() + '/'
        report['sdk_src_dependency_absent'] = not any(p.lower().startswith(sdk_src) for p in dependencies)
        report['compile_returncode'] = result.returncode
        if result.returncode == 0:
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
            (out / 'run.log').write_text(result.stdout + result.stderr, encoding='utf-8')
            report['run_returncode'] = result.returncode
            report['results'] = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
            report['passed'] = (result.returncode == 0 and len(report['results']) == 5
                                and report['abi_dependency_check_passed'] and report['sdk_src_dependency_absent'])
            print(result.stdout + result.stderr)
        else:
            print(result.stdout + result.stderr)
    (out / 'proof.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if args.defaults_preview:
        for name in ('proof.json', 'compile.log', 'run.log', 'used-sdkconfig.h'):
            if (out / name).exists():
                shutil.copyfile(out / name, out / ('preview-' + name))
    print(out / 'proof.json')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
