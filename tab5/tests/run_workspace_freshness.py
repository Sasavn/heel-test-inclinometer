"""Exact production main observation fragment, UART reply during snapshot.
No device, OS clock change, UI window, or rewritten measurement algorithm.
"""
from pathlib import Path
import argparse,subprocess,shutil
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
p.add_argument('--main-source',type=Path,default=root/'tab5/main/main.c',help='Optional frozen baseline source for expected-failure reproduction')
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
source=a.main_source.read_text(encoding='utf-8')
start=source.index('static tab5_ui_state_t ui;');end=source.index('/* Coherent live publication',start)
fragment=source[start:end]
shim=r'''
#include "tab5_ui.h"
#include <assert.h>
#include <stdio.h>
static tab5_workspace_t workspace;
static tab5_relative_t relative_state;
static tab5_measure_snapshot_t actual;
static bool recording,light_theme,record_sync,reply_during_snapshot;
static uint8_t brightness_pct=60;
static uint32_t backlight_timeout_s;
static uint64_t clock_us=1020000;
static void tab5_measure_snapshot(tab5_measure_snapshot_t *out){
 if(reply_during_snapshot){clock_us+=34001;actual.sensor[0].timestamp_us=clock_us;actual.sensor[0].good++;}
 *out=actual;
}
static uint64_t esp_timer_get_time(void){return clock_us;}
static bool tab5_services_mode_change_allowed(void){return true;}
static void production_cycle(void){uint64_t now=clock_us;
'''+fragment+r'''
 fprintf(stderr,"observation now=%llu clock=%llu quality=%u gaps=%llu valid=%u\n",(unsigned long long)now,(unsigned long long)clock_us,ui.relative.quality,(unsigned long long)workspace.live.live_gaps,ui.workspace.values_valid);
 assert(now==clock_us&&ui.relative.ready&&ui.relative.quality==0);
 assert(ui.workspace.live_gaps==0&&ui.workspace.values_valid);
}
int main(void){
 tab5_relative_config_t roles;tab5_relative_config_default(&roles);roles.axes_confirmed=1;roles.startup_mode=TAB5_MODE_LEVEL;
 tab5_relative_init(&relative_state,&roles);tab5_workspace_init(&workspace,NULL);
 for(unsigned i=0;i<2;i++)actual.sensor[i]=(tab5_measure_sensor_t){.address=i+2,.valid=true,.good=1,.timestamp_us=1000000+i*16000,.decoded_x=(float)i,.decoded_y=-(float)i};
 tab5_relative_result_t pair;tab5_relative_observe(&relative_state,&actual,clock_us,&pair);
 production_cycle();reply_during_snapshot=true;production_cycle();
 assert(workspace.live.live_pairs==1); /* Only ref advanced: no invented pair. */
 printf("PASS exact main observation fragment: clock AFTER snapshot, concurrent reply RX skew=%llu us, no false STALE/GAP/value reset, no duplicate history\n",(unsigned long long)(actual.sensor[0].timestamp_us-actual.sensor[1].timestamp_us));
}
'''
c=a.out/'freshness.c';c.write_text(shim,encoding='utf-8');exe=(a.out/'freshness.exe').resolve()
subprocess.run([shutil.which('gcc'),'--std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'tab5/main'),str(c),str(root/'tab5/main/tab5_relative.c'),str(root/'tab5/main/tab5_workspace.c'),'-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True);(a.out/'test.log').write_bytes(r.stdout+r.stderr);print(r.stdout.decode(errors='replace'));raise SystemExit(r.returncode)
