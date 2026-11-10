from pathlib import Path
import subprocess,shutil,json,hashlib
from PIL import Image
root=Path(__file__).resolve().parents[2];main=root/'tab5/main'
import argparse
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,default=root.parent/'inclinometer-feedback/2026-11-04-first-dual-mode/ui');a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
sources=[root/'tab5/tests/test_native_dual.c']+[main/n for n in ['tab5_ui_graph.c','tab5_measure.c','tab5_time.c','tab5_plot.c','tab5_level.c','native/native_ui.c','native/font_native_fallback14.c']]+sorted(main.glob('font_tab5_*.c'))
exe=out/'native-dual.exe'
subprocess.run([shutil.which('gcc'),'-std=c11','-O2','-Werror=misleading-indentation','-Werror=format-truncation','-DNUI_PROFILE_FINE_COUNTERS=0','-DNUI_PROFILE_PRIMITIVE_TIMING=0','-I'+str(main),*[str(p) for p in sources],'-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe),str(out)],capture_output=True,text=True,timeout=45);(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
for p in out.glob('*.rgb565'):Image.frombytes('RGB',(1280,720),p.read_bytes(),'raw','BGR;16').save(p.with_suffix('.png'))
(out/'sources.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [main/'tab5_ui.c',main/'tab5_ui.h',*sources]},indent=2))
