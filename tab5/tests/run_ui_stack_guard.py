"""Actual pinned P4 compiler/production flags, bounded boot-path frame guard.
--ui-source can point to the frozen 1.8.1 file for the negative control.
Does not alter project objects/build, task stack settings, or any device.
"""
from pathlib import Path
import argparse,hashlib,json,shlex,subprocess
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
p.add_argument('--build',type=Path,default=repo/'build-native')
p.add_argument('--ui-source',type=Path,default=repo/'tab5/main/tab5_ui.c')
a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
entries=json.loads((a.build/'compile_commands.json').read_text());frames=[];compiles=[]
for name in ['main.c','tab5_ui.c','tab5_native_port.c']:
    e=next(x for x in entries if Path(x['file']).name==name)
    args=shlex.split(e['command'].replace('\\','/'));args[args.index('-o')+1]=str(a.out/(name+'.o'))
    if name=='tab5_ui.c':args[args.index('-c')+1]=str(a.ui_source.resolve())
    args+=['-fstack-usage']
    if name=='tab5_ui.c':args+=['-Wframe-larger-than=2048','-Werror=frame-larger-than=2048']
    run=subprocess.run(args,cwd=e['directory'],capture_output=True)
    (a.out/(name+'-compile.log')).write_bytes(run.stdout+run.stderr)
    compiles.append({'unit':name,'returncode':run.returncode,'command':args})
    for f in a.out.glob(name+'*.su'):
        for line in f.read_text().splitlines():
            parts=line.split('\t');frames.append({'function':parts[0].rsplit(':',1)[-1],'bytes':int(parts[1]),'kind':parts[2],'unit':name})
limits={'app_main':2048,'tab5_native_port_start':256,'tab5_ui_init':512,'tab5_ui_update':2048}
observed={name:max((r['bytes'] for r in frames if r['function']==name or r['function'].startswith(name+'.')),default=-1) for name in limits}
passed=all(r['returncode']==0 for r in compiles) and all(0<=observed[k]<=limit for k,limit in limits.items()) and sum(observed.values())<=6144
result={'passed':passed,'ui_source':str(a.ui_source),'ui_sha256':hashlib.sha256(a.ui_source.read_bytes()).hexdigest(),'observed_bytes':observed,'selected_chain_bytes':sum(observed.values()),'limits':limits,'compiles':compiles,'frames':frames,'scope':'Selected own frames in actual boot call chain, excluding other callees/RTOS/interrupts. Not total runtime stack/high-water proof.'}
(a.out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'passed':passed,'observed':observed,'selected_chain_bytes':sum(observed.values())},indent=2));raise SystemExit(0 if passed else 1)
