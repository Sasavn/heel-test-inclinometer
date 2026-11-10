from pathlib import Path
import hashlib,json,subprocess,shutil
root=Path(__file__).resolve().parents[2];main=root/'tab5/main'
import argparse
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,default=root.parent/'inclinometer-feedback/2026-11-04-first-dual-mode/ui');a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
frozen=hashlib.sha256((main/'tab5_ui.c').read_bytes()).hexdigest()
sources=[root/'tab5/tests/test_native_dual_performance.c']+[main/n for n in ['tab5_ui_graph.c','tab5_measure.c','tab5_time.c','tab5_plot.c','tab5_level.c','native/native_ui.c','native/font_native_fallback14.c']]+sorted(main.glob('font_tab5_*.c'))
exe=out/'performance-probe.exe'
subprocess.run([shutil.which('gcc'),'-std=c11','-O2','-Werror=format-truncation','-DNUI_PROFILE_FINE_COUNTERS=0','-DNUI_PROFILE_PRIMITIVE_TIMING=0','-I'+str(main),*[str(p) for p in sources],'-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe),str(out/'performance-raw.json')],capture_output=True,text=True,timeout=90);(out/'performance.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
assert frozen==hashlib.sha256((main/'tab5_ui.c').read_bytes()).hexdigest()
data=json.loads((out/'performance-raw.json').read_text());data['scenes']+=data.pop('theme_0_followup_scenes',[]);data.pop('theme_1_followup_scenes',None);data['idle_gate_pass']=all(x['rendered_frames']==0 and x['pending_dirty']==0 for x in data['scenes']);data['status']='PASS' if data['idle_gate_pass'] else 'FAIL_IDLE_REDRAW';data['ui_sha256']=frozen;data['source_sha256']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources+[main/'tab5_ui.c',main/'tab5_ui.h']};(out/'performance.json').write_text(json.dumps(data,indent=2));print('Performance JSON verified; frozen UI unchanged.')

assert data['idle_gate_pass'], 'FAIL: identical dual snapshot caused dirty/render; see performance.json'
