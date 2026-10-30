"""Compile production adapter with small ESP-IDF fakes, execute real poll task."""
import pathlib
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
#include "tab5_input_gpio.h"
#include "driver/gpio.h"
static unsigned tick, count, configured, resets, rejected;
static bool fail_task;
static void (*task)(void *);
static struct { int kind; bool level; uint32_t ms; } events[16];
static jmp_buf done;
int gpio_config(const gpio_config_t *g) {
 assert(g->pin_bit_mask==((1ULL<<2)|(1ULL<<3)|(1ULL<<4)|(1ULL<<48)));
 assert(g->mode==GPIO_MODE_INPUT && g->intr_type==GPIO_INTR_DISABLE);
 configured++; return ESP_OK;
}
int gpio_reset_pin(int p) { (void)p; resets++; return 0; }
int gpio_get_level(int p) {
 if(p==2) return tick<5?0:tick<40?1:0; /* boot ON -> OFF -> ON */
 if(p==48) return tick<70?1:tick<100?0:1;
 return 1;
}
int64_t esp_timer_get_time(void) { return (int64_t)tick*1000; }
static bool level(int kind,bool value,uint32_t ms) {
 /* Reject each record transition twice; original timestamps must survive. */
 if(kind==0 && rejected<2) { rejected++; return false; }
 events[count].kind=kind; events[count].level=value; events[count++].ms=ms;
 rejected=0; return true;
}
static bool record(bool on,uint32_t ms) { return level(0,on,ms); }
static bool button(bool down,uint32_t ms) { return level(1,down,ms); }
static bool detent(int delta,uint32_t ms) { (void)delta;(void)ms; return true; }
int xTaskCreate(void (*fn)(void *),const char *n,unsigned stack,void *arg,unsigned pri,void *out) {
 (void)n;(void)stack;(void)arg;(void)pri;(void)out;
 task=fn; return fail_task?0:pdPASS;
}
void vTaskDelete(void *p) { (void)p; longjmp(done,2); }
void vTaskDelay(unsigned n) { tick+=n; if(tick>=140) longjmp(done,1); }
int main(void) {
 tab5_gpio_config c={3,4,48,2,20,5};
 tab5_gpio_callbacks cb={detent,button,record};
#if CONFIG_TAB5_INPUT_GPIO_ENABLE
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
 assert(tab5_input_gpio_start(&c,&cb)==ESP_ERR_INVALID_STATE);
 if(setjmp(done)==0) task(NULL);
 assert(count==4);
 assert(events[0].kind==0 && !events[0].level && events[0].ms==25);
 assert(events[1].kind==0 && events[1].level && events[1].ms==60);
 /* Sampling resumes after backpressure; no accepted command is repeated. */
 assert(events[2].kind==1 && events[2].level && events[2].ms==95);
 assert(events[3].kind==1 && !events[3].level && events[3].ms==120);
 assert(tab5_input_gpio_retries()==4);
#else
 assert(tab5_input_gpio_start(&c,&cb)==ESP_ERR_NOT_SUPPORTED);
 assert(configured==0 && !task && tab5_input_gpio_retries()==0);
#endif
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
