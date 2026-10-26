"""Build two checked revision-specific GitHub ZIP assets, without device access."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rev1', type=Path, required=True)
    p.add_argument('--rev3', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    reports = []
    for profile, build, lo, hi in [('p4-rev1', a.rev1, 100, 199),
                                  ('p4-rev3', a.rev3, 300, 399)]:
        subprocess.run([sys.executable, str(Path(__file__).with_name('package.py')),
                        str(build)], check=True)
        folder = build / 'package'
        meta = json.loads((folder / 'krenomer-esp32.json').read_text(encoding='utf-8'))
        if (meta['revision_min'], meta['revision_max']) != (lo, hi):
            raise ValueError('Silicon revision mismatch: ' + profile)
        if meta['version'] != '1.7.0-esp32':
            raise ValueError('Expected 1.7.0-esp32 candidate')
        files = {name: (folder / name).read_bytes() for name in meta['parts_sha256']}
        for name, expected in meta['parts_sha256'].items():
            if digest(files[name]) != expected:
                raise ValueError('Part hash mismatch: ' + name)
        if digest(files['krenomer-esp32.bin']) != meta['sha256']:
            raise ValueError('OTA hash mismatch')
        files['krenomer-esp32.json'] = (folder / 'krenomer-esp32.json').read_bytes()
        files['README.txt'] = (
            'Krenomer Tab5 1.7.0-esp32; hardware-unverified prerelease.\n'
            f'ESP32-P4 silicon revisions {lo}-{hi} only ({profile}).\n'
            'Not for STM32. Do not choose revision from the tablet appearance.\n'
            'Only krenomer-esp32.bin is an OTA application; select matching JSON.\n'
            'Factory parts are separate: bootloader 0x2000, partition table 0x8000,\n'
            'initial OTA data 0xf000, application 0x20000. No NVS image/erase.\n'
            'Do not send bootloader/partition/OTA-data images as OTA application.\n'
            'Factory OTA-data resets the boot-slot selection: use factory mode\n'
            'only for the explicit recovery/install operation, not a normal update.\n'
            'P4 firmware does not update the C6 radio firmware.\n'
            'Build/host tests passed; board boot, FPS/latency, WiFi/NTP and RTC\n'
            'retention were not tested for this candidate.\n'
            'Sources: https://github.com/Sasavn/heel-test-inclinometer/tree/tab5-esp32\n'
        ).encode('utf-8')
        files['SHA256SUMS.txt'] = ''.join(
            digest(data) + '  ' + name + '\n' for name, data in sorted(files.items())
        ).encode('ascii')
        target = a.out / ('Krenomer-Tab5-1.7.0-esp32-' + profile + '.zip')
        with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for name, data in sorted(files.items()):
                info = zipfile.ZipInfo(name, date_time=(2026, 10, 26, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                z.writestr(info, data)
        with zipfile.ZipFile(target) as z:
            if z.testzip() is not None:
                raise ValueError('Corrupt ZIP')
            for name, data in files.items():
                if z.read(name) != data:
                    raise ValueError('ZIP content mismatch: ' + name)
        reports.append(dict(asset=target.name, bytes=target.stat().st_size,
                            archive_sha256=digest(target.read_bytes()), profile=profile,
                            hardware_tested=False, flashed=False, **meta))
    (a.out / 'delivery.json').write_text(
        json.dumps(dict(version='1.7.0-esp32', prerelease=True, packages=reports),
                   indent=2) + '\n', encoding='utf-8')
    (a.out / 'SHA256SUMS.txt').write_text(''.join(
        item['archive_sha256'] + '  ' + item['asset'] + '\n' for item in reports), encoding='ascii')
    print(json.dumps(reports, indent=2))


if __name__ == '__main__':
    main()
