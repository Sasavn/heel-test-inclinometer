"""Build/run real LVGL software renderer without SDL, windows or hardware.
Usage: python tab5/tests/run_ui_graph_render_test.py [--out PATH]
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
p.add_argument('--out', type=Path, default=ROOT/'tab5'/'tests'/'output_graph_render')
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
sources += [ROOT/'tab5'/'tests'/'test_ui_graph_render.c', ROOT/'tab5'/'main'/'tab5_ui.c',ROOT/'tab5'/'main'/'tab5_measure.c']
sources += [ROOT/'tab5'/'main'/'tab5_ui_graph.c', ROOT/'tab5'/'main'/'tab5_time.c']
sources += sorted((ROOT/'tab5'/'main').glob('font_tab5_*.c'))
flags = ['-std=c11','-O1','-DLV_CONF_SKIP','-DLV_COLOR_DEPTH=32','-DLV_USE_LOG=0',
         '-DLV_MEM_SIZE=8388608','-DLV_USE_OS=0','-DLV_USE_DRAW_SW=1','-DLV_USE_DRAW_SW_ASM=0',
         '-I'+str(lvgl),'-I'+str(ROOT/'tab5'/'main')]
headers = sorted(lvgl.rglob('*.h')) + sorted((ROOT/'tab5'/'main').glob('*.h'))
fingerprint = str(ROOT) + str(flags) + ''.join(str(h.stat().st_mtime_ns) for h in headers)
cache = Path(tempfile.gettempdir()) / ('tab5-lvgl-headless-' + hashlib.sha256(fingerprint.encode()).hexdigest()[:12])
cache.mkdir(exist_ok=True)
def compile_one(source):
    obj = cache / (hashlib.sha256(str(source).encode()).hexdigest()[:20]+'.o')
    if source.name in ('tab5_ui_graph.c','test_ui_graph_render.c') or not obj.exists() or obj.stat().st_mtime_ns < source.stat().st_mtime_ns:
        subprocess.run([compiler,*flags,'-c',str(source),'-o',str(obj)],check=True)
    return obj
with concurrent.futures.ThreadPoolExecutor(max_workers=min(8,os.cpu_count() or 2)) as pool:
    objects = list(pool.map(compile_one,sources))
exe = cache/'test_ui.exe'
rsp = cache/'link.rsp'
args = [str(o) for o in objects] + ['-lm','-o',str(exe)]
rsp.write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in args),encoding='utf-8')
subprocess.run([compiler,'@'+str(rsp)],check=True)
import time
started = time.time_ns()
result = subprocess.run([str(exe),str(a.out)],check=False,timeout=90)
captures = []
for ppm in a.out.glob('*.ppm'):
    if ppm.stat().st_mtime_ns < started:
        continue
    with Image.open(ppm) as im: im.save(ppm.with_suffix('.png'))
    captures.append(ppm.with_suffix('.png'))
# A review sheet of only this run's actual LVGL flush captures.
if captures:
    from PIL import ImageDraw
    captures.sort()
    sheet = Image.new('RGB',(384*4,238*((len(captures)+3)//4)), '#e9edf4')
    draw = ImageDraw.Draw(sheet)
    for index, path in enumerate(captures):
        with Image.open(path) as im:
            im = im.convert('RGB'); im.thumbnail((384,216))
            x, y = (index%4)*384, (index//4)*238
            sheet.paste(im,(x,y)); draw.text((x+4,y+219),path.stem,fill='#1a1d24')
    sheet.save(a.out/'contact_sheet.png')
raise SystemExit(result.returncode)
