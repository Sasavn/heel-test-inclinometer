"""Offline clock backend tests. No Windows clock setter, UDP, GUI or board.
Builds real board/time/core with fake I2C/system-clock functions in a temp folder.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import re

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / 'tab5/main'
CC = shutil.which('gcc')
if not CC:
    raise SystemExit('gcc is required')

SHIM = r'''
#include "tab5_board.h"
#include "driver/i2c_master.h"
#include <sys/time.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char regs[256];static int read_fail=-1,write_fail=-1,stuck=-1;
static int system_error,clock_calls,flag_writes;static int64_t fake_epoch;
int bsp_i2c_init(void){return 0;}void *bsp_i2c_get_handle(void){return (void*)1;}
int i2c_master_bus_add_device(void *bus,const i2c_device_config_t *cfg,i2c_master_dev_handle_t *dev){(void)bus;*dev=(void*)(uintptr_t)cfg->device_address;return 0;}
int i2c_master_transmit_receive(void *d,const void *reg,size_t rn,void *out,size_t n,int timeout){
 (void)d;(void)rn;(void)timeout;int r=*(const unsigned char*)reg;if(r==read_fail)return -1;memcpy(out,regs+r,n);return 0;}
int i2c_master_transmit(void *d,const unsigned char *data,size_t n,int timeout){
 (void)d;(void)timeout;int r=data[0];if(r==write_fail)return -1;if(r==0x1d)flag_writes++;
 if(r!=stuck){memcpy(regs+r,data+1,n-1);}return 0;}
int tab5_test_settimeofday(const struct timeval *tv,const void *tz){(void)tz;clock_calls++;if(system_error)return -1;fake_epoch=tv->tv_sec;return 0;}
static void test_production_rows(void);
static void fresh(void){memset(regs,0,sizeof(regs));read_fail=write_fail=stuck=-1;system_error=clock_calls=flag_writes=0;fake_epoch=0;}
void test_board_io(void){
 fresh();regs[0x1d]=0x82;regs[0x1f]=0xc3;tab5_time_set_offset(180);tab5_board_init();
 assert(tab5_board_backup_ready()&&regs[0x1f]==0xf0&&regs[0x1d]==0x82&&!clock_calls&&!flag_writes);
 /* Existing RTC stored local 2026-10-26 00:30, default Moscow -> prior UTC day. */
 fresh();unsigned char wall[]={0,0x30,0,2,0x26,0x10,0x26};memcpy(regs+0x10,wall,7);tab5_board_init();
 assert(tab5_board_time_valid()&&tab5_board_rtc_valid());
 struct tm utc;assert(tab5_time_utc_to_local(fake_epoch,0,&utc)&&utc.tm_mday==25&&utc.tm_hour==21&&utc.tm_min==30);
 struct tm local={.tm_year=128,.tm_mon=1,.tm_mday=29,.tm_hour=12};int64_t epoch;assert(tab5_time_local_to_utc(&local,0,&epoch));
 regs[0x1d]=0xb2;tab5_time_apply_result_t r=tab5_board_set_utc(epoch,0);
 assert(r.system_ok&&r.rtc_ok&&regs[0x1d]==0x30&&regs[0x14]==0x29&&regs[0x15]==2);
 read_fail=0x1d;r=tab5_board_set_utc(epoch,0);assert(r.system_ok&&!r.rtc_ok&&!tab5_board_rtc_valid());read_fail=-1;
 write_fail=0x1d;r=tab5_board_set_utc(epoch,0);assert(r.system_ok&&!r.rtc_ok);write_fail=-1;
 regs[0x1d]=0x82;stuck=0x1d;r=tab5_board_set_utc(epoch,0);assert(r.system_ok&&!r.rtc_ok);stuck=-1;
 write_fail=0x10;r=tab5_board_set_utc(epoch,0);assert(r.system_ok&&!r.rtc_ok);write_fail=-1;
 int before=flag_writes;system_error=1;r=tab5_board_set_utc(epoch,0);assert(!r.system_ok&&!r.rtc_ok&&flag_writes==before);
 fresh();read_fail=0x1f;regs[0x1d]=0x82;tab5_board_init();assert(!tab5_board_backup_ready());
 fresh();stuck=0x1f;regs[0x1d]=0x82;tab5_board_init();assert(!tab5_board_backup_ready());
 fresh();regs[0x1d]=0;memcpy(regs+0x10,wall,7);regs[0x14]=0x31;regs[0x15]=4;tab5_board_init();assert(!tab5_board_time_valid()&&!clock_calls);
 puts("PASS: production board local-RTC compatibility, backup POR/readback, flags preserved, RTC write/read/clear/readback errors, system error, invalid RTC calendar");
 test_production_rows();
}
'''

# Compile exact production bodies, not a reimplementation of CSV timestamp logic.
SOURCE = (MAIN / 'main.c').read_text(encoding='utf-8')
def body(signature):
    start = SOURCE.index(signature)
    end = SOURCE.index('\n}', start) + 2
    return SOURCE[start:end]

ROW = body('static tab5_service_sample_t make_row(')
APPLY = body('static bool apply_wall_time(')
CALLBACK = body('static void ui_callback(')
REJECTED = body('static void consume_rejected_actions(')
assert 'p->timestamp_us/1000' in ROW
assert not re.search(r'\b(?:record_start_us|record_name|record_duration_s)\s*=', APPLY)
assert 'set_recording(' not in APPLY
assert 'time_tz_offset' in SOURCE and 'tab5_time_parse_command(line+9' in SOURCE
assert not re.search(r'\b(?:localtime_r|mktime|setenv|tzset)\s*\(', SOURCE)
SHIM += r'''
#include "tab5_measure.h"
#include "tab5_services.h"
#include "tab5_ui.h"
#include <math.h>
typedef struct {unsigned index;tab5_measure_sensor_t value;} sample_event_t;
static bool recording;
static uint64_t record_start_us;
static uint32_t record_duration_s;
static char record_name[40];
static char time_sync_status[128];
static char notice[160];
static uint32_t rejected_actions,ui_result_serial;
static tab5_ui_action_kind_t ui_result_kind;
static bool ui_result_success;
static void *actions;
static int test_queue_result;
static int test_queue_send(void *queue,const void *item,unsigned wait){(void)queue;(void)item;assert(wait==0);return test_queue_result;}
#define pdTRUE 1
#define xQueueSend test_queue_send
static bool save_fail;
static int persisted_offset;
static bool persist_timezone(int offset){if(save_fail)return false;persisted_offset=offset;return true;}
static time_t tab5_test_time(time_t *out){if(out)*out=(time_t)fake_epoch;return (time_t)fake_epoch;}
'''
SHIM += APPLY + '\n#define time(...) tab5_test_time(__VA_ARGS__)\n' + ROW + '\n#undef time\n'
SHIM += CALLBACK + '\n' + REJECTED + '\n'
SHIM += r'''
static void test_production_rows(void){
 tab5_ui_action_t a={.kind=TAB5_UI_TIME_SYNC};test_queue_result=0;ui_callback(&a,NULL);
 assert(rejected_actions==(1u<<TAB5_UI_TIME_SYNC)&&ui_result_serial==0);
 consume_rejected_actions();assert(!rejected_actions&&ui_result_serial==1&&ui_result_kind==TAB5_UI_TIME_SYNC&&!ui_result_success);
 assert(strstr(time_sync_status,"переполнена"));consume_rejected_actions();assert(ui_result_serial==1);
 test_queue_result=1;ui_callback(&a,NULL);assert(!rejected_actions);
 fresh();tab5_time_set_offset(180);tab5_board_init();
 struct tm date={.tm_year=126,.tm_mon=9,.tm_mday=26,.tm_hour=12};int64_t base;
 assert(tab5_time_local_to_utc(&date,180,&base));
 recording=true;record_start_us=5000000;record_duration_s=17;strcpy(record_name,"KR_already_open.csv");
 const int corrections[]={0,3600,-3600,0,0};unsigned last_ms=0;
 char first[9]={0},previous[9]={0};
 for(unsigned i=0;i<5;i++){
  assert(apply_wall_time(base+corrections[i],180));
  sample_event_t e={.index=0,.value={.timestamp_us=6000000+i*50000,.decoded_x=1.2f,.decoded_y=2.3f}};
  tab5_service_sample_t row=make_row(&e,7.4f);
  assert(row.ms==6000+i*50&&row.ms>last_ms);last_ms=row.ms;
  assert(!strcmp(row.date,"26.10.2026"));
  const char *expected[]={"12:00:00","13:00:00","11:00:00","12:00:00","12:00:00"};
  assert(!strcmp(row.time,expected[i]));
  if(i==0)strcpy(first,row.time);
  if(i==4)assert(!strcmp(row.time,previous)&&!strcmp(row.time,first));
  strcpy(previous,row.time);
  assert(recording&&record_start_us==5000000&&record_duration_s==17&&!strcmp(record_name,"KR_already_open.csv"));
  assert(strstr(time_sync_status,"запись продолжается")&&persisted_offset==180);
 }
 assert(apply_wall_time(base,0)&&tab5_time_offset()==0&&persisted_offset==0);
 write_fail=0x10;assert(!apply_wall_time(base,0)&&strstr(time_sync_status,"RTC"));write_fail=-1;
 save_fail=true;assert(!apply_wall_time(base,0)&&strstr(time_sync_status,"flash"));save_fail=false;
 puts("PASS: exact production callback typed queue rejection, make_row+apply_wall_time with fake clock, forward/backward/repeated DateTime, monotonic Ms, recording identity/start unchanged, UTC0 persist, partial RTC/NVS failure");
}
'''

with tempfile.TemporaryDirectory(prefix='tab5-time-sync-') as td:
    out = Path(td)
    (out / 'driver').mkdir()
    (out / 'bsp').mkdir()
    (out / 'driver/i2c_master.h').write_text('''#pragma once
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
#define I2C_ADDR_BIT_LEN_7 0
typedef void *i2c_master_dev_handle_t;
typedef struct {int dev_addr_length,device_address,scl_speed_hz;} i2c_device_config_t;
int i2c_master_bus_add_device(void*,const i2c_device_config_t*,i2c_master_dev_handle_t*);
int i2c_master_transmit_receive(void*,const void*,size_t,void*,size_t,int);
int i2c_master_transmit(void*,const unsigned char*,size_t,int);
''', encoding='utf-8')
    (out / 'bsp/esp-bsp.h').write_text('#pragma once\nint bsp_i2c_init(void);void *bsp_i2c_get_handle(void);\n', encoding='utf-8')
    shim = out / 'shim.c'
    shim.write_text(SHIM, encoding='utf-8')
    declarations = out / 'clock_hook.h'
    declarations.write_text('#include <sys/time.h>\nint tab5_test_settimeofday(const struct timeval*,const void*);\n', encoding='utf-8')
    exe = out / 'test.exe'
    subprocess.run([CC, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Dsettimeofday=tab5_test_settimeofday',
                    '-include', str(declarations),
                    '-I'+str(out), '-I'+str(MAIN),
                    str(ROOT / 'tab5/tests/test_time_sync.c'), str(MAIN / 'tab5_time.c'),
                    str(MAIN / 'tab5_time_sync.c'), str(MAIN / 'tab5_board.c'), str(shim),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)
