"""Compile production adapter with small ESP-IDF fakes, execute real poll task."""
import pathlib
import json
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / 'tab5/components/tab5_input'
COMMON = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
typedef void *QueueHandle_t;
typedef unsigned TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
extern int test_lock_depth;
#define portENTER_CRITICAL(m) ((void)(m), test_lock_depth++)
#define portEXIT_CRITICAL(m) ((void)(m), test_lock_depth--)
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NOT_SUPPORTED 4
#define pdTRUE 1
#define pdPASS 1
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
typedef struct { uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type; } gpio_config_t;
int gpio_get_level(int);
int gpio_config(const gpio_config_t *);
int gpio_reset_pin(int);
int64_t esp_timer_get_time(void);
int xQueueSend(QueueHandle_t,const void *,unsigned);
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void *);
void vTaskDelete(void *);
void vTaskDelay(unsigned);
'''
HARNESS = r'''
#include <assert.h>
#include <setjmp.h>
#include <string.h>
#include "tab5_input_gpio.h"
#include "driver/gpio.h"
int test_lock_depth;
static unsigned tick, count, configured, resets, rejected, reads, detents;
static bool fail_task;
static void (*task)(void *);
static struct { int kind; bool level; uint32_t ms; } events[16];
static jmp_buf done;
int gpio_config(const gpio_config_t *g) {
 assert(test_lock_depth==0);
 assert(g->pin_bit_mask==((1ULL<<2)|(1ULL<<3)|(1ULL<<4)|(1ULL<<48)));
 assert(g->mode==GPIO_MODE_INPUT && g->intr_type==GPIO_INTR_DISABLE);
 configured++; return ESP_OK;
}
int gpio_reset_pin(int p) { (void)p; resets++; return 0; }
int gpio_get_level(int p) {
 assert(test_lock_depth==0);reads++;
 if(p==2) return tick<5?0:tick<40?1:0; /* boot ON -> OFF -> ON */
 if(p==48) return tick<70?1:tick<100?0:1;
 unsigned ab=3;
 if(tick>=5 && tick<10)ab=2;else if(tick<15 && tick>=10)ab=0;
 else if(tick>=15 && tick<20)ab=1;
 else if(tick>=45 && tick<50)ab=1;else if(tick>=50 && tick<55)ab=0;
 else if(tick>=55 && tick<60)ab=2;
 else if(tick>=110 && tick<115)ab=0; /* invalid diagonal, no detent */
 return p==3?(int)(ab>>1):(int)(ab&1);
}
int64_t esp_timer_get_time(void) { return (int64_t)tick*1000; }
static bool level(int kind,bool value,uint32_t ms) {
 assert(test_lock_depth==0);
 tab5_gpio_status s;unsigned old_reads=reads,old_configured=configured;
 tab5_input_gpio_snapshot(&s);
 assert(reads==old_reads && configured==old_configured && s.sampled_ms==tick && test_lock_depth==0);
 /* Raw observation continues while the rejected REC event retains its time;
  * PRESS control debounce still starts only when backpressure clears. */
 if(kind==0 && value && tick==70)assert(ms==60 && s.raw[2]==0 && s.raw_changes[2]==1 && s.press_down==0);
 /* Reject each record transition twice; original timestamps must survive. */
 if(kind==0 && rejected<2) { rejected++; return false; }
 events[count].kind=kind; events[count].level=value; events[count++].ms=ms;
 rejected=0; return true;
}
static bool record(bool on,uint32_t ms) { return level(0,on,ms); }
static bool button(bool down,uint32_t ms) { return level(1,down,ms); }
static bool detent(int delta,uint32_t ms) {
 assert(test_lock_depth==0);
 assert((detents==0 && delta==1 && ms==20) || (detents==1 && delta==-1 && ms==60));
 detents++;return true;
}
int xTaskCreate(void (*fn)(void *),const char *n,unsigned stack,void *arg,unsigned pri,void *out) {
 (void)n;(void)stack;(void)arg;(void)pri;(void)out;
 task=fn; return fail_task?0:pdPASS;
}
void vTaskDelete(void *p) { (void)p; longjmp(done,2); }
void vTaskDelay(unsigned n) { if(tick==125)n+=5;tick+=n; if(tick>=140) longjmp(done,1); }
int main(void) {
 tab5_gpio_config c={3,4,48,2,20,5};
 tab5_gpio_callbacks cb={detent,button,record};
 tab5_gpio_status s,again;
 tab5_input_gpio_snapshot(NULL);tab5_input_gpio_snapshot(&s);
 assert(!s.initialized && !s.sampled && s.raw[0]==-1 && reads==0 && configured==0);
#if CONFIG_TAB5_INPUT_GPIO_ENABLE
 assert(s.enabled);
 assert(tab5_input_gpio_start(NULL,&cb)==ESP_ERR_INVALID_ARG);
 for(int p=0;p<55;p++) if(p!=2 && p!=3 && p!=4 && p!=48) {
  c.press=p; assert(tab5_input_gpio_start(&c,&cb)==ESP_ERR_INVALID_ARG);
 }
 c.press=3; assert(tab5_input_gpio_start(&c,&cb)==ESP_ERR_INVALID_ARG);
 c.press=48;
 fail_task=true;
 assert(tab5_input_gpio_start(&c,&cb)==ESP_ERR_NO_MEM && resets==4);
 fail_task=false;
 assert(tab5_input_gpio_start(&c,&cb)==ESP_OK);
 tab5_input_gpio_snapshot(&s);
 assert(s.initialized && !s.sampled && reads==0 && s.config.a==3 && s.config.record==2);
 assert(tab5_input_gpio_start(&c,&cb)==ESP_ERR_INVALID_STATE);
 if(setjmp(done)==0) task(NULL);
 assert(count==4);
 assert(events[0].kind==0 && !events[0].level && events[0].ms==25);
 assert(events[1].kind==0 && events[1].level && events[1].ms==60);
 /* Sampling resumes after backpressure; no accepted command is repeated. */
 assert(events[2].kind==1 && events[2].level && events[2].ms==95);
 assert(events[3].kind==1 && !events[3].level && events[3].ms==120);
 assert(tab5_input_gpio_retries()==4);
 tab5_input_gpio_snapshot(&s);tab5_input_gpio_snapshot(&again);
 assert(memcmp(&s,&again,sizeof(s))==0 && test_lock_depth==0);
 assert(s.samples==28 && reads==s.samples*4 && s.sampled_ms==135);
 assert(s.poll_gap_ms==10 && s.poll_gap_max_ms==10);
 assert(s.raw[0]==1 && s.raw[1]==1 && s.raw[2]==1 && s.raw[3]==0);
 assert(s.raw_changes[0]==6 && s.raw_changes[1]==6 && s.raw_changes[2]==2 && s.raw_changes[3]==2);
 assert(s.ab_changes==10 && s.invalid_ab==2 && s.decoder_partial==0);
 assert(s.detent_positive==1 && s.detent_negative==1 && detents==2);
 assert(s.press_down==1 && s.press_up==1 && s.record_on==1 && s.record_off==1);
 assert(s.attempts[0]==2 && s.accepted[0]==2 && s.rejected[0]==0);
 assert(s.attempts[1]==2 && s.accepted[1]==2 && s.rejected[1]==0);
 assert(s.attempts[2]==6 && s.accepted[2]==2 && s.rejected[2]==4);
 assert(s.event_ms[0]==60 && s.attempt_ms[0]==70 && s.event_age_ms[0]==10 && s.event_age_max_ms[0]==10);
 assert(s.event_ms[2]==60 && s.attempt_ms[2]==70 && s.event_age_max_ms[2]==10);
#else
 assert(tab5_input_gpio_start(&c,&cb)==ESP_ERR_NOT_SUPPORTED);
 assert(configured==0 && !task && tab5_input_gpio_retries()==0);
 tab5_input_gpio_snapshot(&again);
 assert(!again.enabled && !again.initialized && !again.sampled && reads==0);
 assert(again.config.a==-1 && again.raw[3]==-1 && again.start_error==ESP_ERR_NOT_SUPPORTED);
#endif
 return 0;
}
'''

TEMPORAL = r'''
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "tab5_input_gpio.h"
#include "driver/gpio.h"
int test_lock_depth;
static unsigned tick,positive,negative;
static void (*task)(void *);
static jmp_buf done;
int gpio_config(const gpio_config_t *g){(void)g;return ESP_OK;}
int gpio_reset_pin(int p){(void)p;return ESP_OK;}
int gpio_get_level(int p){
 const unsigned phases[]={3,2,0,1}; /* one valid AB phase every 2ms */
 unsigned ab=phases[(tick/2)%4];
 return p==3?(int)(ab>>1):p==4?(int)(ab&1):1;
}
int64_t esp_timer_get_time(void){return (int64_t)tick*1000;}
int xTaskCreate(void (*fn)(void *),const char *n,unsigned s,void *a,unsigned p,void *o){
 (void)n;(void)s;(void)a;(void)p;(void)o;task=fn;return pdPASS;
}
void vTaskDelete(void *p){(void)p;assert(0);}
void vTaskDelay(unsigned ms){tick+=ms;if(tick>=80)longjmp(done,1);}
static bool detent(int d,uint32_t ms){(void)ms;if(d>0)positive++;else negative++;return true;}
static bool level(bool on,uint32_t ms){(void)on;(void)ms;assert(0);return true;}
int main(int argc,char **argv){
 assert(argc==2);unsigned poll=(unsigned)atoi(argv[1]);
 tab5_gpio_config c={3,4,2,48,30,poll};tab5_gpio_callbacks cb={detent,level,level};
 assert(tab5_input_gpio_start(&c,&cb)==ESP_OK);
 if(setjmp(done)==0)task(NULL);
 tab5_gpio_status s;tab5_input_gpio_snapshot(&s);
 assert(s.detent_positive==positive && s.detent_negative==negative);
 assert(s.poll_gap_max_ms==poll && s.press_down==0 && s.record_on==0);
 printf("{\"poll_ms\":%u,\"positive\":%u,\"negative\":%u,\"invalid_ab\":%u,\"gap_max\":%u}\n",
 poll,positive,negative,s.invalid_ab,s.poll_gap_max_ms);
 return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='tab5-input-') as tmp:
    d = pathlib.Path(tmp)
    (d / 'fake.h').write_text(COMMON)
    for name in ('esp_err.h', 'esp_timer.h', 'freertos/FreeRTOS.h',
                 'freertos/queue.h', 'freertos/task.h', 'driver/gpio.h'):
        f = d / name
        f.parent.mkdir(exist_ok=True, parents=True)
        f.write_text('#include "fake.h"\n')
    (d / 'freertos/FreeRTOS.h').write_text(
        '#define TEST_FREERTOS_INCLUDED 1\n#include "fake.h"\n')
    (d / 'freertos/task.h').write_text(
        '#ifndef TEST_FREERTOS_INCLUDED\n'
        '#error "include FreeRTOS.h before task.h"\n'
        '#endif\n#include "fake.h"\n')
    (d / 'test.c').write_text(HARNESS)
    for enabled in (0, 1):
        (d / 'sdkconfig.h').write_text(f'#define CONFIG_TAB5_INPUT_GPIO_ENABLE {enabled}\n')
        exe = d / f'test{enabled}.exe'
        subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        f'-DCONFIG_TAB5_INPUT_GPIO_ENABLE={enabled}',
                        '-I'+str(d), '-I'+str(SRC), str(d/'test.c'),
                        str(SRC/'tab5_input.c'), str(SRC/'tab5_input_gpio.c'),
                        '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        print(f'GPIO adapter CONFIG={enabled}: PASS')
    (d / 'sdkconfig.h').write_text('#define CONFIG_TAB5_INPUT_GPIO_ENABLE 1\n')
    (d / 'temporal.c').write_text(TEMPORAL)
    exe = d / 'temporal.exe'
    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(d), '-I'+str(SRC), str(d/'temporal.c'),
                    str(SRC/'tab5_input.c'), str(SRC/'tab5_input_gpio.c'),
                    '-o', str(exe)], check=True)
    fast, slow = [json.loads(subprocess.check_output([str(exe), str(p)], text=True)) for p in (1, 5)]
    assert fast['positive'] == 9 and fast['negative'] == 0 and fast['invalid_ab'] == 0
    assert slow['positive'] < fast['positive'] and slow['invalid_ab'] > 0
    print('Temporal fixture 2ms AB phases (software stimulus, NOT hardware):', fast, slow, 'PASS')
