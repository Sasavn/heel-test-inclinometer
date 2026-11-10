"""Pinned generation is reproducible, fails closed, and never edits the SDK."""
from pathlib import Path
import hashlib
import importlib.util
import shutil
import tempfile

root = Path(__file__).resolve().parents[2]
component = root / 'tab5/components/fatfs'
spec = importlib.util.spec_from_file_location('fatfs_prepare', component / 'prepare.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
sdk = root / '.tools/esp-idf/components/fatfs'
original = (sdk / 'src/ffconf.h').read_bytes()
with tempfile.TemporaryDirectory(prefix='fatfs-overlay-', dir=root / '.tools') as temp:
    temp = Path(temp)
    first = module.prepare(sdk, temp / 'first')
    second = module.prepare(sdk, temp / 'second')
    assert first == second
    assert set(first['generated_sha256']) == {'ff.c','ff.h','ffconf.h','ffunicode.c','diskio.h'}
    assert b'#ifdef CONFIG_FATFS_EXFAT' in (temp / 'first/ffconf.h').read_bytes()
    for name, expected in first['generated_sha256'].items():
        assert hashlib.sha256((temp / 'first' / name).read_bytes()).hexdigest() == expected
    # A corrupt source is rejected before an output directory is created.
    upstream = temp / 'upstream'
    upstream.mkdir()
    for name in first['upstream']['sha256']:
        target = upstream / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(sdk / name, target)
    with (upstream / 'src/ffconf.h').open('ab') as changed:
        changed.write(b'\n/* unexpected SDK mutation */\n')
    try:
        module.prepare(upstream, temp / 'rejected')
        raise AssertionError('Modified upstream accepted')
    except ValueError:
        assert not (temp / 'rejected').exists()
assert (sdk / 'src/ffconf.h').read_bytes() == original
print('FatFS overlay: deterministic five-file generation, pin mismatch refusal, SDK unchanged PASS')
