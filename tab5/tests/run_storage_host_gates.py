"""Frozen host-only storage1.6.15 gates. No native UI rebuild/device/COM/GUI.

Fails closed before compiling if services and the tracked MSC getter disagree.
Run again with a fresh --out after the service owner completes alignment.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from run_services_startup_guard import function

ROOT=Path(__file__).resolve().parents[2]
MSC=Path('tab5/components/usb_host_msc')
UPSTREAM=MSC/'upstream'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def interface(root):
    header=root/MSC/'include/usb/msc_host_vfs_drive.h'
    declaration=header.read_text(encoding='utf-8')
    api=re.search(r'esp_err_t\s+(msc_host_vfs_get_\w+)\s*\(',declaration)
    assert api,'Missing tracked MSC getter declaration'
    api=api[1]
    source=(root/'tab5/main/tab5_services.c').read_text(encoding='utf-8')
    body=function(source,'mount_medium')
    wrapper=(root/MSC/'include/usb/msc_host_vfs.h').read_text(encoding='utf-8')
    assert ('#include "usb/msc_host_vfs_drive.h"' in source or
            '#include "usb/msc_host_vfs.h"' in source and '#include "usb/msc_host_vfs_drive.h"' in wrapper),'Services public include chain must expose tracked getter'
    assert api+'(' in body,f'Services does not call declared getter {api}'
    assert body.index(api+'(')>body.index('msc_host_vfs_register('),'Getter must follow successful registration'
    assert 'ff_diskio_get_drive(' not in body,'No free-slot guess for an existing VFS registration'
    assert body.index('supported_drive(')>body.index(api+'('),'Actual assigned drive must feed FS policy'
    assert 'msc_host_vfs_unregister' in body,'Getter/policy failure cleanup required'
    function((root/MSC/'msc_vfs_owned.c').read_text(encoding='utf-8'),api)
    return {'status':'PASS','getter':api,'header_sha256':sha(header),'services_sha256':sha(root/'tab5/main/tab5_services.c')}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path)
    args=parser.parse_args()
    out=args.out.resolve() if args.out else ROOT/'.tools/storage-1.6.15'/('final-hostgates-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    if out.exists() and any(out.iterdir()):parser.error('Use fresh output directory')
    out.mkdir(parents=True)
    report={'status':'RUNNING','scope':'host source-chain/I-O-boundary tests; no native heavy tests or devices','results':[]}
    def save():(out/'HOST_GATES.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    try:
        report['msc_interface']=interface(ROOT)
    except (AssertionError,OSError) as exc:
        report.update(status='WAITING_INTERFACE_FREEZE',reason=str(exc));save();print(json.dumps(report,indent=2));return 2
    files=list((ROOT/'tab5/main').glob('*.h'))
    files += [ROOT/n for n in ('tab5/main/main.c','tab5/main/tab5_services.c','tab5/CMakeLists.txt','tab5/sdkconfig.defaults','tab5/sdkconfig','tab5/components/tab5_board_support/src/bsp_usb.c','.tools/esp-idf/components/soc/esp32p4/usb_dwc_periph.c')]
    files += list((ROOT/'tab5/tests').glob('test_services*'))
    files += [ROOT/'tab5/tests'/n for n in ('test_retry_backoff.c','run_services_deferred.py','run_services_startup_guard.py','test_bsp_usb_start.py','test_fatfs_overlay.py','test_msc_drive_getter.py','run_storage_host_gates.py')]
    files += [p for folder in (ROOT/MSC,ROOT/'tab5/components/fatfs') for p in folder.rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    pins=json.loads((ROOT/MSC/'upstream.json').read_text())
    files += [ROOT/UPSTREAM/n for n in pins['files']]
    ff_pins=json.loads((ROOT/'tab5/components/fatfs/upstream.json').read_text())
    files += [ROOT/'.tools/esp-idf/components/fatfs'/n for n in ff_pins['sha256']]
    files=sorted(set(p for p in files if p.is_file()))
    hashes={str(p.relative_to(ROOT)):sha(p) for p in files}
    snapshot=out/'source'
    for p in files:
        dest=snapshot/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest);dest.chmod(0o444)
    assert all(sha(p)==hashes[str(p.relative_to(ROOT))] for p in files),'Source race during snapshot'
    (out/'SOURCE_MANIFEST.json').write_text(json.dumps(hashes,indent=2))
    report.update(source_manifest=str(out/'SOURCE_MANIFEST.json'),services_sha256=sha(snapshot/'tab5/main/tab5_services.c'),services_header_sha256=sha(snapshot/'tab5/main/tab5_services.h'),main_sha256=sha(snapshot/'tab5/main/main.c'),msc_interface=interface(snapshot));save()
    commands=[
        ('services-deferred-entry-consumer-deadline',[sys.executable,str(snapshot/'tab5/tests/run_services_deferred.py'),'--out',str(out/'services')]),
        ('legacy-services-startup-retry-backoff',[sys.executable,str(snapshot/'tab5/tests/test_services_run.py')]),
        ('actual-bsp-usb-start',[sys.executable,str(snapshot/'tab5/tests/test_bsp_usb_start.py')]),
        ('fatfs-overlay-pin-generation-sdk-unchanged',[sys.executable,str(snapshot/'tab5/tests/test_fatfs_overlay.py')]),
        ('actual-msc-register-getter-unregister',[sys.executable,str(snapshot/'tab5/tests/test_msc_drive_getter.py')]),
    ]
    for name,command in commands:
        log=out/(name+'.log')
        try:
            run=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
            log.write_bytes(run.stdout);code=run.returncode
        except subprocess.TimeoutExpired as exc:
            log.write_bytes((exc.stdout or b'')+b'\nFAIL host deadline\n');code=-1
        report['results'].append({'gate':name,'status':'PASS' if code==0 else 'FAIL','returncode':code,'log':str(log)});save();print(name,code,flush=True)
    report['source_snapshot_unchanged']=all(sha(snapshot/n)==h for n,h in hashes.items())
    report['workspace_matches_snapshot']=all(sha(ROOT/n)==h for n,h in hashes.items())
    report['status']='PASS' if all(r['status']=='PASS' for r in report['results']) and report['source_snapshot_unchanged'] and report['workspace_matches_snapshot'] else 'FAIL'
    services_report=out/'services/SERVICES_GATE.json'
    if services_report.exists():report['services_details']=json.loads(services_report.read_text())
    fs_proof=ROOT/'.tools/storage-1.6.15/fatfs-images/proof.json'
    if fs_proof.exists():
        fs=json.loads(fs_proof.read_text());report['fatfs_image_external']={'owner':'Godel','path':str(fs_proof),'sha256':sha(fs_proof),'passed':fs.get('passed'),'configuration_mode':fs.get('configuration_mode'),'scope':'Worker actual pinned FatFS RAM-image run, not repeated by this runner'}
        shutil.copy2(fs_proof,out/'GODEL_FATFS_IMAGE_PROOF.json')
    report['cpu_jobs_complete']=True;save()
    index={str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file() and p.suffix not in ('.exe','.o') and '__pycache__' not in p.parts and 'output_services' not in p.parts}
    (out/'ARTIFACT_SHA256.json').write_text(json.dumps(index,indent=2,sort_keys=True))
    print(report['status'],out/'HOST_GATES.json')
    return 0 if report['status']=='PASS' else 1


if __name__=='__main__':
    raise SystemExit(main())
