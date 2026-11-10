"""Host source-stub tests of the actual native port (no devices or renderer).
Checks startup cleanup, buffer ownership, idle skips, lock boundaries and PPA90
inverse coordinates. Target builds separately validate actual IDF signatures.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "tab5/main/tab5_native_port.c"
STUB = r'''
#include <stdatomic.h>
#include "native/native_ui.h"
#include "tab5_ui.h"
void tab5_ui_encoder_wake(uint32_t ms){(void)ms;}
#include "tab5_backlight.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
void tab5_ui_touch(int32_t x,int32_t y,bool p,uint32_t ms){(void)x;(void)y;(void)p;(void)ms;}
void tab5_ui_input(tab5_ui_input_event_t e,int32_t d,uint32_t ms){(void)e;(void)d;(void)ms;}

#define TAB5_NATIVE_INPUT_CAPACITY 64u
typedef struct {uint32_t posted,consumed,rejected,pending;} tab5_native_input_stats_t;
typedef struct {
    bool pending,active,finished,restored,cache_enabled;
    uint32_t request_id,requested_frames,completed_frames,accepted_frames,interval_us,reason,touch_reads,fps_milli;
    int32_t scroll_min,scroll_max;
    uint64_t started_us,elapsed_us,setup_us,cold_frame_us;
    uint64_t render_us,rotate_us,lcd_submit_us,lcd_wait_us,refresh_us;
    uint64_t lcd_copy_bytes;
} tab5_native_scroll_benchmark_status_t;
typedef int esp_err_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef unsigned TickType_t;
typedef struct {int dummy;} ppa_event_data_t;
typedef struct {bool (*on_trans_done)(void*,ppa_event_data_t*,void*);} ppa_event_callbacks_t;
typedef int portMUX_TYPE;
typedef struct {int tokens;} *SemaphoreHandle_t;
typedef void *esp_lcd_panel_handle_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef void *ppa_client_handle_t;
typedef struct {void *io;} *esp_lcd_touch_handle_t;
typedef struct {int dummy;} esp_lcd_dpi_panel_event_data_t;
typedef struct {bool (*on_color_trans_done)(esp_lcd_panel_handle_t,esp_lcd_dpi_panel_event_data_t*,void*);} esp_lcd_dpi_panel_event_callbacks_t;
typedef struct {int oper_type;} ppa_client_config_t;
typedef struct {const void *buffer;size_t buffer_size;int pic_w,pic_h,block_w,block_h,block_offset_x,block_offset_y,srm_cm;} ppa_buf_t;
typedef struct {ppa_buf_t in,out;int rotation_angle;float scale_x,scale_y;int mode;} ppa_srm_oper_config_t;
typedef struct {void *panel;} bsp_lcd_handles_t;
typedef struct {int dummy;} bsp_display_config_t;
typedef struct {uint64_t render_us,flush_us,refresh_us,wait_us,rotate_us,lcd_copy_bytes;uint32_t full_lcd_copies,partial_lcd_copies;uint32_t renders,flushes,refreshes,refresh_max_us,errors,interactive_renders,input_render_wait_max_us,verify_checks,verify_errors;uint64_t verify_mismatches,verify_us;uint32_t touch_reads,touch_read_gap_max_us,sample_render_count,sample_render_max_us,sample_lcd_count,sample_lcd_max_us;uint64_t sample_render_us,sample_lcd_us;uint32_t compose_frames,compose_copy_jobs,compose_ppa_jobs,compose_fallbacks,compose_timeouts;uint64_t compose_copy_bytes,compose_copy_us,compose_ppa_us;uint32_t compose_source_sync_jobs,compose_source_sync_hits;uint64_t compose_source_sync_bytes;uint32_t compose_cpu_jobs;uint64_t compose_cpu_pixels,compose_cpu_us;uint64_t touch_read_us,cpu_tick_us,input_dispatch_us,ui_lock_wait_us,wake_wait_us,callback_process_delay_us,cache_callback_process_delay_us,ppa_callback_process_delay_us,lcd_callback_process_delay_us;uint32_t compose_unaligned_views,producer_backpressure,overlap_renders,queued_peak,worker_completed_frames;uint64_t overlap_us;bool worker_enabled;uint32_t ppa_timeouts,lcd_timeouts,submit_errors,pipeline_state;} tab5_native_port_stats_t;
enum {ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=1,ESP_ERR_INVALID_STATE=2,pdTRUE=1,pdFALSE=0,pdPASS=1,
 MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,PPA_OPERATION_SRM=1,PPA_SRM_COLOR_MODE_RGB565=1,
 PPA_SRM_ROTATION_ANGLE_90=1,PPA_TRANS_MODE_BLOCKING=1,PPA_TRANS_MODE_NON_BLOCKING=2,ESP_ERR_INVALID_ARG=0x102,BSP_FEATURE_TOUCH=1,BSP_FEATURE_LCD=2};
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(n) (n)
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define portENTER_CRITICAL_ISR(p) ((void)(p))
#define portEXIT_CRITICAL_ISR(p) ((void)(p))
static int fail_at,step,live,display_live,io_live,renderer_live,ui_held;
static bool in_worker;static void (*advance_hook)(uint64_t);static void (*pixel_hook)(void);static int (*position_hook)(void);
static ppa_srm_oper_config_t last_op;
static int ppa_fault,lcd_fault,cache_fault,query_fault,copy_cache_fault;
static bool query_null,query_unaligned;
#define CONFIG_BSP_LCD_DPI_BUFFER_NUMS 1
static _Alignas(64) uint16_t driver_fb[1280*720];
static int lcd_x,lcd_y,lcd_w,lcd_h;
static const uint16_t *lcd_source;
static uint64_t lcd_bytes;
static bool record_trace;static unsigned trace_mode,trace_count;
static int completed_trace[2][120];
static int esp_lcd_dpi_panel_get_frame_buffer(void *panel,unsigned count,void **out,...){
 assert(panel&&count==1);*out=query_null?NULL:(void*)((unsigned char*)driver_fb+(query_unaligned?2:0));return query_fault;
}
static bool emulate_pixels,corrupt_ppa;
static bool ppa_inflight,lcd_inflight,auto_ppa_complete=true,accept_on_error;
static uint64_t ppa_delay_us,ppa_due,lcd_due,lcd_delay_us=10000;
static unsigned task_notifications,notify_calls,notify_immediates,notify_waits;
static bool semaphore_should_wake,notification_should_wake;
static unsigned touches_while_ppa,touches_while_lcd;
static unsigned physical_reads;static uint64_t physical_last_start;
static bool (*ppa_done_cb)(void*,ppa_event_data_t*,void*);
static void finish_ppa(void);static void finish_lcd(void);
static unsigned cache_checks;
static bool model_source;
static _Alignas(64) uint16_t source_cpu[10][64*64],source_physical[10][64*64];
static unsigned source_sync_calls;static size_t source_sync_bytes;
static int source_sync_fault;
static bool model_cache;
static uint16_t *model_canonical;
static _Alignas(64) uint16_t model_memory[1280*720];
static bool model_valid[1280*720*2/64];
static unsigned model_sync_calls,model_sync_bytes,model_sync_fail_at;
static const uint16_t *model_flush_lcd(const uint16_t *src,size_t bytes){
 if(!model_cache)return src;
 size_t offset=(size_t)(src-model_canonical)*2;assert(offset+bytes<=sizeof(model_memory));
 for(size_t i=0;i<sizeof(model_valid)/sizeof(*model_valid);i++)if(model_valid[i]){
  assert(i*64>=offset&&i*64+64<=offset+bytes);
  memcpy((uint8_t*)model_memory+i*64,(uint8_t*)model_canonical+i*64,64);
 }
 return model_memory+offset/2;
}
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C 1
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 2
#define ESP_CACHE_MSYNC_FLAG_UNALIGNED 4
static int esp_cache_msync(void *p,size_t n,int flags){
 assert(p&&n&&(in_worker||!ui_held)&&!ppa_inflight&&!lcd_inflight);
 if(p==driver_fb){assert(n==1280u*720u*2u&&flags==1);cache_checks++;return cache_fault;}
 assert(flags==1||flags==2||flags==6);
 if(model_source&&flags==2){
  unsigned i=0;while(i<10&&p!=source_cpu[i])i++;
  assert(i<10&&!(n%64)&&n<=sizeof(source_cpu[i]));source_sync_calls++;
  if(source_sync_fault)return source_sync_fault;
  memcpy(source_physical[i],source_cpu[i],n);source_sync_bytes+=n;
 }
 if(model_cache&&flags==1&&(uintptr_t)p>=(uintptr_t)model_canonical&&(uintptr_t)p<(uintptr_t)model_canonical+sizeof(model_memory)){
  size_t offset=(uintptr_t)p-(uintptr_t)model_canonical;assert(!(offset%64)&&!(n%64)&&offset+n<=sizeof(model_memory));
  /* No CPU store may precede ANY later invalidation, including shared lines. */
  for(size_t i=0;i<sizeof(model_valid)/sizeof(*model_valid);i++)if(model_valid[i])assert(!memcmp((uint8_t*)model_canonical+i*64,(uint8_t*)model_memory+i*64,64));
  model_sync_calls++;if(model_sync_calls==model_sync_fail_at)return ESP_FAIL;
  for(size_t i=offset/64;i<(offset+n)/64;i++){assert(!model_valid[i]);model_valid[i]=true;}
  memcpy(p,(uint8_t*)model_memory+offset,n);model_sync_bytes+=(unsigned)n;
 }
 return copy_cache_fault;
}

static nui_area_t test_dirty_areas[48]={{0,0,1279,719}};
static size_t test_dirty_count=1;
static nui_area_t pending_areas[48];static size_t pending_count;
static void pending_add(nui_area_t a){
 for(size_t i=0;i<pending_count;){nui_area_t b=pending_areas[i];
  if(a.x1<=b.x2&&b.x1<=a.x2&&a.y1<=b.y2&&b.y1<=a.y2){
   if(b.x1<a.x1)a.x1=b.x1;if(b.y1<a.y1)a.y1=b.y1;if(b.x2>a.x2)a.x2=b.x2;if(b.y2>a.y2)a.y2=b.y2;
   pending_areas[i]=pending_areas[--pending_count];i=0;
  }else i++;
 }
 if(pending_count==48){pending_count=1;pending_areas[0]=(nui_area_t){0,0,1279,719};}else pending_areas[pending_count++]=a;
}
size_t nui_native_get_pending_dirty(nui_area_t *out,size_t cap){assert(ui_held&&!in_worker);size_t n=pending_count<cap?pending_count:cap;if(out)memcpy(out,pending_areas,n*sizeof(*out));return pending_count;}

size_t nui_native_get_dirty(nui_area_t *out,size_t cap){size_t n=test_dirty_count<cap?test_dirty_count:cap;for(size_t i=0;i<n;i++)out[i]=test_dirty_areas[i];return test_dirty_count;}
static nui_surface_defer_cb_t defer_cb;static void *defer_user;
static unsigned releases,materializations,pins;static bool materialize_fault;
static uint16_t *mock_frame;
void nui_native_set_surface_defer_cb(nui_surface_defer_cb_t cb,void *user){defer_cb=cb;defer_user=user;}
bool nui_native_set_surface_alignment(unsigned tile){assert(tile==1||tile==16);return true;}
bool nui_native_set_dirty_alignment(unsigned tile){assert(tile==1||tile==16);return true;}
void nui_surface_view_release(nui_surface_view_t *v){assert(ui_held&&!in_worker&&v->lease&&pins);pins--;releases++;v->lease=NULL;}
bool nui_surface_view_materialize(const nui_surface_view_t *v){
 assert(ui_held&&!in_worker&&pins&&v->lease);materializations++;if(materialize_fault)return false;
 unsigned w=v->destination.x2-v->destination.x1+1,h=v->destination.y2-v->destination.y1+1;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)mock_frame[(v->destination.y1+y)*1280+v->destination.x1+x]=v->landscape[(v->source_y+y)*v->landscape_stride+v->source_x+x];
 return true;
}
static uint64_t test_now;
static uint64_t injected_touch_us,injected_tick_us,injected_input_us,injected_lock_us;
typedef void *esp_async_fbcpy_handle_t;
typedef struct {int unused;} esp_async_fbcpy_config_t;
typedef struct {int unused;} esp_async_fbcpy_event_data_t;
typedef bool(*esp_async_fbcpy_event_callback_t)(esp_async_fbcpy_handle_t,esp_async_fbcpy_event_data_t*,void*);
typedef struct {
 const void *src_buffer;void *dst_buffer;
 size_t src_buffer_size_x,src_buffer_size_y,dst_buffer_size_x,dst_buffer_size_y;
 size_t src_offset_x,src_offset_y,dst_offset_x,dst_offset_y,copy_size_x,copy_size_y;
 struct {int color_type_id;} pixel_format_unique_id;
} esp_async_fbcpy_trans_desc_t;
#define LCD_COLOR_FMT_RGB565 16
static bool copy_available,copy_inflight,copy_corrupt;static int copy_fault;
static unsigned copy_installs,copy_uninstalls,copy_submits;
static uint64_t copy_due,copy_delay_us=2000;
static esp_async_fbcpy_trans_desc_t copy_transaction;
static esp_async_fbcpy_event_callback_t copy_callback;
static int esp_async_fbcpy_install(const esp_async_fbcpy_config_t *c,void **out){(void)c;if(!copy_available)return ESP_FAIL;*out=(void*)4;copy_installs++;return ESP_OK;}
static int esp_async_fbcpy_uninstall(void *h){assert(h==(void*)4&&!copy_inflight);copy_uninstalls++;return ESP_OK;}
static int esp_async_fbcpy(void *h,const esp_async_fbcpy_trans_desc_t *d,esp_async_fbcpy_event_callback_t cb,void *user){
 assert(h==(void*)4&&!user&&(in_worker||!ui_held)&&!copy_inflight&&!ppa_inflight&&!lcd_inflight);
 assert(d->src_offset_x+d->copy_size_x<=d->src_buffer_size_x&&d->src_offset_y+d->copy_size_y<=d->src_buffer_size_y);
 assert(d->dst_offset_x+d->copy_size_x<=d->dst_buffer_size_x&&d->dst_offset_y+d->copy_size_y<=d->dst_buffer_size_y);
 assert(d->pixel_format_unique_id.color_type_id==16);copy_submits++;
 if(!copy_fault||accept_on_error){copy_inflight=true;copy_due=test_now+copy_delay_us;copy_transaction=*d;copy_callback=cb;
  if(model_source){unsigned i=0;while(i<10&&d->src_buffer!=source_cpu[i])i++;assert(i<10);copy_transaction.src_buffer=source_physical[i];}}
 return copy_fault;
}
static void finish_copy(void){
 assert(copy_inflight&&!ppa_inflight&&!lcd_inflight&&(in_worker||!ui_held));esp_async_fbcpy_trans_desc_t *d=&copy_transaction;
 const uint16_t *src=d->src_buffer;uint16_t *dst=d->dst_buffer;
 for(size_t y=0;y<d->copy_size_y;y++)for(size_t x=0;x<d->copy_size_x;x++)dst[(d->dst_offset_y+y)*d->dst_buffer_size_x+d->dst_offset_x+x]=src[(d->src_offset_y+y)*d->src_buffer_size_x+d->src_offset_x+x];
 if(copy_corrupt)dst[d->dst_offset_y*d->dst_buffer_size_x+d->dst_offset_x]^=1;
 copy_inflight=false;copy_callback((void*)4,NULL,NULL);
}
static unsigned rotations,submits,ticks,touches,renders,dirty,loops,loop_limit;
static bool auto_complete;
static bool input_pressed,touch_dirty,stream_dirty;
static unsigned input_begin_loop,input_end_loop,complete_at_loop;
static uint64_t draw_times[256];
static unsigned draw_count;

static jmp_buf stop_loop;
static SemaphoreHandle_t first_mutex;
static bool (*done_cb)(void*,esp_lcd_dpi_panel_event_data_t*,void*);
static bool fails(void){return ++step==fail_at;}
static void *allocated(size_t n){if(fails())return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void released(void *p){if(p){live--;free(p);}}
static SemaphoreHandle_t xSemaphoreCreateMutex(void){SemaphoreHandle_t s=allocated(sizeof(*s));if(s){s->tokens=1;first_mutex=s;}return s;}
static SemaphoreHandle_t xSemaphoreCreateBinary(void){return allocated(sizeof(struct {int tokens;}));}
static void vSemaphoreDelete(SemaphoreHandle_t s){if(s==first_mutex)first_mutex=NULL;released(s);}
static int xSemaphoreTake(SemaphoreHandle_t s,unsigned wait){(void)wait;if(!s->tokens)return 0;s->tokens--;if(s==first_mutex){assert(!in_worker&&!ui_held);ui_held=1;test_now+=injected_lock_us;}return 1;}
static void xSemaphoreGive(SemaphoreHandle_t s){s->tokens=1;if(s==first_mutex){assert(ui_held);ui_held=0;}}
static void xSemaphoreGiveFromISR(SemaphoreHandle_t s,BaseType_t *wake){s->tokens=1;*wake=semaphore_should_wake?pdTRUE:pdFALSE;}
static void *heap_caps_aligned_alloc(size_t align,size_t n,int flags){assert(align==64&&flags==3);return allocated(n);}
typedef struct {unsigned values[2],head,count;} *QueueHandle_t;
static QueueHandle_t xQueueCreate(unsigned n,size_t size){assert(n==2&&size==sizeof(unsigned));return allocated(sizeof(*((QueueHandle_t)0)));}
static int xQueueSend(QueueHandle_t q,const void *p,unsigned wait){assert(q&&!wait);if(q->count==2)return pdFALSE;q->values[(q->head+q->count++)%2]=*(const unsigned*)p;return pdTRUE;}
static int xQueueReceive(QueueHandle_t q,void *p,unsigned wait){assert(q&&!wait);if(!q->count)return pdFALSE;*(unsigned*)p=q->values[q->head];q->head=(q->head+1)%2;q->count--;return pdTRUE;}
static int xQueuePeek(QueueHandle_t q,void *p,unsigned wait){assert(q&&!wait);if(!q->count)return pdFALSE;*(unsigned*)p=q->values[q->head];return pdTRUE;}
static void vQueueDelete(QueueHandle_t q){released(q);}
static TaskHandle_t xTaskGetCurrentTaskHandle(void){return in_worker?(void*)4:(void*)3;}
static void xTaskNotifyGive(TaskHandle_t h){assert(h==(void*)3||h==(void*)4);}
static void vTaskDelete(TaskHandle_t h){assert(h==(void*)3||h==(void*)4);}
static void vTaskNotifyGiveFromISR(TaskHandle_t task,BaseType_t *wake){assert(task==(void*)4);task_notifications++;notify_calls++;*wake=notification_should_wake?pdTRUE:pdFALSE;}
static int64_t esp_timer_get_time(void){return (int64_t)test_now;}
static int bsp_display_new_with_handles(const bsp_display_config_t *c,bsp_lcd_handles_t *h){(void)c;if(fails())return ESP_FAIL;display_live=1;h->panel=(void*)1;return 0;}
static void bsp_display_delete(void){display_live=0;}
static int bsp_feature_enable(int f,bool on){(void)f;(void)on;return 0;}
static int bsp_touch_new(void *c,esp_lcd_touch_handle_t *t){(void)c;*t=allocated(sizeof(**t));if(!*t)return ESP_FAIL;(*t)->io=(void*)2;io_live=1;return 0;}
static int esp_lcd_touch_del(esp_lcd_touch_handle_t t){released(t);return 0;}
static int esp_lcd_panel_io_del(void *p){assert(p==(void*)2);io_live=0;return 0;}
static int esp_lcd_dpi_panel_register_event_callbacks(void *p,const esp_lcd_dpi_panel_event_callbacks_t *cb,void *ctx){(void)p;(void)ctx;if(fails())return ESP_FAIL;done_cb=cb->on_color_trans_done;return 0;}
static int ppa_register_client(const ppa_client_config_t *c,void **p){assert(c->oper_type==1);*p=allocated(1);return *p?0:ESP_FAIL;}
static int ppa_client_register_event_callbacks(void *p,const ppa_event_callbacks_t *c){assert(p);if(fails())return ESP_FAIL;ppa_done_cb=c->on_trans_done;return ESP_OK;}
static int ppa_unregister_client(void *p){assert(!ppa_inflight);released(p);return 0;}
static int esp_lcd_panel_disp_on_off(void *p,bool on){(void)p;assert(on);return fails()?ESP_FAIL:0;}
static bool tab5_panel_ready(void){return !fails();}
static void tab5_panel_observer_reset(void){}
static int brightness_observed=60;
static int bsp_display_brightness_set(int n){brightness_observed=n;assert(!ui_held&&n>=0&&n<=100);return fails()?ESP_FAIL:0;}
static int xTaskCreatePinnedToCore(void (*fn)(void*),const char *name,unsigned stack,void *a,unsigned priority,TaskHandle_t *h,int core){
 (void)fn;(void)a;assert(stack==8192&&core==1&&h);assert(priority==4||priority==5);
 if(fails())return 0;*h=!strcmp(name,"native_gpu")?(void*)4:(void*)3;return pdPASS;
}

bool nui_native_init(int w,int h,uint16_t *p){assert(w==1280&&h==720&&p);renderer_live=1;mock_frame=p;pending_count=0;pending_add((nui_area_t){0,0,1279,719});return !fails();}
bool nui_native_bind_framebuffer(uint16_t *p){assert(ui_held&&!in_worker&&p);mock_frame=p;return true;}
void nui_native_deinit(void){renderer_live=0;}
void nui_native_set_clock_cb(uint64_t (*cb)(void)){assert(cb);}
void tab5_ui_init(tab5_ui_action_cb_t cb,void *p){(void)cb;(void)p;}
void tab5_ui_destroy(void){}
static int bench_position,bench_restored_position,bench_restore_calls,bench_moves;
static bool mock_cache_enabled;
static unsigned mock_cache_clears;
bool tab5_ui_scroll_benchmark_begin(tab5_ui_scroll_benchmark_state_t *saved,int *maximum){
 assert(ui_held);*saved=(tab5_ui_scroll_benchmark_state_t){.valid=true,.settings_offset=137,.page=2};*maximum=950;bench_position=0;dirty=1;pending_add((nui_area_t){0,0,1279,719});return true;
}
void tab5_ui_scroll_benchmark_position(int offset){assert(ui_held);bench_position=offset;bench_moves++;dirty=1;pending_add((nui_area_t){32,32,287,223});}
void tab5_ui_scroll_benchmark_end(tab5_ui_scroll_benchmark_state_t *saved){assert(ui_held);bench_restored_position=saved->settings_offset;saved->valid=false;bench_restore_calls++;dirty=1;pending_add((nui_area_t){0,0,1279,719});}
static bool mock_cache_get(void){return mock_cache_enabled;}
static void mock_cache_set(bool enabled){assert(ui_held);mock_cache_enabled=enabled;}
static void mock_cache_clear(void){assert(ui_held);mock_cache_clears++;}
bool tab5_ui_scroll_cache_enabled(void){return mock_cache_get();}
void tab5_ui_set_scroll_cache_enabled(bool enabled){mock_cache_set(enabled);}
void tab5_ui_clear_scroll_cache(void){mock_cache_clear();}


void nui_native_tick(uint32_t ms){(void)ms;assert(ui_held&&!in_worker);ticks++;test_now+=injected_tick_us;}
void nui_native_touch(int x,int y,bool p){(void)x;(void)y;assert(ui_held&&!in_worker);touches++;test_now+=injected_input_us;if(ppa_inflight)touches_while_ppa++;if(lcd_inflight)touches_while_lcd++;if(touch_dirty&&(p||(input_end_loop&&loops==input_end_loop)))dirty=1;}
static void (*render_hook)(void);
size_t nui_native_render(void){
 assert(ui_held&&!in_worker);renders++;test_dirty_count=pending_count;memcpy(test_dirty_areas,pending_areas,pending_count*sizeof(*pending_areas));pending_count=0;
 if(test_dirty_count&&render_hook)render_hook();return test_dirty_count;
}
void nui_native_invalidate(const nui_area_t *a){assert(ui_held&&!in_worker&&a);pending_add(*a);}

static int esp_lcd_touch_read_data(void *p){(void)p;assert(!ui_held);if(physical_reads)assert(test_now-physical_last_start>=10000);physical_reads++;physical_last_start=test_now;if(advance_hook)advance_hook(injected_touch_us);else test_now+=injected_touch_us;return 0;}
static bool esp_lcd_touch_get_coordinates(void *p,uint16_t *x,uint16_t *y,uint16_t *s,uint8_t *n,unsigned cap){(void)p;assert(cap==1);if(!input_pressed)return false;*x=100;*y=100;*s=1;*n=1;return true;}
static void finish_ppa(void){
 assert(ppa_inflight&&(in_worker||!ui_held));const ppa_srm_oper_config_t *c=&last_op;
 if(emulate_pixels){const uint16_t *src=c->in.buffer;uint16_t *dst=(uint16_t*)c->out.buffer;
  for(unsigned y=0;y<(unsigned)c->in.block_h;y++)for(unsigned x=0;x<(unsigned)c->in.block_w;x++)
   dst[(c->out.block_offset_y+c->in.block_w-1-x)*c->out.pic_w+c->out.block_offset_x+y]=src[(c->in.block_offset_y+y)*1280+c->in.block_offset_x+x];
  if(corrupt_ppa)dst[0]^=1;
 }
 ppa_inflight=false;assert(ppa_done_cb);ppa_done_cb(NULL,NULL,NULL);
}
static void finish_lcd(void){
 assert(lcd_inflight&&!ppa_inflight&&(in_worker||!ui_held));
 if(emulate_pixels)for(int y=0;y<lcd_h;y++)for(int x=0;x<lcd_w;x++)
  driver_fb[(lcd_y+y)*720+lcd_x+x]=lcd_source[y*lcd_w+x];
 if(pixel_hook)pixel_hook();
 if(record_trace&&trace_count<120&&position_hook&&position_hook()>=0)completed_trace[trace_mode][trace_count++]=position_hook();
 lcd_inflight=false;assert(done_cb);done_cb(NULL,NULL,NULL);
}
static int ppa_do_scale_rotate_mirror(void *p,const ppa_srm_oper_config_t *c){
 assert(p&&(in_worker||!ui_held)&&!copy_inflight&&!ppa_inflight&&!lcd_inflight&&c->mode==PPA_TRANS_MODE_NON_BLOCKING);
 assert(c->in.pic_w==1280&&c->in.pic_h==720&&c->rotation_angle==1);
 assert((c->out.pic_w==c->in.block_h&&c->out.pic_h==c->in.block_w)||(c->out.pic_w==720&&c->out.pic_h==1280));
 assert(((unsigned)c->out.pic_w*c->out.pic_h*2u)%64==0);
 last_op=*c;rotations++;
 if(!ppa_fault||accept_on_error){ppa_inflight=true;ppa_due=test_now+ppa_delay_us;if(auto_ppa_complete&&!ppa_delay_us)finish_ppa();}
 return ppa_fault;
}
static int esp_lcd_panel_draw_bitmap(void *p,int x,int y,int w,int h,void *buf){
 assert(p&&buf&&(in_worker||!ui_held)&&!copy_inflight&&!ppa_inflight&&!lcd_inflight&&x>=0&&y>=0&&w>x&&h>y&&w<=720&&h<=1280);
 lcd_x=x;lcd_y=y;lcd_w=w-x;lcd_h=h-y;lcd_source=model_flush_lcd(buf,(size_t)(w-x)*(h-y)*2);
 assert((lcd_w==last_op.out.pic_w&&lcd_h==last_op.out.pic_h)||lcd_w==720);
 lcd_bytes+=(uint64_t)lcd_w*lcd_h*2;submits++;
 if(!lcd_fault||accept_on_error){lcd_inflight=true;lcd_due=test_now+lcd_delay_us;}
 return lcd_fault;
}
static void vTaskDelay(unsigned ms){
 assert(in_worker||!ui_held);test_now+=(uint64_t)ms*1000;loops++;
 if(copy_inflight&&test_now>=copy_due)finish_copy();
 if(ppa_inflight&&auto_ppa_complete&&test_now>=ppa_due)finish_ppa();
 if(lcd_inflight&&((auto_complete&&test_now>=lcd_due)||loops==complete_at_loop))finish_lcd();
 if(input_begin_loop&&loops==input_begin_loop)input_pressed=true;
 if(input_end_loop&&loops==input_end_loop)input_pressed=false;
 if(stream_dirty)dirty=1;
 if(loops>=loop_limit)longjmp(stop_loop,1);
}

static uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t ticks){
 assert(clear==pdTRUE&&ticks>=1&&ticks<=10&&!ui_held);
 if(task_notifications){unsigned count=task_notifications;task_notifications=0;notify_immediates++;return count;}
 notify_waits++;uint64_t target=test_now+(uint64_t)ticks*1000;
 if(copy_inflight&&copy_due<target)target=copy_due;
 if(ppa_inflight&&auto_ppa_complete&&ppa_due<target)target=ppa_due;
 if(lcd_inflight&&auto_complete&&lcd_due<target)target=lcd_due;
 if(target==test_now){
  if(copy_inflight&&test_now>=copy_due)finish_copy();
  if(ppa_inflight&&auto_ppa_complete&&test_now>=ppa_due)finish_ppa();
  if(lcd_inflight&&auto_complete&&test_now>=lcd_due)finish_lcd();
 }else {
  /* Deterministic event clock. Delay helper also advances scripted input. */
  unsigned ms=(unsigned)((target-test_now+999)/1000);vTaskDelay(ms);
 }
 unsigned count=task_notifications;task_notifications=0;return count;
}

#define free released
'''
TEST = r'''

#undef free
static _Alignas(64) uint16_t scene[1280*720],expected_slot[2][1280*720];
static _Alignas(64) uint16_t cached_land[256*256],cached_portrait[256*256];
static uint64_t render_cost_us=20000;
static int scene_position,last_scene_position=-1;
static unsigned full_renders,partial_renders,pixel_frames,render_while_dma;
static bool paint_defer=true,inject_render_overlap=true,force_no_copy,odd_view;
static void run_worker(void){bool old=in_worker;in_worker=true;worker_step();in_worker=old;}
static void advance_sim(uint64_t duration){
 uint64_t target=test_now+duration;run_worker();
 for(unsigned guard=0;guard<10000;guard++){
  uint64_t next=target+1;
  if(copy_inflight&&copy_due<next)next=copy_due;
  if(ppa_inflight&&auto_ppa_complete&&ppa_due<next)next=ppa_due;
  if(lcd_inflight&&auto_complete&&lcd_due<next)next=lcd_due;
  if(next>target)break;
  if(next>test_now)test_now=next;
  bool old=in_worker;in_worker=true;
  if(copy_inflight&&test_now>=copy_due)finish_copy();
  if(ppa_inflight&&auto_ppa_complete&&test_now>=ppa_due)finish_ppa();
  if(lcd_inflight&&auto_complete&&test_now>=lcd_due)finish_lcd();
  worker_step();in_worker=old;
 }
 if(test_now<target)test_now=target;
}
static int frame_position(void){return active_slot>=0&&slots[active_slot].benchmark_request&&slot_benchmark_valid(&slots[active_slot])?slots[active_slot].benchmark_position:-1;}
static void check_pixels(void){
 assert(active_slot>=0);const uint16_t *ref=expected_slot[active_slot];
 for(unsigned y=0;y<720;y++)for(unsigned x=0;x<1280;x++)assert(driver_fb[(1279-x)*720+y]==ref[y*1280+x]);
 pixel_frames++;
}
static nui_surface_view_t scene_view(unsigned index,int position){
 return (nui_surface_view_t){.landscape=cached_land,.portrait=cached_portrait,.landscape_stride=256,.portrait_stride=256,
 .logical_width=256,.logical_height=256,.source_x=odd_view?1u:0u,.source_y=odd_view?1u:(unsigned)(position/16)%64,
 .destination=odd_view?(nui_area_t){65,65,67,67}:(nui_area_t){64,64,255,191},.storage_id=42,.portrait_generation=1,.lease=(void*)(uintptr_t)(index+5)};
}
static void paint_scene(void){
 assert(!in_worker&&ui_held);unsigned index=mock_frame==slots[0].pixels?0:1;assert(mock_frame==slots[index].pixels);
 if(active_slot>=0){assert((int)index!=active_slot);render_while_dma++;}
 int position=benchmark_running()?bench_position:scene_position;
 if(position!=last_scene_position){
  for(int y=32;y<=223;y++)for(int x=32;x<=287;x++)scene[y*1280+x]=(uint16_t)(position*17+x*3+y*11);
  nui_surface_view_t v=scene_view(index,position);
  for(int y=v.destination.y1;y<=v.destination.y2;y++)for(int x=v.destination.x1;x<=v.destination.x2;x++)scene[y*1280+x]=cached_land[(y-v.destination.y1+v.source_y)*256+x-v.destination.x1+v.source_x];
  last_scene_position=position;
 }
 memcpy(expected_slot[index],scene,FRAME_BYTES);
 bool full=test_dirty_count==1&&test_dirty_areas[0].x1==0&&test_dirty_areas[0].y1==0&&test_dirty_areas[0].x2==1279&&test_dirty_areas[0].y2==719;
 if(full)full_renders++;else partial_renders++;
 for(size_t i=0;i<test_dirty_count;i++)for(int y=test_dirty_areas[i].y1;y<=test_dirty_areas[i].y2;y++)
  memcpy(mock_frame+y*1280+test_dirty_areas[i].x1,scene+y*1280+test_dirty_areas[i].x1,(test_dirty_areas[i].x2-test_dirty_areas[i].x1+1)*2);
 if(paint_defer&&defer_cb){
  nui_surface_view_t v=scene_view(index,position);bool covered=false;
  for(size_t i=0;i<test_dirty_count;i++)if(area_contains(test_dirty_areas[i],v.destination))covered=true;
  if(covered){pins++;if(defer_cb(&v,defer_user)){
    for(int y=v.destination.y1;y<=v.destination.y2;y++)for(int x=v.destination.x1;x<=v.destination.x2;x++)mock_frame[y*1280+x]=0xdead;
   }else pins--;}
 }
 if(inject_render_overlap)advance_sim(render_cost_us);else test_now+=render_cost_us;
}
static void reap(void){assert(!ui_held&&!in_worker);task_ui_lock();ui_reap_locked();xSemaphoreGive(ui_mutex);}
static void cleanup_runtime(void){
 benchmark_report.active=benchmark_report.pending=false;auto_complete=auto_ppa_complete=true;
 ppa_fault=lcd_fault=copy_fault=0;copy_cache_fault=0;accept_on_error=false;
 for(unsigned n=0;n<1000&&!transport_idle();n++){advance_sim(10000);reap();}
 assert(transport_idle()&&active_slot<0&&!pins&&!copy_inflight&&!ppa_inflight&&!lcd_inflight);
 __atomic_store_n(&native_task_handle,NULL,__ATOMIC_RELEASE);__atomic_store_n(&transfer_task_handle,NULL,__ATOMIC_RELEASE);
 startup_cleanup(true,true);started=false;assert(!live&&!display_live&&!io_live&&!renderer_live);
}
static void fresh_runtime(void){
 if(started)cleanup_runtime();
 fail_at=step=0;stats=(tab5_native_port_stats_t){0};pipeline=(__typeof__(pipeline)){0};active_slot=-1;
 recovery_epoch=fault_epoch=ready_count=0;worker_fault=false;gpu_started_at=gpu_finished_at=0;
 reference_slot=-1;verify_requested=verify_queued=verify_epoch=0;
 benchmark_report=(tab5_native_scroll_benchmark_status_t){0};benchmark_saved_active=false;benchmark=(__typeof__(benchmark)){0};
 task_notifications=0;copy_available=!force_no_copy;copy_delay_us=1000;ppa_delay_us=2000;lcd_delay_us=4000;
 auto_complete=auto_ppa_complete=true;emulate_pixels=true;input_pressed=touch_dirty=false;
 physical_reads=0;test_now=1000000;injected_touch_us=6000;injected_tick_us=injected_input_us=injected_lock_us=0;
 full_renders=partial_renders=pixel_frames=render_while_dma=0;materializations=releases=pins=0;
 scene_position=0;last_scene_position=-1;odd_view=false;paint_defer=true;inject_render_overlap=true;render_cost_us=20000;
 for(unsigned y=0;y<720;y++)for(unsigned x=0;x<1280;x++)scene[y*1280+x]=(uint16_t)(x*7+y*13);
 for(unsigned y=0;y<256;y++)for(unsigned x=0;x<256;x++){cached_land[y*256+x]=(uint16_t)(x*23+y*19);cached_portrait[(255-x)*256+y]=cached_land[y*256+x];}
 memset(driver_fb,0,sizeof(driver_fb));advance_hook=NULL;pixel_hook=NULL;render_hook=NULL;position_hook=frame_position;
 assert(tab5_native_port_start(NULL,NULL)==ESP_OK&&started&&stats.worker_enabled);
 __atomic_store_n(&native_task_handle,(void*)3,__ATOMIC_RELEASE);__atomic_store_n(&transfer_task_handle,(void*)4,__ATOMIC_RELEASE);
 producer_reset();render_hook=paint_scene;advance_hook=advance_sim;pixel_hook=check_pixels;
}
static void ui_step(void){assert(!in_worker&&!ui_held);producer_step();}
static void request_scene(int value,nui_area_t a){producer.input_requested=true;producer.input_since=test_now;scene_position=value;task_ui_lock();nui_native_invalidate(&a);xSemaphoreGive(ui_mutex);}
static void startup_proof(void){
 unsigned successful_steps=0;
 for(int fault=1;fault<=40;fault++){
  fail_at=fault;step=0;copy_available=true;pending_count=0;stats=(tab5_native_port_stats_t){0};
  int result=tab5_native_port_start(NULL,NULL);
  if(result==ESP_OK){successful_steps=(unsigned)step;started=false;startup_cleanup(true,true);assert(!live);break;}
  assert(!live&&!frame&&!rotated&&!ui_mutex&&!ready_frames&&!completed_frames&&!started);
 }
 assert(successful_steps>=18);fail_at=step=0;
 puts("Two-task startup PASS: all injected allocations/queues/second-slot/two-task creation failures clean up before start gate; zero DMA before started; no firmware build");
}
static void overlap_and_benchmark(void){
 fresh_runtime();record_trace=true;trace_mode=0;trace_count=0;bench_moves=0;mock_cache_enabled=true;
 assert(tab5_native_port_request_scroll_benchmark(120,16667,true));
 for(unsigned n=0;n<5000;n++){
  ui_step();run_worker();advance_sim(1000);
  if(benchmark_report.finished&&benchmark_report.restored)break;
 }
 record_trace=false;
 tab5_native_scroll_benchmark_status_t report;tab5_native_port_scroll_benchmark_status(&report);
 assert(report.finished&&report.restored&&report.reason==1&&report.accepted_frames==120&&report.completed_frames==120&&trace_count==120);
 assert(completed_trace[0][0]==0&&completed_trace[0][60]==950&&completed_trace[0][119]==6&&bench_moves==119);
 assert(stats.overlap_renders>0&&stats.overlap_us>0&&render_while_dma>0&&stats.queued_peak<=2&&stats.queued_peak>0);
 assert(full_renders<8&&partial_renders>100&&!materializations);
 assert(stats.compose_source_sync_jobs==1&&stats.compose_source_sync_hits>=119&&stats.compose_source_sync_bytes==sizeof(cached_portrait)); /* no FULL debt propagation */
 assert(stats.worker_completed_frames>=120&&report.lcd_copy_bytes>0&&report.lcd_copy_bytes<=120u*FRAME_BYTES);
 assert(stats.callback_process_delay_us==0); /* I2C advances worker while producer blocked. */
 uint32_t frames=report.completed_frames;uint64_t bytes=report.lcd_copy_bytes;
 for(unsigned n=0;n<5;n++){ui_step();advance_sim(10000);}tab5_native_port_scroll_benchmark_status(&report);
 assert(report.completed_frames==frames&&report.lcd_copy_bytes==bytes);
 printf("Two-task overlap PASS:120 identical accepted/completed Settings steps,FIFO,both CPU slots,queued_peak=%u,overlap_renders=%u,overlap_us=%llu; I2C6ms cannot stall GPU,callbackdelay0; bounded debt, normal materialize0\n",stats.queued_peak,stats.overlap_renders,(unsigned long long)stats.overlap_us);
}
static void references_and_lifetimes(void){
 fresh_runtime();assert(tab5_native_port_request_verify(2));ui_step();
 assert(slot_state(0)==SLOT_READY&&slots[0].reference_ready&&materializations==1&&full_renders==1);
 run_worker();assert(active_slot==0);
 request_scene(16,(nui_area_t){32,32,287,223});advance_sim(1);ui_step();
 assert(slots[1].reference_ready&&materializations==2&&partial_renders==1&&full_renders==1);
 /* Renderer now points at B; worker reference A must remain captured A. */
 assert(mock_frame==slots[1].pixels&&frame!=mock_frame);
 for(unsigned n=0;n<1000&&(stats.verify_checks<2||!transport_idle());n++){advance_sim(1000);reap();}
 assert(stats.verify_checks==2&&!stats.verify_errors&&!stats.verify_mismatches&&!pins&&verify_requested==0);
 puts("Two-slot verifier PASS:first FULL A+independent LANDSCAPE materialization,diagnostic CPU baseline A->B then actual PARTIAL B; renderer rebound while worker compares captured slot; both full retained FB checks exact; release only by UI after LCD");
}
static void backpressure_and_late_error(void){
 fresh_runtime();copy_fault=ESP_FAIL;accept_on_error=true;copy_delay_us=2000000;
 ui_step(); /* ready A */
 request_scene(16,(nui_area_t){32,32,287,223});
 inject_render_overlap=false;advance_hook=NULL;test_now+=20000;ui_step(); /* ready B before worker observes fault */
 assert(slot_state(0)==SLOT_READY&&slot_state(1)==SLOT_READY&&pins==2);
 unsigned render_count=stats.renders;test_now+=100000;ui_step();assert(stats.renders==render_count&&stats.producer_backpressure>0);
 advance_hook=advance_sim;run_worker();assert(active_slot==0&&copy_inflight&&worker_fault);
 run_worker();assert(slot_state(1)==SLOT_DONE&&slot_state(0)==SLOT_ACTIVE&&pins==2);
 reap();assert(pins==1&&slot_state(1)==SLOT_FREE&&slot_state(0)==SLOT_ACTIVE);
 test_now+=1100000;run_worker();assert(pipeline.timed_out&&pins==1);
 unsigned held_live=live;startup_cleanup(true,true);assert((unsigned)live==held_live&&frame&&rotated);
 render_count=stats.renders;ui_step();assert(stats.renders==render_count&&pins==1);
 copy_fault=0;accept_on_error=false;
 bool old=in_worker;in_worker=true;finish_copy();worker_step();in_worker=old;
 assert(slot_state(0)==SLOT_DONE&&pins==1);reap();assert(!pins&&!worker_fault);
 inject_render_overlap=true;copy_delay_us=1000;test_now+=100000;ui_step();assert(collecting==NULL);
 unsigned index=slot_state(0)==SLOT_READY?0:1;assert(slots[index].plan.dirty_count==1&&slots[index].plan.dirty[0].x1==0&&slots[index].plan.dirty[0].x2==1279);
 advance_sim(100000);reap();
 puts("Two-slot backpressure/error PASS: strict2 slots, third render blocked; unknown DMA retains ACTIVE pixels/leases, queued stale plan dropped+UI-only release, timeout cannot free; late EOF releases via completion, recovery generation forces FULL");
}
static void cancel_proof(void){
 fresh_runtime();assert(tab5_native_port_request_scroll_benchmark(120,16667,true));ui_step();run_worker();
 request_scene(16,(nui_area_t){32,32,287,223});test_now+=17000;ui_step();
 uint32_t before=benchmark_report.completed_frames;uint64_t bytes=benchmark_report.lcd_copy_bytes;
 input_pressed=true;advance_sim(10000);unsigned touches_before=touches;ui_step();
 assert(benchmark_report.finished&&benchmark_report.reason==2&&benchmark.suppress_touch&&touches==touches_before);
 before=benchmark_report.completed_frames;bytes=benchmark_report.lcd_copy_bytes;
 for(unsigned n=0;n<10;n++)advance_sim(10000);reap();
 assert(benchmark_report.completed_frames==before&&benchmark_report.lcd_copy_bytes==bytes);
 input_pressed=false;advance_sim(10000);touches_before=touches;ui_step();
 assert(!benchmark.suppress_touch&&benchmark_report.restored&&touches==touches_before);
 fresh_runtime();assert(tab5_native_port_request_scroll_benchmark(120,16667,true));ui_step();
 task_ui_lock();assert(benchmark_control(false,true));xSemaphoreGive(ui_mutex);assert(benchmark_report.reason==2);
 advance_sim(100000);reap();
 puts("Two-slot cancellation PASS: queued stale benchmark frame discarded, active frame EOF never recounted after abort; physical press+release swallowed across page restoration, encoder cancel activates no controls");
}

static void known_failures_and_fallback(void){
 for(unsigned which=0;which<2;which++){
  fresh_runtime();inject_render_overlap=false;advance_hook=NULL;ui_step();
  if(which)lcd_fault=ESP_ERR_INVALID_ARG;else ppa_fault=ESP_ERR_INVALID_ARG;
  run_worker();for(unsigned n=0;n<100&&active_slot>=0;n++)advance_sim(1000);
  assert(active_slot<0&&pins==1&&stats.submit_errors==1&&recovery_epoch>0);
  reap();assert(!pins);ppa_fault=lcd_fault=0;advance_hook=advance_sim;inject_render_overlap=true;
  test_now+=100000;ui_step();unsigned index=slot_state(0)==SLOT_READY?0:1;
  assert(slots[index].plan.dirty_count==1&&slots[index].plan.dirty[0].x1==0&&slots[index].plan.dirty[0].x2==1279);
  advance_sim(100000);reap();
 }
 fresh_runtime();tab5_native_port_set_cpu_tiles_enabled(true);assert(tab5_native_port_request_scroll_benchmark(8,16667,true));
 for(unsigned n=0;n<1000&&!benchmark_report.restored;n++){ui_step();advance_sim(1000);}
 assert(benchmark_report.completed_frames==8&&stats.compose_cpu_jobs>0&&pixel_frames>=8);
 tab5_native_port_set_cpu_tiles_enabled(false);
 force_no_copy=true;fresh_runtime();assert(!composer_available&&!compose.copy);assert(tab5_native_port_request_scroll_benchmark(4,16667,true));
 for(unsigned n=0;n<1000&&!benchmark_report.restored;n++){ui_step();advance_sim(1000);}
 assert(benchmark_report.completed_frames==4&&!stats.compose_copy_jobs&&stats.compose_ppa_jobs>0&&!pins);
 force_no_copy=false;
 puts("Two-task faults/fallback PASS: known PPA and LCD reject safely complete+UI release+FULL generation recovery; CPUtile opt-in exact on both slots; DMA2D install unavailable falls back to canonical PPA without crash");
}
static void immutable_flush_proof(void){
 fresh_runtime();source_flush_reset();model_source=true;source_sync_calls=0;source_sync_fault=0;
 nui_surface_view_t v={.landscape=source_cpu[0],.portrait=source_cpu[0],.logical_width=64,.logical_height=64,.landscape_stride=64,.portrait_stride=64,.storage_id=1,.portrait_generation=1};
 for(unsigned j=0;j<64*64;j++)source_cpu[0][j]=(uint16_t)(j*17+1);
 in_worker=true;assert(compose_flush_source(&v)==ESP_OK&&source_sync_calls==1&&!memcmp(source_cpu[0],source_physical[0],8192));
 v.source_y=32;assert(compose_flush_source(&v)==ESP_OK&&source_sync_calls==1);
 v.portrait_generation++;source_cpu[0][100]^=1;assert(compose_flush_source(&v)==ESP_OK&&source_sync_calls==2);
 v.storage_id++;assert(compose_flush_source(&v)==ESP_OK&&source_sync_calls==3);
 v.portrait_generation++;source_sync_fault=ESP_ERR_NO_MEM;assert(compose_flush_source(&v)==ESP_ERR_NO_MEM);
 source_sync_fault=0;assert(compose_flush_source(&v)==ESP_OK&&source_sync_calls==5);
 source_flush_reset();for(unsigned i=0;i<9;i++){v.portrait=source_cpu[i];v.storage_id=10+i;v.portrait_generation=1;assert(compose_flush_source(&v)==ESP_OK);}
 unsigned before=source_sync_calls;v.portrait=source_cpu[0];v.storage_id=10;assert(compose_flush_source(&v)==ESP_OK&&source_sync_calls==before+1);
 in_worker=false;model_source=false;
 puts("Worker immutable source PASS: full payload flush -> physical DMA visibility, source-offset hit, generation/ID/address changes, OOM retry uncached and 8-entry eviction reflush; no UI lock/renderer on worker path");
}

static void odd_roi_proof(void){
 fresh_runtime();odd_view=true;ui_step();assert(slot_state(0)==SLOT_READY&&slots[0].plan.view_count==1&&stats.compose_unaligned_views==1);
 for(unsigned i=0;i<1280*720;i++)rotated[i]=0x5aa5;
 run_worker();assert(copy_inflight);bool old=in_worker;in_worker=true;finish_copy();in_worker=old;
 for(unsigned y=0;y<1280;y++)for(unsigned x=0;x<720;x++){
  uint16_t expected=0x5aa5;
  if(x>=65&&x<=67&&y>=1212&&y<=1214)expected=cached_portrait[(252+y-1212)*256+1+x-65];
  assert(rotated[y*720+x]==expected);
 }
 run_worker();advance_sim(100000);reap();assert(pixel_frames==1&&!pins);
 assert(stats.compose_copy_jobs==1&&stats.compose_copy_bytes==18&&stats.compose_ppa_jobs==4);
 puts("Odd ROI PASS:3x3 destination65,65/source1,1 -> canonical65,1212; exact9 GPU pixels, all neighbors unchanged before PPA, CPU complement excludes poisoned9 pixels, complete retained frame exact; dirty alignment16 unchanged");
}
static void backlight_output_proof(void){
 fresh_runtime();assert(tab5_native_port_backlight_configure(300,37));ui_step();assert(brightness_observed==37);
 test_now+=UINT64_C(301000000);ui_step();assert(brightness_observed==0&&backlight.asleep);
 assert(tab5_native_port_backlight_configure(0,37));ui_step();assert(brightness_observed==37&&!backlight.asleep);
 puts("Backlight actual producer BSP output PASS: manual37 -> off0 -> restore37; writes outside UI mutex");
 cleanup_runtime();
}
int main(void){
 backlight_output_proof();startup_proof();overlap_and_benchmark();references_and_lifetimes();backpressure_and_late_error();cancel_proof();known_failures_and_fallback();immutable_flush_proof();odd_roi_proof();cleanup_runtime();
 puts("Actual1717 production producer/worker steps PASS: worker executes no renderer/UI mutex, exactly one canonical/GPU/LCD owner, two immutable FIFO CPU plans and bounded lifetime");return 0;
}

'''

QUEUE_PROOF = r"""
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include "tab5_native_port.h"
void tab5_ui_encoder_wake(uint32_t ms){(void)ms;}
static unsigned samples,downs,ups,encoders,idles;
static int32_t last_delta;
void tab5_ui_touch(int32_t x,int32_t y,bool pressed,uint32_t ms){(void)ms;assert(x>=0&&y>=0);samples++;if(pressed)downs++;else ups++;}
void tab5_ui_input(tab5_ui_input_event_t kind,int32_t delta,uint32_t ms){last_delta=delta;(void)ms;if(kind==TAB5_UI_INPUT_IDLE)idles++;else encoders++;}
"""
QUEUE_TEST = r"""
int main(void){
 tab5_native_port_input_reset();assert(tab5_native_port_post_touch(20,30,true,0));
 assert(input_dispatch(0,false));assert(samples==1&&downs==1);
 assert(!input_dispatch(3,false));assert(!input_dispatch(7,false));assert(samples==1&&idles==3);
 assert(tab5_native_port_post_input(TAB5_UI_INPUT_ROT,1,8));assert(input_dispatch(8,false));assert(encoders==1&&samples==1);
 assert(tab5_native_port_post_touch(20,30,false,10));assert(input_dispatch(10,false));assert(samples==2&&ups==1);
 assert(!input_dispatch(11,false));assert(samples==2);
 /* Existing external dispatch contract still services retained idle touch. */
 assert(!tab5_native_port_input_dispatch(20));assert(samples==3&&ups==2);
 return 0;
}
"""

def main():
    source = PORT.read_text(encoding='utf-8')
    source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
    with tempfile.TemporaryDirectory(prefix='native-port-host-') as directory:
        p = Path(directory)
        (p/'test.c').write_text(STUB+source+TEST, encoding='utf-8')
        subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-misleading-indentation',
                        '-I'+str(ROOT/'tab5/main'),str(p/'test.c'),str(ROOT/'tab5/main/tab5_backlight.c'),'-o',str(p/'test.exe')],check=True)
        subprocess.run([str(p/'test.exe')],check=True)
        (p/'queue.c').write_text(QUEUE_PROOF+source+QUEUE_TEST, encoding='utf-8')
        subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-misleading-indentation',
                        '-DTAB5_NATIVE_INPUT_ONLY=1','-I'+str(ROOT/'tab5/main'),
                        str(p/'queue.c'),str(ROOT/'tab5/main/tab5_backlight.c'),'-o',str(p/'queue.exe')],check=True)
        subprocess.run([str(p/'queue.exe')],check=True)
        print('Actual FIFO fresh-only PASS: no duplicate pressed/release on3/7ms wake; encoder+idle dispatch retained, public legacy dispatch unchanged')


if __name__ == '__main__':
    main()
