"""Production observation state: no UART, windows, simulated firmware or devices."""
import argparse,hashlib,json,pathlib,shutil,subprocess
root=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--out',type=pathlib.Path,default=root/'.tools/workspace-core');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
sources=[root/'tab5/main/tab5_workspace.c',root/'tab5/main/tab5_relative.c',root/'tab5/tests/test_workspace.c']
exe=(a.out/'workspace.exe').resolve()
subprocess.run([shutil.which('gcc') or 'gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'tab5/main'),*[str(s) for s in sources],'-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True)
(a.out/'test.log').write_text(r.stdout+r.stderr,encoding='utf-8')
(a.out/'result.json').write_text(json.dumps({'returncode':r.returncode,'sources':{str(s.relative_to(root)):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources}},indent=2),encoding='utf-8')
print(r.stdout+r.stderr,end='');raise SystemExit(r.returncode)
