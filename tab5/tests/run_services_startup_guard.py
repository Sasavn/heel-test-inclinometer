"""Actual app_main startup chain + source safety/configuration guards; host only."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def mask_c(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', lambda m: ' '*len(m[0]), text, flags=re.S)


def function(text, name):
    masked = mask_c(text)
    match = re.search(r'\b'+re.escape(name)+r'\s*\([^;{}]*\)\s*\{', masked)
    assert match, f"Missing actual production function {name}"
    start = masked.index('{', match.start()); depth = 1; end = start+1
    while depth:
        depth += (masked[end] == '{')-(masked[end] == '}'); end += 1
    return text[start+1:end-1]


def source_guards(root):
    source = (root/'tab5/main/main.c').read_text(encoding='utf-8')
    app = function(source, 'app_main'); code = mask_c(app)
    prefix_end = code.index('bool trial_confirmed')
    prefix = app[:prefix_end]
    assert code.index('tab5_native_port_start') < code.index('tab5_services_init') < code.index('xTaskCreate'), 'actual UI/init/consumer order'
    assert not re.search(r'\b(?:printf|puts|fprintf|ESP_LOG\w*)\s*\(', mask_c(prefix)), 'blocking startup console log'
    usb = function(source, 'usb_task')
    assert re.search(r'tab5_services_line\s*\(\s*1\s*,\s*line\s*,\s*true', usb), 'USB-C explicit OTA dispatcher'
    assert 'usb_serial_jtag_is_connected' in usb and 'usb_serial_jtag_wait_tx_done' in usb
    services = (root/'tab5/main/tab5_services.c').read_text(encoding='utf-8')
    allocation=re.sub(r'\s+','',function(services,'clients_storage_init'))
    assert 'heap_caps_calloc(CLIENTS,sizeof(*clients),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)' in allocation, 'actual ESP allocation must require byte-accessible PSRAM'
    assert 'if(!clients)returnfalse;' in allocation, 'allocation failure must precede client dereference'
    tcp=function(services,'network_task')
    assert re.search(r'if\s*\(\s*!c->auth\s*\)',mask_c(tcp)) and 'tab5_services_line(c->owner,c->rx,true' in tcp, 'TCP commands enter only the explicit authenticated dispatch branch'
    init = mask_c(function(services, 'tab5_services_init'))
    assert not re.search(r'\b(?:tab5_services_retry|retry_medium|mount_sd|startup_usb|bsp_sdcard_mount)\s*\(', init), 'no init-caller physical media probe'
    assert '.format_if_mount_failed=false' in re.sub(r'\s+', '', function(services, 'mount_sd'))
    assert not re.search(r'format_if_mount_failed\s*=\s*true|\b(?:f_mkfs|esp_vfs_fat_sdcard_format|esp_vfs_fat_sdmmc_format)\s*\(', mask_c(services)), 'no automatic formatting'
    assert 'storage_worker_step' in mask_c(function(services,'media_task')), 'ESP task must call the shared tested worker'
    assert 'maintenance' in mask_c(function(services,'service_task')), 'OTA maintenance independent of slow media workers'
    status=function(source,'status')
    assert '"sd_known"' in status and '"sd_fs"' in status and '"known",usb.known' in status and '"fs",filesystem_name(usb.fs_type)' in status and '"known",net.known' in status, 'actual additive JSON known/fs source chain'
    defaults = (root/'tab5/sdkconfig.defaults').read_text(encoding='utf-8')
    assert 'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y' in defaults, 'actual USB-C console defaults'
    return prefix


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT/'.tools/native-1.6.15/startup-guard')
    args = parser.parse_args(); out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    relevant = [ROOT/'tab5/main/main.c', ROOT/'tab5/main/tab5_services.c', ROOT/'tab5/main/tab5_services.h', ROOT/'tab5/sdkconfig.defaults', ROOT/'tab5/tests/test_services_backend.h', ROOT/'tab5/tests/test_services_entry_chain.c', ROOT/'tab5/tests/test_services_ui_consumer.c', ROOT/'tab5/tests/test_services_sd_deadline.c', ROOT/'tab5/tests/test_services.c']
    hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in relevant}
    report={'status':'RUNNING','source_sha256':hashes}
    try:
        prefix=source_guards(ROOT)
        (out/'startup_entry_generated.h').write_text('/* Exact production app_main prefix, renamed only. */\nstatic void run_actual_app_startup(void){\n'+prefix+'\n}\n',encoding='utf-8')
        source=(ROOT/'tab5/main/main.c').read_text(encoding='utf-8');app=function(source,'app_main')
        start=app.index('tab5_medium_status_t sd,flash;');end=app.index('xSemaphoreGive(app_mutex);',start)+len('xSemaphoreGive(app_mutex);')
        consumer='static const char *filesystem_name(tab5_fs_type_t type){'+function(source,'filesystem_name')+'}\nstatic void apply_actual_main_snapshots(tab5_ui_state_t *target){tab5_ui_state_t ui=*target;\n'+app[start:end]+'\n*target=ui;}\n'
        (out/'services_consumer_generated.h').write_text(consumer,encoding='utf-8')
        services=(ROOT/'tab5/main/tab5_services.c').read_text(encoding='utf-8')
        constants='\n'.join(re.findall(r'^#define\s+SD_(?:COMMAND|MOUNT)_MS\s+\d+.*$',services,re.M))
        wire=function(services,'file_wire_size')
        assert 'f_stat(path,&info)' in wire and 'raw=(uint64_t)info.fsize' in wire and 'raw>UINT32_MAX' in wire, 'FatFS original64bit metadata guarded before FILE/VFS narrowing'
        deadline=constants+'\nstatic esp_err_t sd_bounded_transaction(int slot,sdmmc_command_t *cmd){'+function(services,'sd_bounded_transaction')+'}\n'
        (out/'services_deadline_generated.h').write_text(deadline,encoding='utf-8')
        cc=shutil.which('gcc');assert cc,'gcc required';report['results']=[]
        for name,test in [('entry-chain','test_services_entry_chain'),('ui-consumer','test_services_ui_consumer'),('sd-deadline','test_services_sd_deadline')]:
            exe=out/(name+'.exe')
            built=subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-O2','-I'+str(ROOT/'tab5/main'),'-I'+str(out),str(ROOT/'tab5/tests'/(test+'.c')),'-lbcrypt','-lm','-o',str(exe)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (out/(name+'-build.log')).write_bytes(built.stdout)
            if built.returncode:
                report['results'].append({'case':name,'status':'FAIL','reason':'build'});continue
            run=subprocess.run([str(exe)],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
            (out/(name+'.log')).write_bytes(run.stdout);report['results'].append({'case':name,'status':'PASS' if run.returncode==0 else 'FAIL','returncode':run.returncode})
        assert all(hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==h for n,h in hashes.items()),'source changed during test'
        report['status']='PASS' if all(r['status']=='PASS' for r in report['results']) else 'FAIL'
    except (AssertionError, ValueError) as exc:
        report.update(status='FAIL',reason=str(exc))
    (out/'STARTUP_GATE.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report,indent=2))
    return 0 if report['status']=='PASS' else 1


if __name__=='__main__':
    raise SystemExit(main())
