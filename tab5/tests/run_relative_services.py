"""Real service writer + temporary host files, never a device."""
import argparse,pathlib,shutil,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--out',type=pathlib.Path,default=root/'.tools/dual-mode-services');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
exe=(a.out/'test_relative_services.exe').resolve()
subprocess.run([shutil.which('gcc') or 'gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'tab5/main'),str(root/'tab5/tests/test_relative_services.c'),'-lbcrypt','-lm','-o',str(exe)],check=True)
with tempfile.TemporaryDirectory(dir=a.out) as td:
 r=subprocess.run([str(exe)],cwd=td,capture_output=True,text=True)
 for f in pathlib.Path(td).rglob('*_REL.CSV'):shutil.copy2(f,a.out/(f.parent.name+'_'+f.name))
 (a.out/'test.log').write_text(r.stdout+r.stderr,encoding='utf-8');print(r.stdout+r.stderr,end='')
 raise SystemExit(r.returncode)
