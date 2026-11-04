"""Compile exact production NVS/control/record bodies with injected boundaries."""
import argparse,pathlib,subprocess,shutil
root=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--out',type=pathlib.Path,default=root/'.tools/dual-runtime');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
source=(root/'tab5/main/main.c').read_text(encoding='utf-8')
def function(marker):
 start=source.index(marker);brace=source.index('{',start);end=brace+1;depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
shim=r'''
#include "tab5_ui.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
typedef int nvs_handle_t;typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define portMAX_DELAY 0
static tab5_relative_t relative_state,route_relative;
static tab5_workspace_t workspace;
static tab5_relative_t relative_session_meta;
static bool relative_session_known;
static char relative_session_name[64];
static bool recording,route_recording,allowed=true,clock_valid=true,missing=true;
static int fail_write,fail_commit,start_error,starts,stops,commits;
static int route_mutex;
static unsigned measurement_number;
static uint64_t record_start_us,route_start_us,route_epoch;
static uint32_t record_duration_s;
static char record_name[40],notice[160];
static tab5_relative_config_t committed,staged;
static tab5_workspace_config_t committed_workspace,staged_workspace;
static bool workspace_missing=true,control_reserved;
static tab5_measure_snapshot_t actual;
static uint64_t now=1020000;
void tab5_measure_snapshot(tab5_measure_snapshot_t *s){*s=actual;}
static int64_t esp_timer_get_time(void){return now;}
static int nvs_open(const char *k,int mode,int *h){(void)k;(void)mode;*h=1;staged=committed;staged_workspace=committed_workspace;return 0;}
static int nvs_get_blob(int h,const char *k,void *out,size_t *size){(void)h;if(!strcmp(k,"workspace")){if(workspace_missing||*size!=sizeof(committed_workspace))return 1;memcpy(out,&committed_workspace,*size);return 0;}if(missing||*size!=sizeof(committed))return 1;memcpy(out,&committed,*size);return 0;}
static int nvs_set_blob(int h,const char *k,const void *v,size_t size){(void)h;if(fail_write)return 1;if(!strcmp(k,"workspace")){assert(size==sizeof(staged_workspace));memcpy(&staged_workspace,v,size);}else{assert(size==sizeof(staged));memcpy(&staged,v,size);}return 0;}
static int nvs_commit(int h){(void)h;if(fail_commit)return 1;committed=staged;committed_workspace=staged_workspace;missing=false;workspace_missing=false;commits++;return 0;}
static void nvs_close(int h){(void)h;}
static bool tab5_services_control_begin(void){if(!allowed||control_reserved)return false;control_reserved=true;return true;}
static void tab5_services_control_end(void){control_reserved=false;}
static bool tab5_board_time_valid(void){return clock_valid;}
static void tab5_native_port_cancel_scroll_benchmark(void){}
static void tab5_services_record_stop(void){stops++;}
static uint64_t tab5_services_record_epoch(void){return 42;}
static int tab5_services_record_relative_start(const char *s){(void)s;starts++;return start_error;}
static int tab5_services_record_start(const char *s){(void)s;starts++;return start_error;}
static void xSemaphoreTake(int s,int t){(void)s;(void)t;}
static void xSemaphoreGive(int s){(void)s;}
static bool persist(void){return true;}
static const char *esp_err_to_name(int e){(void)e;return "test_error";}
static void tab5_time_local_now(time_t t,struct tm *out){*out=*gmtime(&t);}
'''
for marker in ['static void load_workspace_config(void)','static bool save_workspace_config(', 'static void load_relative_config(void)','static bool save_relative_config(', 'static bool apply_dual_action_body(', 'static bool apply_dual_action(', 'static esp_err_t set_recording(']:shim+='\n'+function(marker)+'\n'
shim+=r'''
int main(void){
 load_relative_config();assert(relative_state.mode==TAB5_MODE_HEEL&&!recording&&!relative_state.offset_valid);
 for(unsigned i=0;i<2;i++)actual.sensor[i]=(tab5_measure_sensor_t){.address=i+2,.valid=true,.good=1,.timestamp_us=1000000+i*16000,.decoded_x=(float)i,.decoded_y=-(float)i};
 tab5_ui_action_t a={.kind=TAB5_UI_REL_CONFIG};tab5_relative_config_default(&a.relative_config);
 a.relative_config.axes_confirmed=1;a.relative_config.startup_mode=TAB5_MODE_LEVEL;
 assert(apply_dual_action(&a)&&commits==1&&relative_state.mode==TAB5_MODE_HEEL);
 load_relative_config();assert(relative_state.mode==TAB5_MODE_LEVEL&&!relative_state.offset_valid&&!recording);
 a.kind=TAB5_UI_REL_CAPTURE;assert(apply_dual_action(&a)&&relative_state.offset_valid);
 load_relative_config();assert(!relative_state.offset_valid); /* REL never stored */
 a.kind=TAB5_UI_MODE;a.mode=TAB5_MODE_HEEL;allowed=false;assert(!apply_dual_action(&a)&&relative_state.mode==TAB5_MODE_LEVEL);allowed=true;
 recording=true;assert(!apply_dual_action(&a));recording=false;
 assert(apply_dual_action(&a)&&relative_state.mode==TAB5_MODE_HEEL);
 a.mode=TAB5_MODE_LEVEL;assert(apply_dual_action(&a));
 a.kind=TAB5_UI_REL_CAPTURE;assert(!apply_dual_action(&a));
 actual.sensor[0].good++;assert(!apply_dual_action(&a));actual.sensor[1].good++;assert(apply_dual_action(&a));
 a.kind=TAB5_UI_REL_CONFIG;a.relative_config=relative_state.config;a.relative_config.reference_index=1;a.relative_config.object_index=0;
 fail_commit=1;assert(!apply_dual_action(&a)&&relative_state.config.reference_index==0&&relative_state.offset_valid);fail_commit=0;
 fail_write=1;assert(!apply_dual_action(&a)&&relative_state.config.reference_index==0);fail_write=0;
 assert(apply_dual_action(&a)&&relative_state.config.reference_index==1&&!relative_state.offset_valid);
 a.relative_config.object_index=1;assert(!apply_dual_action(&a));
 assert(set_recording(true)==ESP_ERR_INVALID_STATE&&!recording&&starts==0); /* boundary needs both new */
 actual.sensor[0].good++;actual.sensor[1].good++;clock_valid=false;assert(set_recording(true)!=ESP_OK);clock_valid=true;
 start_error=1;assert(set_recording(true)!=ESP_OK&&!recording&&!route_recording);start_error=0;
 assert(set_recording(true)==ESP_OK&&recording&&route_recording&&route_relative.mode==TAB5_MODE_LEVEL);
 assert(route_epoch==42&&route_start_us==record_start_us&&route_relative.config.reference_index==1);
 assert(relative_session_known&&relative_session_meta.config.reference_index==1&&relative_session_name[0]);
 a.kind=TAB5_UI_REL_RESET;assert(!apply_dual_action(&a));
 assert(set_recording(false)==ESP_OK&&!recording&&!route_recording&&stops==1);
 allowed=false;assert(!apply_dual_action(&a));allowed=true;assert(apply_dual_action(&a));
 assert(relative_session_meta.config.reference_index==1); /* Session provenance survives live controls. */
 assert(!relative_state.offset_valid&&set_recording(true)!=ESP_OK);

 actual.sensor[0].good++;actual.sensor[1].good++;
 a.kind=TAB5_UI_WORKSPACE_CONFIG;tab5_workspace_config_default(&a.workspace_config);
 a.workspace_config.limits_enabled=1;a.workspace_config.limit_deg[0]=.25f;a.workspace_config.filter_enabled=1;
 fail_commit=1;assert(!apply_dual_action(&a)&&workspace.live.config.limit_deg[0]==.1f&&!control_reserved);fail_commit=0;
 fail_write=1;assert(!apply_dual_action(&a)&&!control_reserved);fail_write=0;
 recording=true;assert(!apply_dual_action(&a));recording=false;allowed=false;assert(!apply_dual_action(&a));allowed=true;
 assert(apply_dual_action(&a)&&workspace.live.config.limit_deg[0]==.25f&&!control_reserved);
 load_relative_config();assert(workspace.live.config.limits_enabled&&workspace.live.config.filter_enabled&&!workspace.held);
 a.kind=TAB5_UI_WORKSPACE_HOLD;a.workspace_hold=true;assert(apply_dual_action(&a)&&workspace.held);
 actual.sensor[0].last_error=TAB5_MEASURE_TIMEOUT;assert(!apply_dual_action(&a));
 a.workspace_hold=false;assert(apply_dual_action(&a)&&!workspace.held);
 a.kind=TAB5_UI_WORKSPACE_CLEAR;recording=true;assert(!apply_dual_action(&a));recording=false;assert(apply_dual_action(&a));
 committed.version=99;load_relative_config();assert(relative_state.mode==TAB5_MODE_HEEL&&!recording);
 puts("PASS exact main dual control: NVS default/corrupt/reboot/commit/write rollback; independent startup/active mode; role validity; fresh boundaries; REL volatile; record/close/OTA guard; no automatic start; real record route; workspace NVS rollback/reboot, HOLD live error, atomic reservation/release");
}
'''
c=a.out/'runtime-shim.c';c.write_text(shim,encoding='utf-8');exe=(a.out/'runtime-test.exe').resolve()
subprocess.run([shutil.which('gcc') or 'gcc','-std=c11','-Wall','-Wextra','-Werror','-O2','-I'+str(root/'tab5/main'),str(c),str(root/'tab5/main/tab5_relative.c'),str(root/'tab5/main/tab5_workspace.c'),'-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True);(a.out/'test.log').write_text(r.stdout+r.stderr,encoding='utf-8');print(r.stdout+r.stderr,end='');raise SystemExit(r.returncode)
