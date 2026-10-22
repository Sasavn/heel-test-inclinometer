"""Check production native UI lifetime without OS windows or devices."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
main = root / 'tab5/main'
cc = shutil.which('gcc')
if not cc:
    raise SystemExit('gcc is required')
sources = [root / 'tab5/tests/test_native_lifetime.c']
sources += [main / s for s in ('tab5_ui.c','tab5_ui_graph.c','tab5_measure.c',
                              'tab5_time.c','tab5_plot.c','tab5_level.c',
                              'native/native_ui.c','native/font_native_fallback14.c')]
sources += sorted(main.glob('font_tab5_*.c'))
with tempfile.TemporaryDirectory(prefix='tab5-native-lifetime-') as td:
    exe = Path(td) / 'lifetime.exe'
    subprocess.run([cc,'-std=c11','-O2','-I'+str(main),*[str(p) for p in sources],
                    '-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=120)
