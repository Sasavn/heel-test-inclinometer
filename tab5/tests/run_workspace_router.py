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
typedef struct {unsigned index;tab5_measure_sensor_t value;uint64_t observed_us;} sample_event_t;
static int storage_samples,route_mutex,tick;
static bool route_recording=true;
static uint64_t route_start_us=900000,route_epoch=42,now=1020000;
static float route_battery=12;
static uint32_t storage_drops;
static tab5_relative_t route_relative;
static tab5_measure_snapshot_t health;
static tab5_service_sample_t rows[64];static uint64_t epochs[64];static unsigned count;
static jmp_buf finish;
static void event(sample_event_t *e,unsigned i,unsigned seq,uint64_t stamp,float x){
 e->index=i;e->value=(tab5_measure_sensor_t){.address=i+2,.valid=true,.good=seq,.timestamp_us=stamp,.decoded_x=x,.decoded_y=-(float)i};
}
static int xQueueReceive(int q,sample_event_t *e,unsigned timeout){
 (void)q;assert(timeout==100);
 switch(tick++){
 case 0:event(e,0,99,800000,88);return 1; /* pre-session callback, not a basis */
 case 1:now=1046666;event(e,0,1,1000000,.25f);return 1;
 case 2:event(e,1,1,1033333,1.25f);return 1;
 case 3:now=1113333;event(e,0,2,1100000,10);return 1;
 case 4:now=1146666;event(e,1,2,1133333,11);return 1;
 case 5:now=1150000;event(e,0,2,1100000,10);e->value.last_error=TAB5_MEASURE_TIMEOUT;e->value.fail_streak=1;e->observed_us=now;return 1; /* transient fault, current health already healthy */
 case 6:now=1151000;return 0; /* same quality, no duplicate gap */
 case 7:now=1160000;health.sensor[0].last_error=TAB5_MEASURE_OK;health.sensor[0].fail_streak=0;return 0; /* health-only recovery must not import seq99 */
 case 8:now=1213333;event(e,0,3,1200000,20);return 1;
 case 9:now=1246666;event(e,1,3,1233333,21);return 1;
 case 10:now=1700001;return 0; /* real accepted pair age 500001 */
 case 11:now=1800000;event(e,0,4,1800000,25);return 1;
 case 12:now=1860001;event(e,1,4,1860001,26);return 1; /* completed bad skew stays explicit */
 case 13:route_start_us=2000000;route_epoch=43;now=2040000;return 0;
 case 14:event(e,1,4,1860001,26);return 1; /* old epoch callback cannot leak */
 case 15:now=2146666;event(e,0,5,2100000,30);return 1;
 case 16:event(e,1,5,2133333,31);return 1;
 case 17:route_recording=false;event(e,0,6,2200000,40);return 1;
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
esp_err_t tab5_services_submit(const tab5_service_sample_t *s,uint64_t epoch){assert(epoch==42||epoch==43);assert(count<64);epochs[count]=epoch;rows[count++]=*s;return 0;}
'''
shim+=body+r'''
int main(void){
 tab5_relative_config_t cfg;tab5_relative_config_default(&cfg);cfg.axes_confirmed=1;cfg.startup_mode=TAB5_MODE_LEVEL;tab5_relative_init(&route_relative,&cfg);
 for(unsigned i=0;i<2;i++)health.sensor[i]=(tab5_measure_sensor_t){.address=i+2,.valid=true,.good=99,.timestamp_us=3000000+i*16000,.decoded_x=88,.decoded_y=88};
 /* An already populated live route copy must be reset for EVERY epoch. */
 tab5_relative_result_t published;tab5_relative_observe(&route_relative,&health,3020000,&published);assert(published.ready);
 if(!setjmp(finish))storage_router_task(NULL);
 unsigned raw=0,data=0,errors=0,stale=0,skew=0,epoch_data=0;uint64_t last_seq=0;
 for(unsigned i=0;i<count;i++){
  tab5_service_sample_t *r=&rows[i];assert(r->relative.sequence[0]<=5&&r->relative.sequence[1]<=5);
  if(!r->relative_event){raw++;assert(r->ms>=900);
   if(!r->relative_raw_only){data++;assert(r->relative.ready&&r->relative.sequence[0]>last_seq);last_seq=r->relative.sequence[0];
    assert(r->relative.sequence[0]==r->relative.sequence[1]);assert(r->relative.live[0]==1);
    if(epochs[i]==43){epoch_data++;assert(last_seq==5&&r->relative.timestamp_us[0]==2100000);}
   }
  }else{errors+=(r->relative.quality&TAB5_PAIR_ERROR)!=0;stale+=(r->relative.quality&TAB5_PAIR_STALE)!=0;skew+=r->relative.quality==TAB5_PAIR_SKEW;}
 }
 assert(raw==10&&data==4&&epoch_data==1&&errors==1&&stale>=1&&skew==1&&!storage_drops);
 assert(!route_recording&&tick==19);
 puts("PASS exact production router: 10 original raw rows, 4 unique accepted REL SAMPLE, explicit real ERROR/STALE/completed SKEW, health seq99 never imported, no false mixed gap, epoch cache reset/pre-session rejection, STOP cutoff");
}
'''
c=a.out/'router.c';c.write_text(shim,encoding='utf-8');exe=(a.out/'router.exe').resolve()
subprocess.run([shutil.which('gcc') or 'gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'tab5/main'),str(c),str(root/'tab5/main/tab5_relative.c'),'-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True);(a.out/'test.log').write_text(r.stdout+r.stderr,encoding='utf-8');print(r.stdout+r.stderr,end='');raise SystemExit(r.returncode)
