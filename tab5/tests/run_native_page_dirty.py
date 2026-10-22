"""Compare bounded page redraw with full production frames; no OS windows/device."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
main = root / 'tab5/main'
cc = shutil.which('gcc')
if not cc:
    raise SystemExit('gcc is required')
sources = [root / 'tab5/tests/test_native_page_dirty.c']
sources += [main / s for s in ('tab5_ui.c','tab5_ui_graph.c','tab5_measure.c',
                              'tab5_time.c','tab5_plot.c','tab5_level.c',
                              'native/native_ui.c','native/font_native_fallback14.c')]
sources += sorted(main.glob('font_tab5_*.c'))
with tempfile.TemporaryDirectory(prefix='tab5-native-page-dirty-') as td:
    for fine in (0, 1):
        exe = Path(td) / f'page-dirty-fine{fine}.exe'
        subprocess.run([cc,'-std=c11','-O2',f'-DNUI_PROFILE_FINE_COUNTERS={fine}',
                        '-DTAB5_RELEASE_CAPTURE_BUILD=1','-DNUI_PROFILE_PRIMITIVE_TIMING=0',
                        '-I'+str(main),*[str(p) for p in sources],
                        '-lm','-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=120)
        print(f'page dirty40 fine={fine} timing=0 PASS',flush=True)
