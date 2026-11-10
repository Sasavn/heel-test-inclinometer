"""Build pure C raster regression and benchmark; no LVGL or hardware."""
from pathlib import Path
import shutil
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
def main():
    cc = shutil.which('gcc')
    if not cc:
        raise SystemExit('gcc required')
    with tempfile.TemporaryDirectory(prefix='tab5-plot-') as directory:
        exe = Path(directory)/'plot.exe'
        subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I'+str(ROOT/'tab5/main'), str(HERE/'test_plot.c'),
                        str(ROOT/'tab5/main/tab5_plot.c'), '-lm', '-o', str(exe)], check=True)
        return subprocess.run([str(exe)], timeout=30, check=False).returncode
if __name__ == '__main__':
    raise SystemExit(main())
