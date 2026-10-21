"""Build/run real LVGL software renderer without SDL, windows or hardware.
Usage: python tab5/tests/run_ui_test.py [--out PATH]
Requires gcc, Python Pillow, and managed_components/lvgl__lvgl (idf dependencies).
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from PIL import Image
ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, default=ROOT/'tab5'/'tests'/'output_ui')
a = p.parse_args()
a.out = a.out.resolve(); a.out.mkdir(parents=True, exist_ok=True)
lvgl = ROOT/'tab5'/'managed_components'/'lvgl__lvgl'
compiler = shutil.which('gcc')
if not compiler or not lvgl.is_dir():
    raise SystemExit('Need gcc on PATH and tab5/managed_components/lvgl__lvgl')
# Cache software-renderer objects outside the repository for fast reruns.
import concurrent.futures
import hashlib
sources = sorted((lvgl/'src').rglob('*.c'))
sources += [ROOT/'tab5'/'tests'/'test_ui.c', ROOT/'tab5'/'main'/'tab5_ui.c',ROOT/'tab5'/'main'/'tab5_measure.c']
sources += sorted((ROOT/'tab5'/'main').glob('font_tab5_*.c'))
flags = ['-std=c11','-O1','-DLV_CONF_SKIP','-DLV_COLOR_DEPTH=32','-DLV_USE_LOG=0',
         '-DLV_USE_OS=0','-DLV_USE_DRAW_SW=1','-DLV_USE_DRAW_SW_ASM=0',
         '-I'+str(lvgl),'-I'+str(ROOT/'tab5'/'main')]
headers = sorted(lvgl.rglob('*.h')) + sorted((ROOT/'tab5'/'main').glob('*.h'))
fingerprint = str(ROOT) + str(flags) + ''.join(str(h.stat().st_mtime_ns) for h in headers)
cache = Path(tempfile.gettempdir()) / ('tab5-lvgl-headless-' + hashlib.sha256(fingerprint.encode()).hexdigest()[:12])
cache.mkdir(exist_ok=True)
def compile_one(source):
    obj = cache / (hashlib.sha256(str(source).encode()).hexdigest()[:20]+'.o')
    if not obj.exists() or obj.stat().st_mtime_ns < source.stat().st_mtime_ns:
        subprocess.run([compiler,*flags,'-c',str(source),'-o',str(obj)],check=True)
    return obj
with concurrent.futures.ThreadPoolExecutor(max_workers=min(8,os.cpu_count() or 2)) as pool:
    objects = list(pool.map(compile_one,sources))
exe = cache/'test_ui.exe'
rsp = cache/'link.rsp'
args = [str(o) for o in objects] + ['-lm','-o',str(exe)]
rsp.write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in args),encoding='utf-8')
subprocess.run([compiler,'@'+str(rsp)],check=True)
result = subprocess.run([str(exe),str(a.out)],check=False)
for ppm in a.out.glob('*.ppm'):
    with Image.open(ppm) as im: im.save(ppm.with_suffix('.png'))
raise SystemExit(result.returncode)
