from pathlib import Path
import hashlib,json,subprocess,sys,shutil
pack=Path(__file__).resolve().parent;source=pack/'source';root=Path('C:/Projects/ClaudeExp/diplom_project_tab5');logs=pack/'logs';logs.mkdir();rows=[]
def run(name,args):
 assert not (root/'.tools/native-opt/BENCHMARK_GATE').exists(),'benchmark gate active'
 p=subprocess.run(args,cwd=source,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=360)
 log=logs/(name+'.log');log.write_bytes(p.stdout);rows.append({'name':name,'status':'PASS' if p.returncode==0 else 'FAIL','returncode':p.returncode,'log':str(log),'command':args});(pack/'TEST_RESULTS.json').write_text(json.dumps(rows,indent=2));print(name,p.returncode,flush=True)
 if p.returncode:print(p.stdout.decode(errors='replace')[-2000:],flush=True);raise SystemExit(p.returncode)
run('targeted',[sys.executable,str(source/'tab5/tests/run_native_cache_keyboard.py'),'--group','all','--out',str(pack/'targeted'),'--benchmark-gate',str(root/'.tools/native-opt/BENCHMARK_GATE')])
run('atomic-role',[sys.executable,str(source/'tab5/tests/run_native_atomic_role.py'),'--out',str(pack/'atomic-role'),'--benchmark-gate',str(root/'.tools/native-opt/BENCHMARK_GATE')])
for name in ['run_native_gradient','run_native_profile_flags','run_draw_profile_mode','run_native_lifetime','run_native_page_dirty']:
 run(name,[sys.executable,str(source/'tab5/tests'/(name+'.py'))])
main=source/'tab5/main';cc=shutil.which('gcc');exe=logs/'final-regressions.exe'
run('final-regressions-build',[cc,'-std=c11','-O2','-DNUI_PROFILE_FINE_COUNTERS=1','-DNUI_PROFILE_PRIMITIVE_TIMING=0','-I'+str(main),str(source/'tab5/tests/test_native_final_regressions.c'),str(main/'native/font_native_fallback14.c'),'-lm','-o',str(exe)])
run('final-regressions',[str(exe)])
manifest=json.loads((pack/'SOURCE_MANIFEST.json').read_text());assert all(hashlib.sha256((source/n).read_bytes()).hexdigest()==h for n,h in manifest['files'].items());print('ALL_TARGETED_HELPERS PASS; snapshot unchanged',flush=True)
