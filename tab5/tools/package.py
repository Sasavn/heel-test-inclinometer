"""Package actual IDF application and factory parts; never accesses serial/USB."""
from pathlib import Path
import hashlib
import json
import shutil
import struct
import sys


def inspect_app(data):
    if len(data) < 288 or data[0] != 0xE9:
        raise ValueError('Not an ESP application image')
    chip = struct.unpack_from('<H', data, 12)[0]
    if chip != 18:
        raise ValueError(f'Wrong chip id {chip}: ESP32-P4 (18) required')
    # First segment follows the 24-byte image header; app descriptor is at +8.
    if struct.unpack_from('<I', data, 32)[0] != 0xABCD5432:
        raise ValueError('Missing ESP-IDF application descriptor')
    version = data[48:80].split(b'\0')[0].decode('utf-8')
    project = data[80:112].split(b'\0')[0].decode('utf-8')
    if project != 'krenomer-esp32' or not version.endswith('-esp32'):
        raise ValueError('Wrong application: project/version must identify esp32 fork')
    return version


def main():
    build = Path(sys.argv[1]).resolve()
    image = build / 'krenomer-esp32.bin'
    version = inspect_app(image.read_bytes())
    out = build / 'package'
    out.mkdir(exist_ok=True)
    shutil.copy2(image, out / 'krenomer-esp32.bin')
    args = json.loads((build / 'flasher_args.json').read_text(encoding='utf-8'))
    if set(args['flash_files']) != {'0x2000', '0x8000', '0xf000', '0x20000'}:
        raise ValueError('Unexpected factory layout; require bootloader/partitions/otadata/ota_0 only')
    parts = {}
    for address, name in args['flash_files'].items():
        source = build / name
        dest = 'krenomer-esp32.bin' if source == image else Path(name).name
        shutil.copy2(source, out / dest)
        parts[address] = dest
    metadata = {'format': 1, 'board': 'tab5', 'chip': 'esp32p4',
                'protocol': 2, 'version': version, 'kind': 'ota-app',
                'size': image.stat().st_size,
                'sha256': hashlib.sha256(image.read_bytes()).hexdigest(),
                'factory_parts': parts,
                'warning': 'Untested on hardware; never send factory image to OTA'}
    data = image.read_bytes()
    metadata['revision_min'] = struct.unpack_from('<H', data, 15)[0] or data[14] * 100
    metadata['revision_max'] = struct.unpack_from('<H', data, 17)[0]
    metadata['parts_sha256'] = {name: hashlib.sha256((out / name).read_bytes()).hexdigest()
                                for name in parts.values()}
    (out / 'krenomer-esp32.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    files = sorted(p for p in out.iterdir() if p.is_file() and p.name != 'SHA256SUMS.txt')
    (out / 'SHA256SUMS.txt').write_text(''.join(
        hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name + '\n' for p in files), encoding='ascii')
    print(out)


if __name__ == '__main__':
    main()
