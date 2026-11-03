"""Exercise production pair engine without UART, OS windows or device access."""
import argparse, hashlib, json, pathlib, shutil, subprocess
root=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--out',type=pathlib.Path,default=root/'.tools/dual-mode-core');a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=True)
sources=[root/'tab5/main/tab5_relative.c',root/'tab5/tests/test_relative.c']
exe=a.out/'test_relative.exe'
command=[shutil.which('gcc') or 'gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'tab5/main'),*[str(s) for s in sources],'-lm','-o',str(exe)]
subprocess.run(command,check=True)
r=subprocess.run([str(exe.resolve())],capture_output=True,text=True)
(a.out/'test.log').write_text(r.stdout+r.stderr,encoding='utf-8')
(a.out/'result.json').write_text(json.dumps({'returncode':r.returncode,'sources':{str(s.relative_to(root)):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources}},indent=2),encoding='utf-8')
print(r.stdout+r.stderr,end='');raise SystemExit(r.returncode)
