"""Exercise the production encoder, touch and modal behavior; no OS window/device."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import argparse
import hashlib
import json
from datetime import datetime, timezone
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument("--capture-dir", type=Path, help="Save six native NTP fixture PNGs and source hashes")
args = parser.parse_args()

root = Path(__file__).resolve().parents[2]
main = root / 'tab5/main'
cc = shutil.which('gcc')
if not cc:
    raise SystemExit('gcc is required')
sources = [root / 'tab5/tests/test_native_encoder.c']
sources += [main / s for s in ('tab5_ui_graph.c','tab5_measure.c',
                              'tab5_time.c','tab5_plot.c','tab5_level.c',
                              'native/native_ui.c','native/font_native_fallback14.c')]
sources += sorted(main.glob('font_tab5_*.c'))
build_root = root / '.tools/native-large/encoder-build'
build_root.mkdir(parents=True, exist_ok=True)
capture_root = args.capture_dir or (root / '.tools/native-large/encoder-captures' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
capture_root = capture_root.resolve()
capture_root.mkdir(parents=True, exist_ok=True)
raw_frames = {}
with tempfile.TemporaryDirectory(dir=build_root, prefix='run-', ignore_cleanup_errors=True) as td:
    for fine in (0, 1):
        exe = Path(td) / f'encoder-fine{fine}.exe'
        subprocess.run([cc,'-std=c11','-O2','-Werror=misleading-indentation',f'-DNUI_PROFILE_FINE_COUNTERS={fine}',
                        '-DTAB5_RELEASE_CAPTURE_BUILD=1','-DNUI_PROFILE_PRIMITIVE_TIMING=0',
                        '-I'+str(main),*[str(p) for p in sources],
                        '-lm','-o',str(exe)],check=True)
        raw_dir = Path(td) / f'frames-fine{fine}'
        raw_dir.mkdir()
        subprocess.run([str(exe),str(raw_dir)],check=True,timeout=120)
        for frame in sorted(raw_dir.glob('*.rgb565')):
            data = frame.read_bytes()
            assert len(data) == 1280*720*2
            if fine == 0:
                raw_frames[frame.stem] = data
            else:
                assert data == raw_frames[frame.stem], f'profile frame mismatch: {frame.stem}'
                Image.frombytes('RGB',(1280,720),data,'raw','BGR;16').save(capture_root / (frame.stem+'.png'))
        print(f'native encoder fine={fine} timing=0 PASS',flush=True)

assert len(raw_frames) == 6
hashes = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
          for path in [Path(__file__), *sources, main / 'tab5_ui.c', main / 'tab5_ui.h']}
manifest = {"kind": "production native RGB565 renderer with injected backend snapshots; no network or device",
            "profiles": [0,1], "themes": ["dark","light"],
            "states": ["pending","success","error"],
            "rgb565_profiles_equal": True, "source_sha256": hashes,
            "frames": {name: hashlib.sha256(data).hexdigest() for name,data in raw_frames.items()}}
(capture_root / 'run.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(f'NTP_CAPTURES 6 PNG; fine0 == fine1 RGB565 PASS: {capture_root}',flush=True)
