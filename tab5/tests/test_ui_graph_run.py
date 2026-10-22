"""Compile/run actual pure graph with actual LVGL headers; no renderer or hardware."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=HERE.parents[1])
    args = parser.parse_args()
    root = args.source_root.resolve()
    compiler = shutil.which('gcc')
    if not compiler:
        raise SystemExit('gcc required on PATH')
    lvgl = root/'tab5/managed_components/lvgl__lvgl'
    with tempfile.TemporaryDirectory(prefix='tab5-graph-') as directory:
        exe = Path(directory)/'graph.exe'
        command = [compiler, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                   '-DLV_CONF_SKIP', '-DLV_COLOR_DEPTH=32', '-DLV_USE_LOG=0',
                   '-DLV_USE_OS=0', '-DLV_USE_DRAW_SW_ASM=0',
                   '-I'+str(lvgl), '-I'+str(root/'tab5/main'),
                   str(HERE/'test_ui_graph.c'), str(root/'tab5/main/tab5_ui_graph.c'),
                   '-lm', '-o', str(exe)]
        subprocess.run(command, check=True, timeout=60)
        return subprocess.run([str(exe)], check=False, timeout=20).returncode

if __name__ == '__main__':
    raise SystemExit(main())
