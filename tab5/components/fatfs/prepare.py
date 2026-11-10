"""Generate the pinned project FatFS core without modifying the installed SDK."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def prepare(upstream, output):
    upstream, output = Path(upstream).resolve(), Path(output).resolve()
    if output == upstream or upstream in output.parents or output in upstream.parents:
        raise ValueError('Generated FatFS must be outside the SDK component')
    pin = json.loads(Path(__file__).with_name('upstream.json').read_text(encoding='utf-8'))
    contents = {}
    for name, expected in pin['sha256'].items():
        data = (upstream / name).read_bytes().replace(b'\r\n', b'\n')
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError('Unexpected pinned FatFS source: ' + name)
        contents[name] = data
    pattern = rb'^#define FF_FS_EXFAT[\t ]+0$'
    replacement = (b'#ifdef CONFIG_FATFS_EXFAT\n#define FF_FS_EXFAT 1\n'
                   b'#else\n#define FF_FS_EXFAT 0\n#endif')
    contents['src/ffconf.h'], count = re.subn(pattern, replacement, contents['src/ffconf.h'], flags=re.M)
    if count != 1:
        raise ValueError('Expected exactly one upstream exFAT switch')
    output.mkdir(parents=True, exist_ok=True)
    generated = {}
    for name in ('ff.c', 'ff.h', 'ffconf.h', 'ffunicode.c', 'diskio.h'):
        data = contents['src/' + name]
        path = output / name
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
        generated[name] = hashlib.sha256(data).hexdigest()
    manifest = dict(upstream=pin, generated_sha256=generated,
                    change='Only ffconf.h FF_FS_EXFAT hardcoded 0 -> CONFIG_FATFS_EXFAT. Licenses retained.')
    (output / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf-fatfs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.idf_fatfs, args.output)
