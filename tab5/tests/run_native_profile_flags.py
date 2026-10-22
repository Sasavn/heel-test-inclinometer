"""Check real renderer's fine/timing gates while preserving coarse accounting."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
main = root/'tab5/main'
cc = shutil.which('gcc')
if not cc:
    raise SystemExit('gcc required')
with tempfile.TemporaryDirectory(prefix='tab5-native-flags-') as td:
    for fine,timed in [(0,0),(1,0),(1,1),(0,1)]:
        exe = Path(td)/f'flags-{fine}-{timed}.exe'
        subprocess.run([cc,'-std=c11','-O2',f'-DNUI_PROFILE_FINE_COUNTERS={fine}',
                        f'-DNUI_PROFILE_PRIMITIVE_TIMING={timed}','-I'+str(main),
                        str(root/'tab5/tests/test_native_profile_flags.c'),str(main/'native/native_ui.c'),
                        str(main/'native/font_native_fallback14.c'),'-lm','-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=30)
