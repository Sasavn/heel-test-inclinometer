"""QA R2 schedule against exact service core, before/after mandatory epoch API.
Optional frozen --source-root supports expected-failure reproduction without edits.
"""
from pathlib import Path
import argparse,subprocess,shutil,tempfile
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--source-root',type=Path,default=repo)
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
root=a.source_root.resolve();versioned='uint64_t tab5_services_record_epoch(void)' in (root/'tab5/main/tab5_services.c').read_text(encoding='utf-8')
code='#define main legacy_suite\n#include "'+(root/'tab5/tests/test_services.c').as_posix()+'"\n#undef main\n'
code+=r'''
int main(void){
 _mkdir("test-sd");_mkdir("test-usb");tab5_services_config_t cfg={0};
 CHECK(tab5_services_init(&cfg)==ESP_OK);pump_initial_media();
 CHECK(tab5_services_record_relative_start("session_A")==ESP_OK);
 uint64_t epoch_a=GET_EPOCH;
 tab5_service_sample_t old={.sensor=2,.relative_record=true,.relative_event=true,
  .relative_config={.version=1,.reference_index=0,.object_index=1,.axes_confirmed=1},
  .relative={.ready=false,.quality=TAB5_PAIR_STALE,.generation=7},.relative_observed_us=1200000};
 stop_and_drain();CHECK(tab5_services_record_relative_start("session_B")==ESP_OK);
 int e=SUBMIT_OLD;media_step(0);media_step(1);stop_and_drain();
 fprintf(stderr,"late_session_A_submit_to_B=%d B_relative_rows=%lu\n",e,(unsigned long)media[0].status.relative_rows);
 CHECK(e==ESP_ERR_INVALID_STATE&&media[0].status.relative_rows==0&&media[1].status.relative_rows==0);
 FILE *f=fopen("test-sd/session_B_REL.CSV","r");char line[1024];CHECK(f&&fgets(line,sizeof(line),f));CHECK(!fgets(line,sizeof(line),f));fclose(f);
 puts("PASS QA R2: delayed A epoch rejected under enqueue lock after STOP/drain/START B; no A row or metadata in B");
}
'''
code=code.replace('GET_EPOCH','tab5_services_record_epoch()' if versioned else 'UINT64_C(1)')
code=code.replace('SUBMIT_OLD','tab5_services_submit(&old,epoch_a)' if versioned else '(void)epoch_a,tab5_services_submit(&old)')
# Parenthesized comma expression is needed for the unversioned baseline.
code=code.replace('int e=(void)epoch_a,tab5_services_submit(&old);','int e=((void)epoch_a,tab5_services_submit(&old));')
c=a.out/'epoch.c';c.write_text(code,encoding='utf-8');exe=(a.out/'epoch.exe').resolve()
subprocess.run([shutil.which('gcc'),'--std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'tab5/main'),str(c),'-lbcrypt','-lm','-o',str(exe)],check=True)
with tempfile.TemporaryDirectory(dir=a.out) as temp:
 r=subprocess.run([str(exe)],cwd=temp,capture_output=True)
 for f in Path(temp).rglob('*_REL.CSV'):shutil.copy2(f,a.out/(f.parent.name+'_'+f.name))
(a.out/'test.log').write_bytes(r.stdout+r.stderr);print((r.stdout+r.stderr).decode(errors='replace'));raise SystemExit(r.returncode)
