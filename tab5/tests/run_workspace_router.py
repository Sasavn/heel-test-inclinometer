"""Exact production storage router: silence watchdog, audit events, STOP boundary.

OS queue/clock/measurement/submit boundaries injected; does not use a device.
"""
import argparse,pathlib,shutil,subprocess
root=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--out',type=pathlib.Path,default=root/'.tools/workspace-router');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
text=(root/'tab5/main/main.c').read_text(encoding='utf-8')
start=text.index('static void storage_router_task(');i=text.index('{',start)+1;depth=1
while depth:
 depth+=(text[i]=='{')-(text[i]=='}');i+=1
body=text[start:i]
shim=r'''
#include "tab5_services.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define pdTRUE 1
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
typedef struct {unsigned index;tab5_measure_sensor_t value;} sample_event_t;
static int storage_samples,route_mutex,tick;
static bool route_recording=true;
static uint64_t route_start_us=900000,route_epoch=42,now=1020000;
static float route_battery=12;
static uint32_t storage_drops;
static tab5_relative_t route_relative;
static tab5_measure_snapshot_t health;
static tab5_service_sample_t rows[16];static unsigned count;
static jmp_buf finish;
static int xQueueReceive(int q,sample_event_t *e,unsigned timeout){
 (void)q;assert(timeout==100);
 switch(tick++){
 case 0:return 0; /* ready observation, no raw callback */
 case 1:now+=100000;health.sensor[0].good++;health.sensor[0].timestamp_us=now-20000;health.sensor[1].good++;health.sensor[1].timestamp_us=now-4000;e->index=0;e->value=health.sensor[0];return 1;
 case 2:now+=100000;health.sensor[0].last_error=TAB5_MEASURE_TIMEOUT;health.sensor[0].fail_streak=1;return 0;
 case 3:now+=100000;return 0; /* same quality: no repeated gap */
 case 4:now+=600000;health.sensor[0].valid=health.sensor[1].valid=false;return 0; /* both silent */
 case 5:now+=100000;for(unsigned i=0;i<2;i++){health.sensor[i].good++;health.sensor[i].timestamp_us=now-16000+i*16000;health.sensor[i].valid=true;health.sensor[i].last_error=TAB5_MEASURE_OK;health.sensor[i].fail_streak=0;}return 0;
 case 6:route_recording=false;now+=100000;e->index=1;e->value=health.sensor[1];return 1;
 default:longjmp(finish,1);
 }
}
static void xSemaphoreTake(int m,int wait){(void)m;(void)wait;}
static void xSemaphoreGive(int m){(void)m;}
static uint64_t esp_timer_get_time(void){return now;}
void tab5_measure_snapshot(tab5_measure_snapshot_t *out){*out=health;}
static tab5_service_sample_t make_row(const sample_event_t *e,float voltage){
 return (tab5_service_sample_t){.sensor=e->index+2,.ms=(uint32_t)(e->value.timestamp_us/1000),.raw_x=e->value.decoded_x,.raw_y=e->value.decoded_y,.battery=voltage};
}
esp_err_t tab5_services_submit(const tab5_service_sample_t *s,uint64_t epoch){assert(epoch==42);assert(count<16);rows[count++]=*s;return 0;}
'''
shim+=body+r'''
int main(void){
 tab5_relative_config_t cfg;tab5_relative_config_default(&cfg);cfg.axes_confirmed=1;cfg.startup_mode=TAB5_MODE_LEVEL;tab5_relative_init(&route_relative,&cfg);
 for(unsigned i=0;i<2;i++)health.sensor[i]=(tab5_measure_sensor_t){.address=i+2,.valid=true,.good=10,.timestamp_us=1000000+i*16000,.decoded_x=(float)i,.decoded_y=-(float)i};
 if(!setjmp(finish))storage_router_task(NULL);
 assert(count==5&&!storage_drops);
 assert(rows[0].relative_event&&rows[0].relative.ready&&rows[0].relative_observed_us==1020000);
 assert(!rows[1].relative_event&&rows[1].sensor==2&&rows[1].relative_record);
 assert(rows[2].relative_event&&!rows[2].relative.ready&&(rows[2].relative.quality&TAB5_PAIR_ERROR));
 assert(rows[3].relative_event&&!rows[3].relative.ready&&(rows[3].relative.quality&TAB5_PAIR_STALE));
 assert(rows[2].relative.timestamp_us[1]==1116000&&rows[3].relative.timestamp_us[1]==1116000);
 assert(rows[2].relative.sequence[1]==rows[3].relative.sequence[1]); /* observer never invents replies */
 assert(rows[4].relative_event&&rows[4].relative.ready&&rows[4].relative_observed_us==2020000);
 assert(!route_recording&&tick==8); /* STOP accepts no following callback */
 puts("PASS exact production router: 100ms watchdog, ready/GAP/both-silent/stale/RECOVERY, no repeated same-quality events, RX provenance unchanged, no fake raw callback, STOP cutoff");
}
'''
c=a.out/'router.c';c.write_text(shim,encoding='utf-8');exe=(a.out/'router.exe').resolve()
subprocess.run([shutil.which('gcc') or 'gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'tab5/main'),str(c),str(root/'tab5/main/tab5_relative.c'),'-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True);(a.out/'test.log').write_text(r.stdout+r.stderr,encoding='utf-8');print(r.stdout+r.stderr,end='');raise SystemExit(r.returncode)
