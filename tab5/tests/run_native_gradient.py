"""Verify bounded gradient LUT keys and exact uncached-renderer values."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
main = root/'tab5/main'
cc = shutil.which('gcc')
if not cc:
    raise SystemExit('gcc required')
with tempfile.TemporaryDirectory(prefix='tab5-gradient-') as td:
    exe = Path(td)/'gradient.exe'
    subprocess.run([cc,'-std=c11','-O2','-I'+str(main),
                    str(root/'tab5/tests/test_native_gradient.c'),str(main/'native/native_ui.c'),
                    str(main/'native/font_native_fallback14.c'),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
