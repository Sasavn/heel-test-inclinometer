"""Compile the production UART entry/transaction functions against host stubs.

No UART/device access. The IDF driver's ISR is not simulated as hardware proof.
Run: python tab5/tests/test_measure_uart.py
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def function(source, name):
    match = re.search(r'^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
    assert match, name
    masked = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                    lambda m: ' ' * len(m[0]), source, flags=re.S)
    depth = 1
    for end in range(match.end(), len(source)):
        depth += (masked[end] == '{') - (masked[end] == '}')
        if not depth:
            return source[match.start():end + 1]
    raise AssertionError(name)


STUBS = r'''
#include "tab5_measure.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef unsigned TickType_t;
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef struct {int baud_rate,data_bits,parity,stop_bits,flow_ctrl,source_clk;} uart_config_t;
typedef struct {uint64_t pin_bit_mask;int mode;} gpio_config_t;
enum {ESP_OK=0,ESP_ERR_INVALID_STATE=1,ESP_ERR_NO_MEM=2,ESP_FAIL=3,
 UART_NUM_1=1,UART_DATA_8_BITS=8,UART_PARITY_DISABLE=0,UART_STOP_BITS_1=1,
 UART_HW_FLOWCTRL_DISABLE=0,UART_SCLK_DEFAULT=0,GPIO_MODE_OUTPUT=1,
 GPIO_NUM_34=34,UART_PIN_NO_CHANGE=-1,UART_MODE_RS485_HALF_DUPLEX=7,pdPASS=1};
#define configTICK_RATE_HZ 1000
#define portMAX_DELAY 0xffffffffu
#define PORT UART_NUM_1
static tab5_measure_core_t device;
static SemaphoreHandle_t mutex;
static TaskHandle_t worker;
static tab5_measure_sample_cb_t sample_cb;
static void *sample_user;
static uint64_t clock_us,tx_at,rx_at;
static unsigned flushes,writes,mode_calls,deletes,manual_high,order,pin_order,rts_order;
static int mode_fail,flush_fail,tx_fail,task_fail;
static uint8_t rx[32];static size_t rx_len,rx_pos;
static bool late_rx_wait;
static int64_t esp_timer_get_time(void){return clock_us;}
static void vTaskDelay(unsigned t){if(late_rx_wait){clock_us+=35000;late_rx_wait=false;}else clock_us+=t*1000;}
static void yield(void){clock_us+=100;}
#define taskYIELD() yield()
static int xSemaphoreTake(void *m,unsigned t){(void)m;(void)t;return 1;}
static int xSemaphoreGive(void *m){(void)m;return 1;}
static void *xSemaphoreCreateMutex(void){return (void*)1;}
static void vSemaphoreDelete(void *m){(void)m;deletes++;}
static int gpio_config(const gpio_config_t *g){assert(g->pin_bit_mask==(1ULL<<34));return 0;}
static int gpio_set_level(int p,int n){assert(p==34);manual_high+=n!=0;return 0;}
static int uart_param_config(int p,const uart_config_t *u){assert(p==PORT&&u->baud_rate==115200&&u->flow_ctrl==0);return 0;}
static int uart_set_rts(int p,int n){assert(p==PORT&&n==1);rts_order=++order;return 0;}
static int uart_set_pin(int p,int tx,int r,int rts,int cts){assert(p==PORT&&tx==20&&r==21&&rts==34&&cts==-1);pin_order=++order;assert(rts_order<pin_order);return 0;}
static int uart_driver_install(int p,int r,int t,int q,void *h,int f){assert(p==PORT&&r==256&&!t&&!q&&!h&&!f);return 0;}
static int uart_set_mode(int p,int m){assert(p==PORT&&m==UART_MODE_RS485_HALF_DUPLEX);mode_calls++;return mode_fail?ESP_FAIL:0;}
static int uart_driver_delete(int p){assert(p==PORT);deletes++;return 0;}
static void measure_task(void *p){(void)p;}
static int xTaskCreatePinnedToCore(void (*f)(void*),const char *n,unsigned stack,void *arg,unsigned priority,void **h,int core){
 (void)n;assert(f==measure_task&&stack==4096&&!arg&&priority==5&&mode_calls&&core==0);if(task_fail)return 0;*h=(void*)2;return pdPASS;
}
static int uart_flush_input(int p){assert(p==PORT);flushes++;rx_len=rx_pos=0;return flush_fail?ESP_FAIL:0;}
static int uart_write_bytes(int p,const void *data,size_t n){
 assert(p==PORT&&n==8&&mode_calls&&flushes);const uint8_t *b=data;
 assert(tab5_measure_crc16(b,6)==(uint16_t)(b[6]|b[7]<<8));writes++;tx_at=clock_us;return (int)n;
}
static int uart_wait_tx_done(int p,unsigned ticks){
 assert(p==PORT&&ticks==30);clock_us+=700;
 /* A complete fast reply is buffered BEFORE the waiting task resumes. */
 rx_at=clock_us;clock_us+=5000;return tx_fail?ESP_FAIL:0;
}
static int uart_read_bytes(int p,void *out,unsigned n,unsigned ticks){
 assert(p==PORT&&n==1&&!ticks);if(late_rx_wait||rx_pos==rx_len)return 0;*(uint8_t*)out=rx[rx_pos++];return 1;
}
'''

TESTS = r'''
static unsigned response_kind;
/* Inject data after the pre-TX flush, at the driver's TX completion. */
static int wait_reply(int p,unsigned ticks){
 int result=uart_wait_tx_done(p,ticks);
 static const uint8_t normal[]={2,3,4,0x27,0x10,0x27,0x74};
 static const uint8_t exception[]={2,0x83,2};
 late_rx_wait=response_kind>=4;
 if(response_kind==3||response_kind==5)return result;
 size_t n=response_kind==1?3:7;
 memcpy(rx,response_kind==1?exception:normal,n);
 uint16_t crc=tab5_measure_crc16(rx,n);rx[n++]=(uint8_t)crc;rx[n++]=(uint8_t)(crc>>8);
 if(response_kind==2||response_kind==6)rx[n-1]^=1;
 if(response_kind==7)n=4;
 rx_len=n;return result;
}
'''

MAIN = r'''
int main(void){
 tab5_measure_config_t cfg;tab5_measure_config_default(&cfg);
 assert(tab5_measure_start(&cfg,NULL,NULL)==ESP_OK);
 assert(mode_calls==1&&!manual_high&&rts_order<pin_order);
 uint16_t raw[2];uint64_t end=clock_us;
 for(response_kind=0;response_kind<4;response_kind++){
  uint64_t previous=end;unsigned f=flushes;
  rx[0]=0xaa;rx_len=1;rx_pos=0; /* stale preceding transaction */
  int e=transact(0,raw,&end);
  assert(tx_at>=previous+15000&&flushes==f+1&&!manual_high&&end>=rx_at);
  assert(e==(response_kind==0?TAB5_MEASURE_OK:response_kind==1?TAB5_MEASURE_EXCEPTION:response_kind==2?TAB5_MEASURE_CRC:TAB5_MEASURE_TIMEOUT));
  if(!response_kind)assert(raw[0]==10000&&raw[1]==10100);
 }
 /* First RX check is empty, then the task wakes 35ms later with the frame
  * already buffered. The original deadline must not be extended or checked
  * before draining. A complete OLD frame is flushed even for blank new RX. */
 for(response_kind=4;response_kind<=7;response_kind++){
  static const uint8_t old[]={2,3,4,0x27,0x10,0x27,0x74};
  memcpy(rx,old,sizeof(old));uint16_t crc=tab5_measure_crc16(rx,sizeof(old));
  rx[7]=(uint8_t)crc;rx[8]=(uint8_t)(crc>>8);rx_len=9;rx_pos=0;
  raw[0]=123;raw[1]=456;unsigned f=flushes;uint64_t previous=end;
  int e=transact(0,raw,&end);
  assert(flushes==f+1&&tx_at>=previous+15000);
  assert(end-rx_at>=35000&&end-rx_at<41000); /* no new 30ms wait */
  assert(e==(response_kind==4?TAB5_MEASURE_OK:response_kind==5?TAB5_MEASURE_TIMEOUT:response_kind==6?TAB5_MEASURE_CRC:TAB5_MEASURE_FRAME));
  if(response_kind==4)assert(raw[0]==10000&&raw[1]==10100);
  else assert(raw[0]==123&&raw[1]==456);
 }
 response_kind=3;
 tx_fail=1;assert(transact(0,raw,&end)==TAB5_MEASURE_IO);tx_fail=0;
 flush_fail=1;unsigned w=writes;assert(transact(0,raw,&end)==TAB5_MEASURE_IO&&writes==w);flush_fail=0;
 mutex=NULL;worker=NULL;mode_fail=1;deletes=0;
 assert(tab5_measure_start(&cfg,NULL,NULL)==ESP_FAIL&&deletes==2&&!mutex&&!worker);
 mode_fail=0;task_fail=1;deletes=0;
 assert(tab5_measure_start(&cfg,NULL,NULL)==ESP_ERR_NO_MEM&&deletes==2&&!mutex&&!worker);
 puts("UART source stubs: late-wake complete/blank/CRC/partial, stale valid frame rejected, original 30ms deadline; RTS34/gap/parser/cleanup PASS");
 return 0;
}
'''


def main():
    compiler = shutil.which('gcc')
    assert compiler, 'gcc required'
    source = (ROOT / 'tab5/main/tab5_measure.c').read_text(encoding='utf-8')
    functions = {name: function(source, name) for name in
                 ('ticks_ceil', 'wait_until', 'transact', 'tab5_measure_start')}
    # Substitute the wait API only; every production function remains otherwise exact.
    harness = (STUBS + TESTS + functions['ticks_ceil'] + functions['wait_until'] +
               functions['transact'].replace('uart_wait_tx_done(', 'wait_reply(') +
               functions['tab5_measure_start'] + MAIN)
    with tempfile.TemporaryDirectory(prefix='tab5-uart-') as directory:
        path = Path(directory)
        (path / 'test.c').write_text(harness, encoding='utf-8')
        subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(ROOT / 'tab5/main'), str(path / 'test.c'),
                        str(ROOT / 'tab5/main/tab5_measure.c'), '-lm', '-o', str(path / 'test.exe')], check=True)
        subprocess.run([str(path / 'test.exe')], check=True)


if __name__ == '__main__':
    main()
