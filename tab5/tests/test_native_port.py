"""Host source-stub tests of the actual native port (no devices or renderer).
Checks startup cleanup, buffer ownership, idle skips, lock boundaries and PPA90
inverse coordinates. Target builds separately validate actual IDF signatures.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
STUB = r'''
#include "native/native_ui.h"
#include "tab5_ui.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
typedef int esp_err_t;
typedef int BaseType_t;
typedef int portMUX_TYPE;
typedef struct {int tokens;} *SemaphoreHandle_t;
typedef void *esp_lcd_panel_handle_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef void *ppa_client_handle_t;
typedef struct {void *io;} *esp_lcd_touch_handle_t;
typedef struct {int dummy;} esp_lcd_dpi_panel_event_data_t;
typedef struct {bool (*on_color_trans_done)(esp_lcd_panel_handle_t,esp_lcd_dpi_panel_event_data_t*,void*);} esp_lcd_dpi_panel_event_callbacks_t;
typedef struct {int oper_type;} ppa_client_config_t;
typedef struct {const void *buffer;size_t buffer_size;int pic_w,pic_h,block_w,block_h,srm_cm;} ppa_buf_t;
typedef struct {ppa_buf_t in,out;int rotation_angle;float scale_x,scale_y;int mode;} ppa_srm_oper_config_t;
typedef struct {void *panel;} bsp_lcd_handles_t;
typedef struct {int dummy;} bsp_display_config_t;
typedef struct {uint64_t render_us,flush_us,refresh_us,wait_us,rotate_us;uint32_t renders,flushes,refreshes,refresh_max_us,errors,interactive_renders,input_render_wait_max_us;} tab5_native_port_stats_t;
enum {ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=1,ESP_ERR_INVALID_STATE=2,pdTRUE=1,pdFALSE=0,pdPASS=1,
 MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,PPA_OPERATION_SRM=1,PPA_SRM_COLOR_MODE_RGB565=1,
 PPA_SRM_ROTATION_ANGLE_90=1,PPA_TRANS_MODE_BLOCKING=1,BSP_FEATURE_TOUCH=1,BSP_FEATURE_LCD=2};
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(n) (n)
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static int fail_at,step,live,display_live,io_live,renderer_live,ui_held;
static unsigned rotations,submits,ticks,touches,renders,dirty,loops,loop_limit;
static bool auto_complete;
static bool input_pressed,touch_dirty,stream_dirty;
static unsigned input_begin_loop,input_end_loop,complete_at_loop;
static uint64_t draw_times[256];
static unsigned draw_count;
static uint64_t test_now;
static jmp_buf stop_loop;
static SemaphoreHandle_t first_mutex;
static bool (*done_cb)(void*,esp_lcd_dpi_panel_event_data_t*,void*);
static bool fails(void){return ++step==fail_at;}
static void *allocated(size_t n){if(fails())return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void released(void *p){if(p){live--;free(p);}}
static SemaphoreHandle_t xSemaphoreCreateMutex(void){SemaphoreHandle_t s=allocated(sizeof(*s));if(s){s->tokens=1;first_mutex=s;}return s;}
static SemaphoreHandle_t xSemaphoreCreateBinary(void){return allocated(sizeof(struct {int tokens;}));}
static void vSemaphoreDelete(SemaphoreHandle_t s){if(s==first_mutex)first_mutex=NULL;released(s);}
static int xSemaphoreTake(SemaphoreHandle_t s,unsigned wait){(void)wait;if(!s->tokens)return 0;s->tokens--;if(s==first_mutex){assert(!ui_held);ui_held=1;}return 1;}
static void xSemaphoreGive(SemaphoreHandle_t s){s->tokens=1;if(s==first_mutex){assert(ui_held);ui_held=0;}}
static void xSemaphoreGiveFromISR(SemaphoreHandle_t s,BaseType_t *wake){s->tokens=1;*wake=0;}
static void *heap_caps_aligned_alloc(size_t align,size_t n,int flags){assert(align==64&&flags==3);return allocated(n);}
static int64_t esp_timer_get_time(void){return (int64_t)test_now;}
static int bsp_display_new_with_handles(const bsp_display_config_t *c,bsp_lcd_handles_t *h){(void)c;if(fails())return ESP_FAIL;display_live=1;h->panel=(void*)1;return 0;}
static void bsp_display_delete(void){display_live=0;}
static int bsp_feature_enable(int f,bool on){(void)f;(void)on;return 0;}
static int bsp_touch_new(void *c,esp_lcd_touch_handle_t *t){(void)c;*t=allocated(sizeof(**t));if(!*t)return ESP_FAIL;(*t)->io=(void*)2;io_live=1;return 0;}
static int esp_lcd_touch_del(esp_lcd_touch_handle_t t){released(t);return 0;}
static int esp_lcd_panel_io_del(void *p){assert(p==(void*)2);io_live=0;return 0;}
static int esp_lcd_dpi_panel_register_event_callbacks(void *p,const esp_lcd_dpi_panel_event_callbacks_t *cb,void *ctx){(void)p;(void)ctx;if(fails())return ESP_FAIL;done_cb=cb->on_color_trans_done;return 0;}
static int ppa_register_client(const ppa_client_config_t *c,void **p){assert(c->oper_type==1);*p=allocated(1);return *p?0:ESP_FAIL;}
static int ppa_unregister_client(void *p){released(p);return 0;}
static int esp_lcd_panel_disp_on_off(void *p,bool on){(void)p;assert(on);return fails()?ESP_FAIL:0;}
static bool tab5_panel_ready(void){return !fails();}
static void tab5_panel_observer_reset(void){}
static int bsp_display_brightness_set(int n){assert(n==60);return fails()?ESP_FAIL:0;}
static int xTaskCreatePinnedToCore(void (*fn)(void*),const char *name,unsigned stack,void *a,unsigned priority,void *h,int core){(void)fn;(void)name;(void)a;(void)h;assert(stack==8192&&priority==4&&core==1);return fails()?0:pdPASS;}
bool nui_native_init(int w,int h,uint16_t *p){assert(w==1280&&h==720&&p);renderer_live=1;return !fails();}
void nui_native_deinit(void){renderer_live=0;}
void nui_native_set_clock_cb(uint64_t (*cb)(void)){assert(cb);}
void tab5_ui_init(tab5_ui_action_cb_t cb,void *p){(void)cb;(void)p;}
void tab5_ui_destroy(void){}
void nui_native_tick(uint32_t ms){(void)ms;assert(ui_held);ticks++;}
void nui_native_touch(int x,int y,bool p){(void)x;(void)y;assert(ui_held);touches++;if(touch_dirty&&(p||(input_end_loop&&loops==input_end_loop)))dirty=1;}
size_t nui_native_render(void){assert(ui_held);renders++;unsigned n=dirty;dirty=0;if(n){assert(draw_count<256);draw_times[draw_count++]=test_now;}return n;}
void nui_native_invalidate(const nui_area_t *a){assert(ui_held&&a&&a->x2==1279);dirty=1;}
static int esp_lcd_touch_read_data(void *p){(void)p;assert(!ui_held);return 0;}
static bool esp_lcd_touch_get_coordinates(void *p,uint16_t *x,uint16_t *y,uint16_t *s,uint8_t *n,unsigned cap){(void)p;assert(cap==1);if(!input_pressed)return false;*x=100;*y=100;*s=1;*n=1;return true;}
static int ppa_do_scale_rotate_mirror(void *p,const ppa_srm_oper_config_t *c){assert(p&&!ui_held);assert(c->in.pic_w==1280&&c->in.pic_h==720&&c->out.pic_w==720&&c->out.pic_h==1280&&c->rotation_angle==1);rotations++;return 0;}
static int esp_lcd_panel_draw_bitmap(void *p,int x,int y,int w,int h,void *buf){assert(p&&buf&&!ui_held&&x==0&&y==0&&w==720&&h==1280);submits++;return 0;}
static void vTaskDelay(unsigned ms){assert(!ui_held);test_now+=(uint64_t)ms*1000;loops++;if((auto_complete||loops==complete_at_loop)&&done_cb)done_cb(NULL,NULL,NULL);if(input_begin_loop&&loops==input_begin_loop)input_pressed=true;if(input_end_loop&&loops==input_end_loop)input_pressed=false;if(stream_dirty)dirty=1;if(loops>=loop_limit)longjmp(stop_loop,1);}
#define free released
'''
TEST = r'''
#undef free
static void run_loop(unsigned count){loops=0;loop_limit=count;if(!setjmp(stop_loop))native_task(NULL);}
static void fresh_runtime(void){
 startup_cleanup(true,true);started=false;assert(!live);
 stats=(tab5_native_port_stats_t){0};rotations=submits=ticks=touches=renders=draw_count=0;
 input_pressed=touch_dirty=stream_dirty=auto_complete=false;
 input_begin_loop=input_end_loop=complete_at_loop=0;test_now=1000000;dirty=1;
 assert(tab5_native_port_start(NULL,NULL)==ESP_OK);
}
int main(void){
 for(int fault=1;fault<=12;fault++){
  fail_at=fault;step=0;assert(tab5_native_port_start(NULL,NULL)!=ESP_OK);
  assert(!live&&!display_live&&!io_live&&!renderer_live&&!started&&!ui_held);
 }
 fail_at=0;step=0;assert(tab5_native_port_start(NULL,NULL)==ESP_OK);
 assert(tab5_native_port_start(NULL,NULL)==ESP_ERR_INVALID_STATE);
 for(unsigned x=0;x<1280;x++)for(unsigned y=0;y<720;y++){int sx,sy;assert(native_touch_position(y,1279-x,&sx,&sy));assert(sx==(int)x&&sy==(int)y);}
 int x=7,y=8;assert(!native_touch_position(720,0,&x,&y)&&x==7&&y==8);assert(!native_touch_position(0,1280,&x,&y));
 test_now=200000;dirty=0;run_loop(30);assert(!rotations&&!submits);
 /* A missing completion leaves both framebuffer owners intact, permits
  * tick/touch and main lock acquisition, and never starts a second transfer. */
 dirty=1;auto_complete=false;run_loop(140);
 assert(rotations==1&&submits==1&&ticks==touches&&touches>=170);
 assert(tab5_native_port_lock(0));tab5_native_port_unlock();
 assert(frame&&rotated&&stats.errors==1);
 /* A press/release sampled 10/20 ms after a periodic frame is serviced at
  * 40 ms, not the former 100 ms cap. Both input events still reach UI. */
 fresh_runtime();auto_complete=true;touch_dirty=true;input_begin_loop=1;input_end_loop=2;
 run_loop(12);assert(draw_count==2&&draw_times[0]==1000000&&draw_times[1]==1040000);
 assert(stats.interactive_renders==1&&stats.input_render_wait_max_us==30000);
 assert(touches==12&&rotations==2&&submits==2);
 /* Holding/repeat may request fast frames, but cannot exceed 25 Hz. */
 fresh_runtime();auto_complete=true;input_pressed=touch_dirty=true;run_loop(100);
 assert(draw_count==25&&stats.interactive_renders==25);
 for(unsigned i=1;i<draw_count;i++)assert(draw_times[i]-draw_times[i-1]>=40000);
 /* Non-input dirty producers retain their 10 Hz display budget. */
 fresh_runtime();auto_complete=true;stream_dirty=true;run_loop(100);
 assert(draw_count==10&&!stats.interactive_renders);
 for(unsigned i=1;i<draw_count;i++)assert(draw_times[i]-draw_times[i-1]>=100000);
 /* Urgency never overrides pending DMA. A request survives a late callback. */
 fresh_runtime();touch_dirty=true;input_begin_loop=1;input_end_loop=2;complete_at_loop=20;
 run_loop(25);assert(draw_count==2&&draw_times[1]==1200000);
 assert(rotations==2&&submits==2&&stats.interactive_renders==1);
 assert(stats.input_render_wait_max_us==190000);
 startup_cleanup(true,true);started=false;
 assert(!live&&!display_live&&!io_live&&!renderer_live);
 puts("native port source: 12 startup fault cleanups/restart, idle skip, no-lock PPA/flush, no reuse without completion, all PPA90 pixels PASS");
 puts("native scheduling: 40 ms input eligibility / 100 ms periodic; 25 Hz hold bound; late-DMA request preserved PASS (stub time, not hardware latency)");
 return 0;
}
'''


def main():
    source = (ROOT/'tab5/main/tab5_native_port.c').read_text(encoding='utf-8')
    source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
    with tempfile.TemporaryDirectory(prefix='native-port-host-') as directory:
        p = Path(directory)
        (p/'test.c').write_text(STUB+source+TEST, encoding='utf-8')
        subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror',
                        '-I'+str(ROOT/'tab5/main'),str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
        subprocess.run([str(p/'test.exe')],check=True)


if __name__ == '__main__':
    main()
