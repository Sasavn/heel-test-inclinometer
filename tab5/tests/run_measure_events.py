"""Run exact UART worker with injected transactions, including transient error."""
import argparse,pathlib,shutil,subprocess
root=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
source=(root/'tab5/main/tab5_measure.c').read_text();start=source.index('static void measure_task(');i=source.index('{',start)+1;depth=1
while depth:depth+=(source[i]=='{')-(source[i]=='}');i+=1
body=source[start:i]
shim=r'''
#include "tab5_measure.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#define portMAX_DELAY 0
static int mutex;
static tab5_measure_core_t device;
static tab5_measure_sample_cb_t sample_cb;
static void *sample_user;
static uint64_t now=100000;
static unsigned calls,received;
static tab5_measure_sensor_t events[6];
static jmp_buf done;
static uint64_t esp_timer_get_time(void){return now;}
static void xSemaphoreTake(int m,int t){(void)m;(void)t;}
static void xSemaphoreGive(int m){(void)m;}
static void vTaskDelay(unsigned t){(void)t;now+=100000;}
static tab5_measure_error_t transact(unsigned i,uint16_t raw[2],uint64_t *end){
 assert(i==calls%2);now+=33333;*end=now;raw[0]=raw[1]=10000;return calls++==2?TAB5_MEASURE_TIMEOUT:TAB5_MEASURE_OK;
}
static void callback(unsigned i,const tab5_measure_sensor_t *s,void *u){
 (void)u;assert(i==received%2);events[received++]=*s;if(received==6)longjmp(done,1);
}
'''+body+r'''
int main(void){
 tab5_measure_core_init(&device,NULL,0);sample_cb=callback;
 if(!setjmp(done))measure_task(NULL);
 assert(calls==6&&received==6);
 assert(events[2].last_error==TAB5_MEASURE_TIMEOUT&&events[2].fail_streak==1&&events[2].errors==1);
 assert(events[2].good==events[0].good&&events[2].timestamp_us==events[0].timestamp_us);
 assert(events[4].last_error==TAB5_MEASURE_OK&&!events[4].fail_streak&&events[4].errors==1&&events[4].good==2);
 puts("PASS exact measure_task: OK/OK, TIMEOUT/OK, OK/OK callbacks; error original good/RX unchanged; true recovery not lost between reader polls");
}
'''
c=a.out/'worker.c';c.write_text(shim);exe=a.out/'worker.exe'
subprocess.run([shutil.which('gcc') or 'gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fstack-usage','-I'+str(root/'tab5/main'),str(c),str(root/'tab5/main/tab5_measure.c'),'-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True);(a.out/'test.log').write_bytes(r.stdout+r.stderr);print(r.stdout.decode());raise SystemExit(r.returncode)
