"""Shared services startup + paused media boundary + explicit OTA host tests.

Only fake media/flash under a fresh ignored output directory. No devices, COM,
GUI or network listeners. Source is snapshotted before compiling.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path)
    args=parser.parse_args()
    out=args.out.resolve() if args.out else ROOT/'.tools/native-1.6.15'/('services-'+time.strftime('%Y%m%d-%H%M%S'))
    if out.exists() and any(out.iterdir()):parser.error('Use a fresh evidence directory')
    snapshot=out/'source';snapshot.mkdir(parents=True)
    files=list((ROOT/'tab5/main').glob('*.h'))+[ROOT/'tab5/main/main.c',ROOT/'tab5/main/tab5_services.c',ROOT/'tab5/sdkconfig.defaults',ROOT/'tab5/CMakeLists.txt']
    files+=list((ROOT/'tab5/tests').glob('test_services*'))+[ROOT/'tab5/tests/test_retry_backoff.c',ROOT/'tab5/tests/run_services_startup_guard.py',Path(__file__).resolve()]
    files=sorted(set(p for p in files if p.is_file()))
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    hashes={str(p.relative_to(ROOT)):sha(p) for p in files}
    for p in files:
        dest=snapshot/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
    assert all(sha(p)==hashes[str(p.relative_to(ROOT))] for p in files),'sources changed during snapshot'
    for p in snapshot.rglob('*'):
        if p.is_file():p.chmod(0o444)
    (out/'SOURCE_MANIFEST.json').write_text(json.dumps(hashes,indent=2))
    report={'status':'RUNNING','source_manifest':str(out/'SOURCE_MANIFEST.json'),'results':[]}
    def save():(out/'SERVICES_GATE.json').write_text(json.dumps(report,indent=2))
    save();cc=shutil.which('gcc');assert cc,'gcc required'
    version=re.search(r'set\(PROJECT_VER\s+"([^"]+)"\)',(snapshot/'tab5/CMakeLists.txt').read_text())[1]
    exe=out/'services-deferred.exe'
    build=subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-O2','-finstrument-functions','-DTAB5_TEST_RELEASE_VERSION="'+version+'"','-I'+str(snapshot/'tab5/main'),str(snapshot/'tab5/tests/test_services_deferred.c'),'-lbcrypt','-lm','-o',str(exe)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/'build.log').write_bytes(build.stdout)
    if build.returncode:
        report.update(status='FAIL',reason='build');save();print(out/'build.log');return 1
    scenarios=['sd-timeout','optional-usb-failure','paused-sd','snapshot-contention','sd-bin-ignored','explicit-usb','explicit-tcp','auth-guard','fs-fat32','fs-exfat','fs-mixed','fs-refused','input-limits','wire-file-limits']
    for scenario in scenarios:
        case=out/scenario;case.mkdir()
        try:
            run=subprocess.run([str(exe),scenario],cwd=case,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20)
            (case/'test.log').write_bytes(run.stdout);code=run.returncode
        except subprocess.TimeoutExpired as exc:
            (case/'test.log').write_bytes((exc.stdout or b'')+b'\nFAIL bounded host timeout\n');code=-1
        report['results'].append({'scenario':scenario,'status':'PASS' if code==0 else 'FAIL','returncode':code,'log':str(case/'test.log')});save();print(scenario,code,flush=True)
    entry=out/'entry-chain'
    run=subprocess.run([__import__('sys').executable,str(snapshot/'tab5/tests/run_services_startup_guard.py'),'--out',str(entry)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
    (out/'entry-console.log').write_bytes(run.stdout)
    report['results'].append({'scenario':'actual-entry-chain','status':'PASS' if run.returncode==0 else 'FAIL','log':str(out/'entry-console.log')})
    assert all(sha(snapshot/n)==h for n,h in hashes.items()),'snapshot source changed'
    report['status']='PASS' if all(r['status']=='PASS' for r in report['results']) else 'FAIL';save();print(report['status'],out/'SERVICES_GATE.json')
    return 0 if report['status']=='PASS' else 1


if __name__=='__main__':
    raise SystemExit(main())
