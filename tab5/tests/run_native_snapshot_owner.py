"""Headless production renderer, no device/COM/network or OS window."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess
repo=Path(__file__).resolve().parents[2];main=repo/'tab5/main'
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
sources=[repo/'tab5/tests/test_native_snapshot_owner.c']+[main/n for n in ['tab5_ui_graph.c','tab5_measure.c','tab5_time.c','tab5_plot.c','tab5_level.c','native/native_ui.c','native/font_native_fallback14.c']]+sorted(main.glob('font_tab5_*.c'))
exe=(a.out/'snapshot-owner.exe').resolve()
cmd=[shutil.which('gcc'),'-std=c11','-O2','-Wall','-Wextra','-Werror=misleading-indentation','-Werror=format-truncation','-DNUI_PROFILE_FINE_COUNTERS=0','-DNUI_PROFILE_PRIMITIVE_TIMING=0','-I'+str(main),*[str(s) for s in sources],'-lm','-o',str(exe)]
r=subprocess.run(cmd,capture_output=True);(a.out/'compile.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(exe)],capture_output=True);(a.out/'test.log').write_bytes(r.stdout+r.stderr)
(a.out/'result.json').write_text(json.dumps({'returncode':r.returncode,'command':cmd,'ui_sha256':hashlib.sha256((main/'tab5_ui.c').read_bytes()).hexdigest()},indent=2)+'\n')
print((r.stdout+r.stderr).decode(errors='replace'));raise SystemExit(r.returncode)
