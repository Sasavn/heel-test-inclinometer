"""Compile exact main.c settings/command bodies with fake NVS and display; no device."""
from pathlib import Path
import subprocess, tempfile

ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'tab5/main/main.c').read_text(encoding='utf-8')
def body(marker):
    start=source.index('{',source.index(marker)); depth=1; i=start+1
    while depth:
        depth+=(source[i]=='{')-(source[i]=='}'); i+=1
    return source[start+1:i-1]
persist=body('static bool persist(void)')
settings=body('else if(a.kind==TAB5_UI_SETTINGS)')
command=body('else if(strcmp(line,"set recsync 0")')
load_start=source.index('uint32_t timeout=0;')
load=source[load_start:source.index('int16_t offset=',load_start)]
shim=r'''
#include "tab5_ui.h"
#include "tab5_backlight.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int nvs_handle_t;typedef int esp_err_t;
#define ESP_OK 0
#define NVS_READWRITE 1
static bool record_sync,light_theme,ui_result_success;
static uint8_t brightness_pct=60;static uint32_t backlight_timeout_s,measurement_number,ui_result_serial;
static tab5_ui_action_kind_t ui_result_kind;static char notice[160],response[80];
static int network,fail_write,fail_commit,read_missing,committed_sync,staged_sync,committed_timeout,staged_timeout,display_timeout,display_brightness;
static tab5_measure_config_t config={.frequency_hz=10,.gap_ms=15};
void tab5_measure_snapshot(tab5_measure_snapshot_t *s){memset(s,0,sizeof(*s));s->config=config;}
void tab5_measure_configure(const tab5_measure_config_t *s){config=*s;}
static const char *esp_err_to_name(int e){(void)e;return "FAKE_NVS_ERROR";}
static int nvs_open(const char *key,int mode,int *h){(void)key;(void)mode;*h=1;staged_sync=committed_sync;staged_timeout=committed_timeout;return 0;}
static int nvs_set_blob(int h,const char *k,const void *p,size_t n){(void)h;(void)k;(void)p;(void)n;return fail_write;}
static int nvs_set_u8(int h,const char *k,uint8_t v){(void)h;if(!strcmp(k,"rec_sync"))staged_sync=v;return fail_write;}
static int nvs_set_u32(int h,const char *k,uint32_t v){(void)h;if(!strcmp(k,"bl_timeout"))staged_timeout=(int)v;return fail_write;}
static int nvs_commit(int h){(void)h;if(fail_commit)return 1;committed_sync=staged_sync;committed_timeout=staged_timeout;return 0;}
static int nvs_get_u8(int h,const char *k,uint8_t *v){(void)h;(void)k;if(read_missing)return 1;*v=(uint8_t)committed_sync;return 0;}
static int nvs_get_u32(int h,const char *k,uint32_t *v){(void)h;(void)k;if(read_missing)return 1;*v=(uint32_t)committed_timeout;return 0;}
static void nvs_close(int h){(void)h;}
static bool tab5_native_port_backlight_configure(uint32_t timeout,uint8_t brightness){display_timeout=(int)timeout;display_brightness=brightness;return true;}
static void reply(void *ctx,const char *s){(void)ctx;snprintf(response,sizeof(response),"%s",s);}
'''
shim+='static bool persist(void){'+persist+'}\n'
shim+='static void apply(tab5_ui_action_t a){'+settings+'}\n'
shim+='static void wire(const char *line){void *ctx=NULL;'+command+'}\n'
shim+='static void reload(void){nvs_handle_t n=1;'+load+'}\n'
shim+=r'''
int main(void){
 tab5_ui_action_t a={.kind=TAB5_UI_SETTINGS,.config=config,.brightness_pct=80,
 .backlight_timeout_set=true,.backlight_timeout_s=300,.record_sync_set=true,.record_sync=true};
 apply(a);assert(ui_result_success&&record_sync&&committed_sync==1&&committed_timeout==300);
 record_sync=false;backlight_timeout_s=0;reload();assert(record_sync&&backlight_timeout_s==300);
 assert(display_timeout==300&&display_brightness==80);
 a.record_sync=false;a.backlight_timeout_s=1800;a.brightness_pct=40;
 fail_commit=1;apply(a);assert(!ui_result_success&&record_sync&&backlight_timeout_s==300&&brightness_pct==80);
 assert(committed_sync==1&&committed_timeout==300&&display_timeout==300&&display_brightness==80);
 fail_commit=0;fail_write=1;wire("set recsync 0");assert(record_sync&&strstr(response,"ERR"));
 fail_write=0;wire("set recsync 0");assert(!record_sync&&committed_sync==0&&!strcmp(response,"OK recsync"));
 wire("set recsync 1");assert(record_sync&&committed_sync==1);
 a.backlight_timeout_s=123;apply(a);assert(!ui_result_success&&backlight_timeout_s==300);
 a.backlight_timeout_set=false;a.record_sync_set=false;a.brightness_pct=0;
 apply(a);assert(ui_result_success&&record_sync&&backlight_timeout_s==300&&brightness_pct==80);
 a.backlight_timeout_set=true;a.backlight_timeout_s=0;a.record_sync_set=true;a.record_sync=false;
 apply(a);assert(ui_result_success&&!record_sync&&backlight_timeout_s==0&&committed_timeout==0);
 committed_sync=3;committed_timeout=123;record_sync=false;backlight_timeout_s=0;reload();assert(!record_sync&&backlight_timeout_s==0);
 read_missing=1;committed_sync=1;committed_timeout=300;reload();assert(!record_sync&&backlight_timeout_s==0);
 puts("PASS exact main settings+recsync command+boot load: persistence/reboot, rollback, unset markers, explicit false/never, corrupt/missing keys, range, ACK/ERR");
}
'''
out=ROOT/'.tools/task740491';out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=out,prefix='runtime-settings-') as td:
    td=Path(td);c=td/'test.c';c.write_text(shim,encoding='utf-8');exe=td/'test.exe'
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'tab5/main'),str(c),str(ROOT/'tab5/main/tab5_backlight.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
