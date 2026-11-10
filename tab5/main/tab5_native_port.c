#include "tab5_native_port.h"
#if 1
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#else
#include <stdatomic.h>
#endif
typedef struct {
    tab5_ui_input_event_t kind;
    int32_t delta;
    uint32_t ms;
    int x,y;
    bool pressed,raw_button;
} native_input_t;
static struct {
    native_input_t events[TAB5_NATIVE_INPUT_CAPACITY];
    unsigned head,count;
    bool open;
    tab5_native_input_stats_t stats;
} input_queue;
static int input_x,input_y;
static bool input_pressed;
#ifdef ESP_PLATFORM
static portMUX_TYPE input_mux=portMUX_INITIALIZER_UNLOCKED;
#define INPUT_LOCK() portENTER_CRITICAL(&input_mux)
#define INPUT_UNLOCK() portEXIT_CRITICAL(&input_mux)
#else
static atomic_flag input_mux=ATOMIC_FLAG_INIT;
#define INPUT_LOCK() do {while(atomic_flag_test_and_set_explicit(&input_mux,memory_order_acquire)) {}} while(0)
#define INPUT_UNLOCK() atomic_flag_clear_explicit(&input_mux,memory_order_release)
#endif
static tab5_backlight_t backlight;
static struct {uint32_t timeout;uint8_t manual;bool pending;} light_request;
static uint8_t light_manual=60;
#ifndef TAB5_NATIVE_INPUT_ONLY
static int light_applied=60;
#endif
static uint64_t light_now;
static uint32_t light_low;
static bool light_clock_ready,button_down,button_swallow,button_long;
static uint32_t button_at;
bool tab5_native_port_backlight_configure(uint32_t timeout_s,uint8_t manual_pct){
    if(!tab5_backlight_timeout_valid(timeout_s)||manual_pct<10||manual_pct>100)return false;
    INPUT_LOCK();light_request.timeout=timeout_s;light_request.manual=manual_pct;light_request.pending=true;INPUT_UNLOCK();return true;
}
static uint64_t light_extend(uint32_t ms){
    if(!light_clock_ready){light_low=ms;light_now=ms;light_clock_ready=true;}
    else {uint32_t delta=ms-light_low;if(delta<UINT32_C(0x80000000)){light_now+=delta;light_low=ms;}}
    return light_now;
}
static void light_begin(uint32_t ms){
    uint64_t now=light_extend(ms);uint32_t timeout=0;bool pending;
    INPUT_LOCK();pending=light_request.pending;if(pending){timeout=light_request.timeout;light_manual=light_request.manual;light_request.pending=false;}INPUT_UNLOCK();
    if(pending)tab5_backlight_configure(&backlight,timeout,now);
}
void tab5_native_port_input_reset(void) {
    INPUT_LOCK();input_queue.head=input_queue.count=0;
    input_queue.stats=(tab5_native_input_stats_t){0};input_queue.open=true;
    INPUT_UNLOCK();input_x=input_y=0;input_pressed=false;
    tab5_backlight_init(&backlight,0);light_clock_ready=false;button_down=button_swallow=button_long=false;light_now=0;light_low=0;
}
void tab5_native_port_input_close(void) {
    INPUT_LOCK();input_queue.open=false;input_queue.head=input_queue.count=0;INPUT_UNLOCK();
}
static bool input_post(native_input_t e) {
    bool valid=e.kind>=TAB5_UI_INPUT_ROT&&e.kind<=TAB5_UI_INPUT_TOUCH;
    valid=valid&&(e.kind!=TAB5_UI_INPUT_ROT||e.delta!=0);
    valid=valid&&(e.kind!=TAB5_UI_INPUT_TOUCH||(e.x>=0&&e.x<1280&&e.y>=0&&e.y<720));
    INPUT_LOCK();
    bool accepted=valid&&input_queue.open&&input_queue.count<TAB5_NATIVE_INPUT_CAPACITY;
    if(accepted){
        input_queue.events[(input_queue.head+input_queue.count)%TAB5_NATIVE_INPUT_CAPACITY]=e;
        input_queue.count++;input_queue.stats.posted++;
    }else input_queue.stats.rejected++;
    INPUT_UNLOCK();return accepted;
}
bool tab5_native_port_post_input(tab5_ui_input_event_t kind,int32_t delta,uint32_t ms) {
    if(kind==TAB5_UI_INPUT_TOUCH){
        INPUT_LOCK();input_queue.stats.rejected++;INPUT_UNLOCK();return false;
    }
    return input_post((native_input_t){.kind=kind,.delta=delta,.ms=ms});
}
bool tab5_native_port_post_button(bool down,uint32_t ms){
    return input_post((native_input_t){.kind=TAB5_UI_INPUT_PRESS,.ms=ms,.raw_button=true,.pressed=down});
}
bool tab5_native_port_post_touch(int x,int y,bool pressed,uint32_t ms) {
    return input_post((native_input_t){.kind=TAB5_UI_INPUT_TOUCH,.ms=ms,.x=x,.y=y,.pressed=pressed});
}
void tab5_native_port_input_stats(tab5_native_input_stats_t *out) {
    if(!out)return;
    INPUT_LOCK();*out=input_queue.stats;out->pending=input_queue.count;INPUT_UNLOCK();
}
static bool input_dispatch(uint32_t now_ms,bool repeat_touch) {
    bool changed=false,touched=false;
    light_begin(now_ms);
    for(unsigned i=0;i<TAB5_NATIVE_INPUT_CAPACITY;i++) {
        native_input_t e;
        INPUT_LOCK();
        bool available=input_queue.open&&input_queue.count;
        if(available){e=input_queue.events[input_queue.head];input_queue.head=(input_queue.head+1)%TAB5_NATIVE_INPUT_CAPACITY;input_queue.count--;input_queue.stats.consumed++;}
        INPUT_UNLOCK();
        if(!available)break;
        changed=true;
        if(e.raw_button){
            uint64_t at=light_extend(e.ms);
            if(e.pressed&&!button_down){button_down=true;button_long=false;button_at=e.ms;button_swallow=tab5_backlight_step(&backlight,at,TAB5_BL_PRESS);if(button_swallow)tab5_ui_encoder_wake(e.ms);}
            else if(!e.pressed&&button_down){
                if(!button_swallow&&!button_long)tab5_ui_input((uint32_t)(e.ms-button_at)>=600?TAB5_UI_INPUT_LONG:TAB5_UI_INPUT_PRESS,0,e.ms);
                button_down=false;button_swallow=false;
            }
            continue;
        }
        if(e.kind==TAB5_UI_INPUT_TOUCH){
            input_x=e.x;input_y=e.y;input_pressed=e.pressed;touched=true;
            if(!tab5_backlight_step(&backlight,light_extend(e.ms),e.pressed?TAB5_BL_TOUCH_DOWN:TAB5_BL_TOUCH_UP))tab5_ui_touch(input_x,input_y,input_pressed,e.ms);
        }else if(e.kind==TAB5_UI_INPUT_ROT){
            int32_t delta=e.delta;
            if(tab5_backlight_step(&backlight,light_extend(e.ms),TAB5_BL_ROTATE)){tab5_ui_encoder_wake(e.ms);delta+=delta>0?-1:1;}
            if(delta)tab5_ui_input(e.kind,delta,e.ms);
        }else if(e.kind==TAB5_UI_INPUT_IDLE||!tab5_backlight_step(&backlight,light_extend(e.ms),TAB5_BL_PRESS))tab5_ui_input(e.kind,e.delta,e.ms);
        else tab5_ui_encoder_wake(e.ms);
    }
    if(!touched&&repeat_touch&&!backlight.swallow)tab5_ui_touch(input_x,input_y,input_pressed,now_ms);
    uint64_t at=light_extend(now_ms);
    if(button_down){
        tab5_backlight_step(&backlight,at,TAB5_BL_PRESS);
        if(!button_swallow&&!button_long&&(uint32_t)(now_ms-button_at)>=600){tab5_ui_input(TAB5_UI_INPUT_LONG,0,now_ms);button_long=true;changed=true;}
    }
    tab5_backlight_step(&backlight,at,TAB5_BL_IDLE);
    tab5_ui_input(TAB5_UI_INPUT_IDLE,0,now_ms);
    return changed;
}
bool tab5_native_port_input_dispatch(uint32_t now_ms) {return input_dispatch(now_ms,true);}
#endif

#ifndef TAB5_NATIVE_INPUT_ONLY
#include "tab5_panel.h"
#include "native/native_ui.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "driver/ppa.h"
#include "esp_async_fbcpy.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_timer.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#define WIDTH 1280
#define HEIGHT 720
#define FRAME_BYTES (WIDTH*HEIGHT*sizeof(uint16_t))
/* Acquisition/UI state still updates at 10 Hz in main.c. Only an observed
 * interaction can request the shorter, bounded display interval. Two private
 * CPU slots permit one producer render ahead; a single transfer worker owns
 * canonical/PPA/LCD until EOF. Slot reuse/release belongs only to the UI. */
#define PERIODIC_RENDER_US UINT64_C(100000)
#define INTERACTION_RENDER_US UINT64_C(16667)
static SemaphoreHandle_t ui_mutex,transfer_done,ppa_done;
/* Published by the running UI task, never by an ISR or a failed create. */
static TaskHandle_t native_task_handle,transfer_task_handle;
static QueueHandle_t ready_frames,completed_frames;
static unsigned recovery_epoch,fault_epoch;
static unsigned ready_count;
static uint64_t gpu_started_at,gpu_finished_at;
static bool composer_available;
static bool worker_fault;
static int active_slot=-1;
static esp_lcd_panel_handle_t panel;
static esp_lcd_touch_handle_t touch;
static ppa_client_handle_t ppa;
static uint16_t *frame,*rotated;
static portMUX_TYPE stats_lock=portMUX_INITIALIZER_UNLOCKED;
static tab5_native_port_stats_t stats;
static bool started;
static unsigned verify_requested,verify_epoch,verify_queued;
typedef enum { IDLE=0,PPA_PENDING=1,LCD_PENDING=2,CACHE_PENDING=3 } pipeline_stage_t;
static struct {
    pipeline_stage_t stage;
    uint64_t render_start,ppa_start,submitted,lcd_submit_return,sample_us,render_cost,rotate_cost;
    uint32_t benchmark_request,reference_epoch;
    int lcd_x,lcd_y,lcd_w,lcd_h;
    bool interactive,timed_out,submit_failed,composing,reference_ready;
} pipeline;
static uint64_t ppa_completed_at,lcd_completed_at;
static void task_ui_lock(void);
static tab5_native_scroll_benchmark_status_t benchmark_report;
static uint64_t benchmark_requested_at;
static bool benchmark_saved_active;
static bool (*benchmark_cache_get)(void);
static void (*benchmark_cache_set)(bool),(*benchmark_cache_clear)(void);
static struct {
    uint32_t id,interval;int position,maximum,direction;
    bool old_cache,needs_step,suppress_touch;
    uint64_t encoder_quiet_until;
    tab5_ui_scroll_benchmark_state_t saved;
} benchmark;

#define COMPOSE_VIEWS 8u
#define COMPOSE_JOBS 48u
/* Runtime diagnostic only. Zero-initialized OFF; no persistence/UI setting.
 * A plan captures once; setters never mutate pending jobs or DMA ownership. */
static bool cpu_tiles_enabled;
void tab5_native_port_set_cpu_tiles_enabled(bool enabled){
    __atomic_store_n(&cpu_tiles_enabled,enabled,__ATOMIC_RELEASE);
}
bool tab5_native_port_cpu_tiles_enabled(void){
    return __atomic_load_n(&cpu_tiles_enabled,__ATOMIC_ACQUIRE);
}
#define COMPOSE_CPU_RECT_PIXELS 16384u
#define COMPOSE_CPU_FRAME_PIXELS 32768u
typedef struct {nui_area_t area;int view;} compose_job_t;
typedef struct {
    esp_async_fbcpy_handle_t copy;
    bool collecting,ready;
    unsigned view_count,job_count,job_index;
    nui_surface_view_t views[COMPOSE_VIEWS];
    compose_job_t jobs[COMPOSE_JOBS];
    nui_area_t dirty[48],fragments[48],scratch[48];
    unsigned dirty_count,cpu_count,cpu_pixels;
    nui_area_t cpu[COMPOSE_JOBS];
    struct {size_t first,last;} sync_ranges[COMPOSE_JOBS];
    unsigned copy_done;
    uint64_t completed_at,job_start;
} compose_state_t;
static compose_state_t compose;
static compose_state_t *collecting=&compose; /* UI task only; worker never reads it. */
typedef enum { SLOT_FREE,SLOT_BUILDING,SLOT_READY,SLOT_ACTIVE,SLOT_DONE } slot_state_t;
typedef struct {
    uint16_t *pixels;
    compose_state_t plan;
    nui_area_t debt[48];unsigned debt_count;
    bool debt_full,initialized,reference_clean,reference_ready,interactive,success;
    unsigned state,generation,reference_epoch;
    uint32_t benchmark_request;
    int benchmark_position;
    uint64_t render_start,render_cost,sample_us;
} frame_slot_t;
static frame_slot_t slots[2];
static int reference_slot=-1;static unsigned reference_slot_epoch; /* UI-only independent CPU reference. */
static void worker_finish(bool success);
static void worker_recovery(bool fault);
static bool transport_idle(void);
/* Transfer-worker only, bounded MRU->LRU. No payload ownership is acquired here:
 * live leases guarantee immutability; unique storage IDs prevent ABA reuse.
 * Never retain a partial strip flush as proof of the complete portrait. */
static struct {
    uint64_t storage_id,generation;
    const uint16_t *portrait;
    uint32_t stride,width,height;
} source_flushed[8];
static unsigned source_flushed_count;
static void source_flush_reset(void){source_flushed_count=0;}
static esp_err_t compose_flush_source(const nui_surface_view_t *v){
    uint64_t rows=((uint64_t)v->logical_width+15u)&~UINT64_C(15);
    uint64_t bytes=rows*v->portrait_stride*sizeof(uint16_t);
    if(!v->portrait||((uintptr_t)v->portrait&63u)||!bytes||(bytes&63u)||bytes>8u*1024u*1024u)return ESP_ERR_INVALID_ARG;
    bool cacheable=v->storage_id&&v->portrait_generation;
    if(cacheable)for(unsigned i=0;i<source_flushed_count;i++){
        if(source_flushed[i].storage_id==v->storage_id&&source_flushed[i].generation==v->portrait_generation&&
           source_flushed[i].portrait==v->portrait&&source_flushed[i].stride==v->portrait_stride&&
           source_flushed[i].width==v->logical_width&&source_flushed[i].height==v->logical_height){
            __typeof__(source_flushed[0]) hit=source_flushed[i];
            for(unsigned j=i;j;j--)source_flushed[j]=source_flushed[j-1];
            source_flushed[0]=hit;
            portENTER_CRITICAL(&stats_lock);stats.compose_source_sync_hits++;portEXIT_CRITICAL(&stats_lock);
            return ESP_OK;
        }
    }
    /* A different key flushing the same address can supersede old physical
     * contents. Drop aliases even on failure; never leave a stale hit behind. */
    unsigned keep=0;
    for(unsigned i=0;i<source_flushed_count;i++){
        if(source_flushed[i].portrait!=v->portrait&&(!v->storage_id||source_flushed[i].storage_id!=v->storage_id))
            source_flushed[keep++]=source_flushed[i];
    }
    source_flushed_count=keep;
    esp_err_t error=esp_cache_msync((void*)v->portrait,(size_t)bytes,ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if(error!=ESP_OK)return error;
    portENTER_CRITICAL(&stats_lock);stats.compose_source_sync_jobs++;stats.compose_source_sync_bytes+=bytes;portEXIT_CRITICAL(&stats_lock);
    if(!cacheable)return ESP_OK;
    unsigned end=source_flushed_count<8?source_flushed_count++:7;
    for(unsigned j=end;j;j--)source_flushed[j]=source_flushed[j-1];
    source_flushed[0]=(__typeof__(source_flushed[0])){v->storage_id,v->portrait_generation,v->portrait,
        v->portrait_stride,v->logical_width,v->logical_height};
    return ESP_OK;
}
static bool area_valid(nui_area_t a){return a.x1>=0&&a.y1>=0&&a.x2>=a.x1&&a.y2>=a.y1&&a.x2<WIDTH&&a.y2<HEIGHT;}
static bool area_intersects(nui_area_t a,nui_area_t b){return a.x1<=b.x2&&b.x1<=a.x2&&a.y1<=b.y2&&b.y1<=a.y2;}
static bool area_contains(nui_area_t a,nui_area_t b){return a.x1<=b.x1&&a.y1<=b.y1&&a.x2>=b.x2&&a.y2>=b.y2;}
static bool compose_accept(const nui_surface_view_t *v,void *unused){
    (void)unused;
    if(!collecting||!collecting->ready||!collecting->collecting||collecting->view_count>=COMPOSE_VIEWS||!v||!v->lease||
       !v->landscape||!v->portrait||!area_valid(v->destination)||((uintptr_t)v->portrait&63u)||
       v->logical_width<1||v->logical_height<1||v->logical_width>16383||v->logical_height>16383||
       v->portrait_stride<1||((v->logical_width+15u)&~15u)<1||v->portrait_stride>16383||((v->logical_width+15u)&~15u)>16383||
       v->portrait_stride<v->logical_height||v->landscape_stride<(size_t)v->logical_width||
       v->landscape_stride>16383||(v->portrait_stride&31u)||((uintptr_t)v->landscape&63u))return false;
    int64_t w=(int64_t)v->destination.x2-v->destination.x1+1,h=(int64_t)v->destination.y2-v->destination.y1+1;
    if((int64_t)v->source_x+w>v->logical_width||(int64_t)v->source_y+h>v->logical_height||
       (v->portrait_stride*(uint64_t)((v->logical_width+15u)&~15u)*2)>8u*1024u*1024u||((v->portrait_stride*(uint64_t)((v->logical_width+15u)&~15u)*2)&63u)||
       (int64_t)v->source_y+h>v->portrait_stride)return false;
    for(unsigned i=0;i<collecting->view_count;i++)if(area_intersects(v->destination,collecting->views[i].destination))return false;
    collecting->views[collecting->view_count++]=*v;
    if((v->destination.x1&15)||(v->destination.y1&15)||((v->destination.x2+1)&15)||((v->destination.y2+1)&15)||(v->source_x&15)||(v->source_y&15)){
        portENTER_CRITICAL(&stats_lock);stats.compose_unaligned_views++;portEXIT_CRITICAL(&stats_lock);
    }
    return true;
}
/* UI mutex required; never releases an unknown DMA owner. */
static void compose_release_locked(void){
    for(unsigned i=0;i<collecting->view_count;i++)nui_surface_view_release(&collecting->views[i]);
    collecting->view_count=collecting->job_count=collecting->job_index=collecting->cpu_count=collecting->cpu_pixels=0;collecting->collecting=false;
}
static bool compose_append(nui_area_t area,int view){
    if(!area_valid(area)||collecting->job_count>=COMPOSE_JOBS)return false;
    collecting->jobs[collecting->job_count++]=(compose_job_t){area,view};return true;
}
static bool fragment_append(unsigned *count,nui_area_t a){
    if(a.x1>a.x2||a.y1>a.y2)return true;
    if(*count>=48)return false;
    collecting->scratch[(*count)++]=a;return true;
}
static bool compose_plan(void){
    const bool cpu_enabled=tab5_native_port_cpu_tiles_enabled();
    collecting->job_count=collecting->job_index=collecting->cpu_count=collecting->cpu_pixels=0;
    size_t count=nui_native_get_dirty(collecting->dirty,48);
    if(!count||count>48)return false;
    collecting->dirty_count=(unsigned)count;
    for(unsigned i=0;i<count;i++)if(!area_valid(collecting->dirty[i]))return false;
    for(unsigned i=0;i<collecting->view_count;i++){
        bool covered=false;for(unsigned j=0;j<count;j++)if(area_contains(collecting->dirty[j],collecting->views[i].destination))covered=true;
        if(!covered||!compose_append(collecting->views[i].destination,(int)i))return false;
    }
    for(unsigned d=0;d<count;d++){
        unsigned n=1;collecting->fragments[0]=collecting->dirty[d];
        for(unsigned v=0;v<collecting->view_count;v++){
            unsigned next=0;nui_area_t cut=collecting->views[v].destination;
            for(unsigned k=0;k<n;k++){
                nui_area_t a=collecting->fragments[k];
                if(!area_intersects(a,cut)){if(!fragment_append(&next,a))return false;continue;}
                nui_area_t c={a.x1>cut.x1?a.x1:cut.x1,a.y1>cut.y1?a.y1:cut.y1,a.x2<cut.x2?a.x2:cut.x2,a.y2<cut.y2?a.y2:cut.y2};
                if(!fragment_append(&next,(nui_area_t){a.x1,a.y1,a.x2,c.y1-1})||
                   !fragment_append(&next,(nui_area_t){a.x1,c.y2+1,a.x2,a.y2})||
                   !fragment_append(&next,(nui_area_t){a.x1,c.y1,c.x1-1,c.y2})||
                   !fragment_append(&next,(nui_area_t){c.x2+1,c.y1,a.x2,c.y2}))return false;
            }
            n=next;for(unsigned k=0;k<n;k++)collecting->fragments[k]=collecting->scratch[k];
        }
        for(unsigned k=0;k<n;k++)if(!compose_append(collecting->fragments[k],-1))return false;
    }
    unsigned remaining=0;
    for(unsigned i=0;i<collecting->job_count;i++){
        compose_job_t job=collecting->jobs[i];nui_area_t a=job.area;
        unsigned pixels=(unsigned)(a.x2-a.x1+1)*(unsigned)(a.y2-a.y1+1);
        if(cpu_enabled&&job.view<0&&pixels<=COMPOSE_CPU_RECT_PIXELS&&pixels<=COMPOSE_CPU_FRAME_PIXELS-collecting->cpu_pixels){
            collecting->cpu[collecting->cpu_count++]=a;collecting->cpu_pixels+=pixels;
        }else collecting->jobs[remaining++]=job;
    }
    collecting->job_count=remaining;
    return collecting->job_count>0||collecting->cpu_count>0;
}
/* CPU fallback: RGB565 dirty union, conservatively expanded to rev1 16x16
 * macroblocks. Buffers/strides remain full-frame and 64-byte aligned. */
#define ROTATION_DIRTY_CAPACITY 48
static bool rotation_baseline_valid;
static bool native_rotation_bounds(const nui_area_t *areas,size_t count,nui_area_t *out){
    if(!out)return false;
    *out=(nui_area_t){0,0,WIDTH-1,HEIGHT-1};
    if(!areas||!count||count>ROTATION_DIRTY_CAPACITY)return false;
    int x1=WIDTH,y1=HEIGHT,x2=-1,y2=-1;
    for(size_t i=0;i<count;i++){
        nui_area_t a=areas[i];
        if(a.x1>a.x2||a.y1>a.y2||a.x2<0||a.y2<0||a.x1>=WIDTH||a.y1>=HEIGHT)return false;
        if(a.x1<0)a.x1=0;
        if(a.y1<0)a.y1=0;
        if(a.x2>=WIDTH)a.x2=WIDTH-1;
        if(a.y2>=HEIGHT)a.y2=HEIGHT-1;
        if(a.x1<x1)x1=a.x1;
        if(a.y1<y1)y1=a.y1;
        if(a.x2>x2)x2=a.x2;
        if(a.y2>y2)y2=a.y2;
    }
    *out=(nui_area_t){x1&~15,y1&~15,((x2+16)&~15)-1,((y2+16)&~15)-1};
    return true;
}

/* Inverse of the existing PPA 90-degree landscape->portrait mapping:
 * display pixel (x,y) becomes portrait (y,1279-x). */
static bool native_touch_position(unsigned tx,unsigned ty,int *x,int *y) {
    if(tx>=HEIGHT||ty>=WIDTH||!x||!y)return false;
    *x=WIDTH-1-(int)ty;*y=(int)tx;return true;
}

bool tab5_native_port_lock(uint32_t timeout_ms) {
    /* Services can already answer diagnostics during startup. Never expose
     * a partially initialized (or failure-cleaned) renderer mutex. */
    return __atomic_load_n(&started,__ATOMIC_ACQUIRE)&&ui_mutex&&
        xSemaphoreTake(ui_mutex,pdMS_TO_TICKS(timeout_ms))==pdTRUE;
}
void tab5_native_port_unlock(void) { xSemaphoreGive(ui_mutex); }
void tab5_native_port_stats(tab5_native_port_stats_t *out,bool reset) {
    portENTER_CRITICAL(&stats_lock);
    if(reset){uint32_t state=stats.pipeline_state;bool enabled=stats.worker_enabled;stats=(tab5_native_port_stats_t){0};stats.pipeline_state=state;stats.worker_enabled=enabled;}
    if(out)*out=stats;
    portEXIT_CRITICAL(&stats_lock);
}
static uint64_t clock_us(void);
/* Audited ST7121/ST7123 vendor wrapper returns the original DPI panel.
 * One driver FB is required: no inference of the current FB in a multi-FB mode.
 * This accessor is diagnostic-only and never exposes a writable FB to render. */
static bool verification_framebuffer(uint16_t **out){
    *out=NULL;
#if CONFIG_BSP_LCD_DPI_BUFFER_NUMS == 1
    void *buffer=NULL;
    if(esp_lcd_dpi_panel_get_frame_buffer(panel,1,&buffer,(void **)NULL)!=ESP_OK||
       !buffer||((uintptr_t)buffer&63u))return false;
    *out=buffer;return true;
#else
    return false;
#endif
}
/* Diagnostic only: 1..2 future successful rotations, never a startup scan.
 * UI task owns both buffers here: PPA and LCD-copy complete, before reuse. */
bool tab5_native_port_request_verify(unsigned frames){
    if(frames<1||frames>2||!__atomic_load_n(&started,__ATOMIC_ACQUIRE))return false;
    uint16_t *buffer;if(!verification_framebuffer(&buffer))return false;
    portENTER_CRITICAL(&stats_lock);
    __atomic_store_n(&verify_requested,frames,__ATOMIC_RELEASE);verify_queued=0;
    __atomic_add_fetch(&verify_epoch,1,__ATOMIC_ACQ_REL);
    portEXIT_CRITICAL(&stats_lock);return true;
}
static uint64_t native_rotation_mismatches(const uint16_t *src,const uint16_t *dst){
    uint64_t mismatches=0;
    for(unsigned y=0;y<HEIGHT;y++)for(unsigned x=0;x<WIDTH;x++)
        mismatches+=dst[(WIDTH-1-x)*HEIGHT+y]!=src[y*WIDTH+x];
    return mismatches;
}
static uint64_t verify_rotation_once(void){
    portENTER_CRITICAL(&stats_lock);
    unsigned count=__atomic_load_n(&verify_requested,__ATOMIC_ACQUIRE);
    bool check=pipeline.reference_ready&&count&&pipeline.reference_epoch==__atomic_load_n(&verify_epoch,__ATOMIC_ACQUIRE);
    if(check)__atomic_store_n(&verify_requested,count-1,__ATOMIC_RELEASE);
    if(pipeline.reference_ready&&pipeline.reference_epoch==verify_epoch&&verify_queued)verify_queued--;
    portEXIT_CRITICAL(&stats_lock);
    if(!check)return 0;
    uint64_t begin=clock_us();
    /* Read the driver's actual retained portrait FB after LCD copy EOF.
     * The packed PPA output is not a full image and cannot verify retention. */
    uint16_t *buffer=NULL;
    esp_err_t error=verification_framebuffer(&buffer)?
        esp_cache_msync(buffer,FRAME_BYTES,ESP_CACHE_MSYNC_FLAG_DIR_M2C):ESP_ERR_INVALID_STATE;
    uint64_t mismatches=error==ESP_OK?native_rotation_mismatches(frame,buffer):0;
    uint64_t elapsed=clock_us()-begin;
    portENTER_CRITICAL(&stats_lock);
    if(error==ESP_OK){stats.verify_checks++;stats.verify_mismatches+=mismatches;}
    else stats.verify_errors++;
    stats.verify_us+=elapsed;
    portEXIT_CRITICAL(&stats_lock);
    return elapsed;
}
static void pipeline_set(pipeline_stage_t stage){
    pipeline.stage=stage;
    portENTER_CRITICAL(&stats_lock);stats.pipeline_state=(uint32_t)stage;portEXIT_CRITICAL(&stats_lock);
}
static bool notify_completion(SemaphoreHandle_t done){
    BaseType_t semaphore_wake=pdFALSE,notify_wake=pdFALSE;
    xSemaphoreGiveFromISR(done,&semaphore_wake);
    TaskHandle_t task=__atomic_load_n(&transfer_task_handle,__ATOMIC_ACQUIRE);
    if(task)vTaskNotifyGiveFromISR(task,&notify_wake);
    return semaphore_wake==pdTRUE||notify_wake==pdTRUE;
}
static bool ppa_finished(ppa_client_handle_t client,ppa_event_data_t *event,void *ctx){
    (void)client;(void)event;(void)ctx;
    portENTER_CRITICAL_ISR(&stats_lock);ppa_completed_at=clock_us();portEXIT_CRITICAL_ISR(&stats_lock);
    return notify_completion(ppa_done);
}
static bool transfer_finished(esp_lcd_panel_handle_t handle,esp_lcd_dpi_panel_event_data_t *event,void *ctx){
    (void)handle;(void)event;(void)ctx;
    portENTER_CRITICAL_ISR(&stats_lock);lcd_completed_at=clock_us();gpu_finished_at=lcd_completed_at;portEXIT_CRITICAL_ISR(&stats_lock);
    return notify_completion(transfer_done);
}
static uint64_t clock_us(void){return (uint64_t)esp_timer_get_time();}
/* Internal UI-task lock waits only; external main callers use port_lock(). */
static void task_ui_lock(void){
    uint64_t begin=clock_us();xSemaphoreTake(ui_mutex,portMAX_DELAY);
    uint64_t elapsed=clock_us()-begin;
    portENTER_CRITICAL(&stats_lock);stats.ui_lock_wait_us+=elapsed;portEXIT_CRITICAL(&stats_lock);
}
/* Caller holds stats_lock; timestamp is captured immediately after observing
 * completion, before verification or the next job is submitted. */
static void account_callback_delay(pipeline_stage_t stage,uint64_t done,uint64_t processed){
    uint64_t elapsed=processed>done?processed-done:0;
    stats.callback_process_delay_us+=elapsed;
    if(stage==CACHE_PENDING)stats.cache_callback_process_delay_us+=elapsed;
    else if(stage==PPA_PENDING)stats.ppa_callback_process_delay_us+=elapsed;
    else stats.lcd_callback_process_delay_us+=elapsed;
}
bool tab5_native_port_set_scroll_cache_hooks(bool (*get)(void),void (*set)(bool),void (*clear)(void)){
    if(!get||!set||!clear)return false;
    portENTER_CRITICAL(&stats_lock);
    bool allowed=!benchmark_report.pending&&!benchmark_report.active&&!benchmark_saved_active;
    if(allowed){benchmark_cache_get=get;benchmark_cache_set=set;benchmark_cache_clear=clear;}
    portEXIT_CRITICAL(&stats_lock);return allowed;
}
bool tab5_native_port_request_scroll_benchmark(uint32_t frames,uint32_t interval,bool cache){
    if(!__atomic_load_n(&started,__ATOMIC_ACQUIRE)||!frames||frames>120||(interval!=40000&&interval!=16667))return false;
    portENTER_CRITICAL(&stats_lock);
    if(cache&&!benchmark_cache_set){portEXIT_CRITICAL(&stats_lock);return false;}
    uint32_t id=benchmark_report.request_id+1;if(!id)id=1;
    benchmark_report=(tab5_native_scroll_benchmark_status_t){.pending=true,.request_id=id,
        .requested_frames=frames,.interval_us=interval,.cache_enabled=cache};
    benchmark_requested_at=clock_us();portEXIT_CRITICAL(&stats_lock);return true;
}
void tab5_native_port_cancel_scroll_benchmark(void){
    portENTER_CRITICAL(&stats_lock);
    if(benchmark_report.pending||benchmark_report.active){
        benchmark_report.pending=benchmark_report.active=false;benchmark_report.finished=true;
        benchmark_report.reason=3;
        if(benchmark_report.started_us)benchmark_report.elapsed_us=clock_us()-benchmark_report.started_us;
        benchmark_report.restored=!benchmark_saved_active;
    }
    portEXIT_CRITICAL(&stats_lock);
}
void tab5_native_port_scroll_benchmark_status(tab5_native_scroll_benchmark_status_t *out){
    if(!out)return;
    portENTER_CRITICAL(&stats_lock);*out=benchmark_report;
    if(out->active)out->elapsed_us=clock_us()-out->started_us;
    if(out->elapsed_us)out->fps_milli=(uint32_t)((uint64_t)out->completed_frames*UINT64_C(1000000000)/out->elapsed_us);
    portEXIT_CRITICAL(&stats_lock);
}
/* All helpers below run only in UI task with ui_mutex, never request/status. */
static void benchmark_finish(unsigned reason,uint32_t id){
    portENTER_CRITICAL(&stats_lock);
    if(benchmark_report.request_id!=id){portEXIT_CRITICAL(&stats_lock);return;}benchmark_report.pending=benchmark_report.active=false;
    benchmark_report.finished=true;benchmark_report.reason=reason;
    if(benchmark_report.started_us)benchmark_report.elapsed_us=clock_us()-benchmark_report.started_us;
    benchmark_report.restored=!benchmark_saved_active;portEXIT_CRITICAL(&stats_lock);
}
static void benchmark_restore(void){
    if(benchmark.saved.valid){
        if(benchmark_cache_set)benchmark_cache_set(benchmark.old_cache);
        tab5_ui_scroll_benchmark_end(&benchmark.saved);
    }
    portENTER_CRITICAL(&stats_lock);benchmark_saved_active=false;
    if(benchmark_report.request_id==benchmark.id||benchmark_report.finished)benchmark_report.restored=true;
    portEXIT_CRITICAL(&stats_lock);
}
static bool benchmark_running(void){
    portENTER_CRITICAL(&stats_lock);bool active=benchmark_report.active&&benchmark_report.request_id==benchmark.id;
    portEXIT_CRITICAL(&stats_lock);return active;
}
static bool benchmark_control(bool pressed,bool encoder){
    uint64_t now=clock_us();tab5_native_scroll_benchmark_status_t report;
    tab5_native_port_scroll_benchmark_status(&report);
    if(pressed&&(report.pending||report.active||benchmark.saved.valid)){
        benchmark.suppress_touch=true;benchmark_finish(2,report.request_id);
    }
    if(benchmark.suppress_touch){
        if(!pressed){benchmark_restore();benchmark.suppress_touch=false;}
        return true; /* Swallow both press and release; never click restored UI. */
    }
    if(benchmark.saved.valid&&(report.request_id!=benchmark.id||!report.active))benchmark_restore();
    if(encoder&&(report.pending||report.active)){
        benchmark_finish(2,report.request_id);benchmark_restore();benchmark.encoder_quiet_until=now+2000000;return true;
    }
    if(encoder&&now<benchmark.encoder_quiet_until){benchmark.encoder_quiet_until=now+2000000;return true;}
    if(report.active&&(now-report.started_us>=25000000||__atomic_load_n(&worker_fault,__ATOMIC_ACQUIRE))){
        benchmark_finish(__atomic_load_n(&worker_fault,__ATOMIC_ACQUIRE)?6:4,report.request_id);benchmark_restore();return false;
    }
    if(report.pending){
        if(now-benchmark_requested_at>=25000000){benchmark_finish(4,report.request_id);return false;}
        if(!transport_idle())return false;
        benchmark.id=report.request_id;uint64_t begin=clock_us();
        if(!tab5_ui_scroll_benchmark_begin(&benchmark.saved,&benchmark.maximum)){benchmark_finish(5,report.request_id);return false;}
        benchmark.old_cache=benchmark_cache_get?benchmark_cache_get():false;
        if(benchmark_cache_set){
            benchmark_cache_set(report.cache_enabled);benchmark_cache_clear();
            if(benchmark_cache_get()!=report.cache_enabled){
                benchmark_restore();benchmark_finish(5,report.request_id);return false;
            }
        }
        benchmark.position=0;benchmark.direction=1;benchmark.needs_step=false;benchmark.interval=report.interval_us;
        portENTER_CRITICAL(&stats_lock);benchmark_saved_active=true;
        if(benchmark_report.request_id!=report.request_id||!benchmark_report.pending){
            portEXIT_CRITICAL(&stats_lock);benchmark_restore();return false;
        }
        benchmark_report.pending=false;benchmark_report.active=true;benchmark_report.started_us=begin;benchmark_report.setup_us=clock_us()-begin;
        portEXIT_CRITICAL(&stats_lock);
    }
    if(benchmark_running()&&benchmark.needs_step){
        int next=benchmark.position+benchmark.direction*16;
        if(next>=benchmark.maximum){next=benchmark.maximum;benchmark.direction=-1;}
        if(next<=0){next=0;benchmark.direction=1;}
        benchmark.position=next;benchmark.needs_step=false;tab5_ui_scroll_benchmark_position(next);
        portENTER_CRITICAL(&stats_lock);
        if(next<benchmark_report.scroll_min)benchmark_report.scroll_min=next;
        if(next>benchmark_report.scroll_max)benchmark_report.scroll_max=next;
        portEXIT_CRITICAL(&stats_lock);
    }
    return false;
}
static bool cache_copy_finished(esp_async_fbcpy_handle_t handle,esp_async_fbcpy_event_data_t *event,void *ctx){
    (void)handle;(void)event;(void)ctx;
    portENTER_CRITICAL_ISR(&stats_lock);compose.completed_at=clock_us();portEXIT_CRITICAL_ISR(&stats_lock);
    __atomic_store_n(&compose.copy_done,1,__ATOMIC_RELEASE);
    BaseType_t wake=pdFALSE;TaskHandle_t task=__atomic_load_n(&transfer_task_handle,__ATOMIC_ACQUIRE);
    if(task)vTaskNotifyGiveFromISR(task,&wake);
    return wake==pdTRUE;
}
static esp_err_t compose_submit_job(void){
    compose_job_t *job=&compose.jobs[compose.job_index];nui_area_t a=job->area;
    unsigned w=(unsigned)(a.x2-a.x1+1),h=(unsigned)(a.y2-a.y1+1);
    unsigned dx=(unsigned)a.y1,dy=WIDTH-1-(unsigned)a.x2;
    compose.job_start=clock_us();
    if(job->view>=0){
        nui_surface_view_t *v=&compose.views[job->view];
        unsigned sx=(unsigned)v->source_y,sy=(unsigned)(v->logical_width-v->source_x-(int)w);
        esp_err_t error=compose_flush_source(v);
        if(error!=ESP_OK)return ESP_ERR_INVALID_ARG; /* No DMA submitted. */
        size_t first=(size_t)dy*HEIGHT*2,last=(size_t)(dy+w)*HEIGHT*2;
        first&=~(size_t)63;last=(last+63)&~(size_t)63;
        if(last>FRAME_BYTES)return ESP_ERR_INVALID_ARG;
        error=esp_cache_msync((uint8_t*)rotated+first,last-first,ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        if(error!=ESP_OK)return ESP_ERR_INVALID_ARG;
        esp_async_fbcpy_trans_desc_t transaction={.src_buffer=v->portrait,.dst_buffer=rotated,
            .src_buffer_size_x=v->portrait_stride,.src_buffer_size_y=(size_t)((v->logical_width+15u)&~15u),
            .dst_buffer_size_x=HEIGHT,.dst_buffer_size_y=WIDTH,.src_offset_x=sx,.src_offset_y=sy,
            .dst_offset_x=dx,.dst_offset_y=dy,.copy_size_x=h,.copy_size_y=w,
            .pixel_format_unique_id={.color_type_id=LCD_COLOR_FMT_RGB565}};
        __atomic_store_n(&compose.copy_done,0,__ATOMIC_RELEASE);pipeline_set(CACHE_PENDING);
        return esp_async_fbcpy(compose.copy,&transaction,cache_copy_finished,NULL);
    }
    ppa_srm_oper_config_t op={
        .in={.buffer=frame,.pic_w=WIDTH,.pic_h=HEIGHT,.block_w=w,.block_h=h,.block_offset_x=(unsigned)a.x1,.block_offset_y=(unsigned)a.y1,.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
        .out={.buffer=rotated,.buffer_size=FRAME_BYTES,.pic_w=HEIGHT,.pic_h=WIDTH,.block_offset_x=dx,.block_offset_y=dy,.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
        .rotation_angle=PPA_SRM_ROTATION_ANGLE_90,.scale_x=1,.scale_y=1,.mode=PPA_TRANS_MODE_NON_BLOCKING};
    xSemaphoreTake(ppa_done,0);pipeline_set(PPA_PENDING);
    return ppa_do_scale_rotate_mirror(ppa,&op);
}
static void submit_lcd(void);
static void pipeline_error(esp_err_t error);
/* Only after every DMA/PPA EOF. Prepare ALL touched canonical cache lines
 * before ANY CPU store: adjacent disjoint rects can share a cache line, even
 * across portrait rows (720 RGB565 pixels = 1440 bytes). Sorted intervals are
 * merged across rows. No broad post-write invalidation is allowed. */
static esp_err_t compose_cpu_sync(void){
    size_t pending_first=0,pending_last=0;
    int first_row=WIDTH,last_row=-1;
    for(unsigned i=0;i<compose.cpu_count;i++){
        int lo=WIDTH-1-compose.cpu[i].x2,hi=WIDTH-1-compose.cpu[i].x1;
        if(lo<first_row)first_row=lo;
        if(hi>last_row)last_row=hi;
    }
    for(int row=first_row;row<=last_row;row++){
        unsigned count=0;
        for(unsigned i=0;i<compose.cpu_count;i++){
            nui_area_t a=compose.cpu[i];
            if(row<WIDTH-1-a.x2||row>WIDTH-1-a.x1)continue;
            size_t first=((size_t)row*HEIGHT+(unsigned)a.y1)*2;
            size_t last=((size_t)row*HEIGHT+(unsigned)a.y2+1)*2;
            first&=~(size_t)63;last=(last+63)&~(size_t)63;
            if(last>FRAME_BYTES)return ESP_ERR_INVALID_ARG;
            unsigned at=count++;
            while(at&&compose.sync_ranges[at-1].first>first){compose.sync_ranges[at]=compose.sync_ranges[at-1];at--;}
            compose.sync_ranges[at].first=first;compose.sync_ranges[at].last=last;
        }
        for(unsigned i=0;i<count;i++){
            size_t first=compose.sync_ranges[i].first,last=compose.sync_ranges[i].last;
            if(pending_last&&first>pending_last){
                esp_err_t e=esp_cache_msync((uint8_t*)rotated+pending_first,pending_last-pending_first,ESP_CACHE_MSYNC_FLAG_DIR_M2C);
                if(e!=ESP_OK)return e;
                pending_first=first;pending_last=last;
            }else {
                if(!pending_last)pending_first=first;
                if(last>pending_last)pending_last=last;
            }
        }
    }
    return pending_last?esp_cache_msync((uint8_t*)rotated+pending_first,pending_last-pending_first,ESP_CACHE_MSYNC_FLAG_DIR_M2C):ESP_OK;
}
static void compose_cpu_transpose(nui_area_t a){
    uint16_t tile[16][16]; /* 512 B, bounded UI task stack, no allocation. */
    for(int by=a.y1;by<=a.y2;by+=16)for(int bx=a.x1;bx<=a.x2;bx+=16){
        unsigned h=(unsigned)(a.y2-by+1),w=(unsigned)(a.x2-bx+1);
        if(h>16)h=16;
        if(w>16)w=16;
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)tile[y][x]=frame[(size_t)(by+(int)y)*WIDTH+bx+x];
        for(unsigned x=0;x<w;x++){
            uint16_t *dst=rotated+(size_t)(WIDTH-1-bx-(int)x)*HEIGHT+by;
            for(unsigned y=0;y<h;y++)dst[y]=tile[y][x];
        }
    }
}
static void compose_advance(void){
    if(compose.job_index<compose.job_count){esp_err_t e=compose_submit_job();if(e!=ESP_OK)pipeline_error(e);return;}
    if(compose.cpu_count){
        uint64_t begin=clock_us();esp_err_t e=compose_cpu_sync();
        if(e!=ESP_OK){
            /* All DMA owners have completed and no CPU store has happened.
             * A sync failure is safely recoverable, regardless of its code. */
            pipeline_error(ESP_ERR_INVALID_ARG);return;
        }
        for(unsigned i=0;i<compose.cpu_count;i++)compose_cpu_transpose(compose.cpu[i]);
        uint64_t elapsed=clock_us()-begin;pipeline.rotate_cost+=elapsed;
        portENTER_CRITICAL(&stats_lock);stats.compose_cpu_jobs+=compose.cpu_count;
        stats.compose_cpu_pixels+=compose.cpu_pixels;stats.compose_cpu_us+=elapsed;stats.rotate_us+=elapsed;
        portEXIT_CRITICAL(&stats_lock);
    }
    /* Canonical portrait rows are already compact at full width. Bound ALL
     * rendered dirty regions, including CPU edges and intervening retained
     * holes; never infer the transport area from deferred views alone.
     * First/recovery stays full. Exactly one driver submit / copy EOF. */
    int x1=0,x2=WIDTH-1;
    if(rotation_baseline_valid&&compose.dirty_count&&compose.dirty_count<=48){
        x1=WIDTH-1;x2=0;
        for(unsigned i=0;i<compose.dirty_count;i++){
            if(compose.dirty[i].x1<x1)x1=compose.dirty[i].x1;
            if(compose.dirty[i].x2>x2)x2=compose.dirty[i].x2;
        }
    }
    pipeline.lcd_x=0;pipeline.lcd_y=WIDTH-1-x2;
    pipeline.lcd_w=HEIGHT;pipeline.lcd_h=x2-x1+1;submit_lcd();
}
static void pipeline_error(esp_err_t error){
    portENTER_CRITICAL(&stats_lock);stats.errors++;stats.submit_errors++;portEXIT_CRITICAL(&stats_lock);
    pipeline.submit_failed=true;worker_recovery(true);
    /* INVALID_ARG is rejected before DMA submission in the pinned PPA/DPI
     * drivers. Any other failure has unknown ownership: retain both buffers
     * until the corresponding completion; no cancel/free/retry on a timeout. */
    if(error==ESP_ERR_INVALID_ARG)worker_finish(false);
}
static void submit_lcd(void){
    xSemaphoreTake(transfer_done,0);
    pipeline.submitted=clock_us();pipeline_set(LCD_PENDING);
    esp_err_t error=esp_lcd_panel_draw_bitmap(panel,pipeline.lcd_x,pipeline.lcd_y,
        pipeline.lcd_x+pipeline.lcd_w,pipeline.lcd_y+pipeline.lcd_h,
        pipeline.composing?rotated+(size_t)pipeline.lcd_y*HEIGHT:rotated);
    uint64_t submitted=clock_us();pipeline.lcd_submit_return=submitted;
    portENTER_CRITICAL(&stats_lock);stats.flush_us+=submitted-pipeline.submitted;
    if(error==ESP_OK){
        stats.flushes++;stats.lcd_copy_bytes+=(uint64_t)pipeline.lcd_w*pipeline.lcd_h*sizeof(uint16_t);
        if(pipeline.lcd_x==0&&pipeline.lcd_y==0&&pipeline.lcd_w==HEIGHT&&pipeline.lcd_h==WIDTH)stats.full_lcd_copies++;
        else stats.partial_lcd_copies++;
    }
    portEXIT_CRITICAL(&stats_lock);
    if(error!=ESP_OK)pipeline_error(error);
}
static void pipeline_poll(void){
    if(pipeline.stage==CACHE_PENDING&&__atomic_exchange_n(&compose.copy_done,0,__ATOMIC_ACQ_REL)){
        uint64_t processed=clock_us();
        portENTER_CRITICAL(&stats_lock);uint64_t cost=compose.completed_at-compose.job_start;
        account_callback_delay(CACHE_PENDING,compose.completed_at,processed);
        pipeline.rotate_cost+=cost;stats.rotate_us+=cost;stats.compose_copy_us+=cost;stats.compose_copy_jobs++;
        nui_area_t a=compose.jobs[compose.job_index].area;stats.compose_copy_bytes+=(uint64_t)(a.x2-a.x1+1)*(a.y2-a.y1+1)*2;
        portEXIT_CRITICAL(&stats_lock);
        if(pipeline.submit_failed){worker_finish(false);return;}
        compose.job_index++;compose_advance();
    }
    if(pipeline.stage==PPA_PENDING&&xSemaphoreTake(ppa_done,0)==pdTRUE){
        uint64_t processed=clock_us();
        portENTER_CRITICAL(&stats_lock);uint64_t done=ppa_completed_at;
        account_callback_delay(PPA_PENDING,done,processed);
        uint64_t cost=done-(pipeline.composing?compose.job_start:pipeline.ppa_start);
        pipeline.rotate_cost+=cost;stats.rotate_us+=cost;
        if(pipeline.composing){stats.compose_ppa_jobs++;stats.compose_ppa_us+=cost;}portEXIT_CRITICAL(&stats_lock);
        if(pipeline.submit_failed){worker_finish(false);return;}
        /* PPA output is a compact ROI; retain ownership through cropped copy. */
        if(pipeline.composing){compose.job_index++;compose_advance();}
        else submit_lcd();
    }
    if(pipeline.stage==LCD_PENDING&&xSemaphoreTake(transfer_done,0)==pdTRUE){
        uint64_t processed=clock_us();
        if(!pipeline.submit_failed)verify_rotation_once();
        portENTER_CRITICAL(&stats_lock);uint64_t done=lcd_completed_at;
        account_callback_delay(LCD_PENDING,done,processed);
        uint64_t elapsed=done-pipeline.render_start;
        stats.wait_us+=done>pipeline.lcd_submit_return?done-pipeline.lcd_submit_return:0;stats.refreshes++;stats.refresh_us+=elapsed;
        if(elapsed>stats.refresh_max_us)stats.refresh_max_us=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
        if(pipeline.interactive){uint64_t age=done-pipeline.sample_us;stats.sample_lcd_count++;stats.sample_lcd_us+=age;
            if(age>stats.sample_lcd_max_us)stats.sample_lcd_max_us=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;}
        if(benchmark_report.active&&pipeline.benchmark_request==benchmark_report.request_id&&
           !pipeline.timed_out&&!pipeline.submit_failed&&done-benchmark_report.started_us<25000000){
            benchmark_report.completed_frames++;benchmark_report.elapsed_us=done-benchmark_report.started_us;
            benchmark_report.lcd_copy_bytes+=(uint64_t)pipeline.lcd_w*pipeline.lcd_h*sizeof(uint16_t);
            benchmark_report.render_us+=pipeline.render_cost;benchmark_report.rotate_us+=pipeline.rotate_cost;
            benchmark_report.lcd_submit_us+=pipeline.lcd_submit_return-pipeline.submitted;
            benchmark_report.lcd_wait_us+=done>pipeline.lcd_submit_return?done-pipeline.lcd_submit_return:0;
            benchmark_report.refresh_us+=elapsed;
            if(benchmark_report.completed_frames==1)benchmark_report.cold_frame_us=elapsed;
            if(benchmark_report.completed_frames>=benchmark_report.requested_frames){
                benchmark_report.active=false;benchmark_report.finished=true;benchmark_report.reason=1;
            }
        }
        portEXIT_CRITICAL(&stats_lock);
        worker_finish(!pipeline.timed_out&&!pipeline.submit_failed);
    }
    uint64_t now=clock_us();
    if(pipeline.stage!=IDLE&&!pipeline.timed_out&&
       now-(pipeline.stage==CACHE_PENDING?compose.job_start:pipeline.stage==PPA_PENDING?(pipeline.composing?compose.job_start:pipeline.ppa_start):pipeline.submitted)>=1000000){
        pipeline.timed_out=true;worker_recovery(true);
        portENTER_CRITICAL(&stats_lock);stats.errors++;
        if(pipeline.stage==PPA_PENDING)stats.ppa_timeouts++;else if(pipeline.stage==CACHE_PENDING)stats.compose_timeouts++;else stats.lcd_timeouts++;
        portEXIT_CRITICAL(&stats_lock);
    }
}
static unsigned slot_state(unsigned index){return __atomic_load_n(&slots[index].state,__ATOMIC_ACQUIRE);}
static void slot_set(unsigned index,slot_state_t state){__atomic_store_n(&slots[index].state,(unsigned)state,__ATOMIC_RELEASE);}
static void notify_ui(void){TaskHandle_t h=__atomic_load_n(&native_task_handle,__ATOMIC_ACQUIRE);if(h)xTaskNotifyGive(h);}
static void worker_recovery(bool fault){
    /* One recovery generation fences all plans captured before this failure.
     * Unknown DMA still owns ACTIVE buffers/leases until its late EOF. */
    rotation_baseline_valid=false;
    unsigned epoch=__atomic_add_fetch(&recovery_epoch,1,__ATOMIC_ACQ_REL);
    if(fault){__atomic_store_n(&fault_epoch,epoch,__ATOMIC_RELEASE);__atomic_store_n(&worker_fault,true,__ATOMIC_RELEASE);}
    notify_ui();
}
static void worker_complete_slot(unsigned index,bool success){
    slots[index].success=success;
    /* Reservation is returned on a dropped/failed frame, not consumed. */
    if(!success&&slots[index].reference_ready){
        portENTER_CRITICAL(&stats_lock);
        if(slots[index].reference_epoch==verify_epoch&&verify_queued)verify_queued--;
        portEXIT_CRITICAL(&stats_lock);
    }
    slot_set(index,SLOT_DONE);
    /* Capacity two == total slots. A failed send cannot permit reuse: UI also
     * scans DONE states, so no lease/buffer can be lost on notification failure. */
    xQueueSend(completed_frames,&index,0);notify_ui();
}
static void worker_finish(bool success){
    if(active_slot<0)return;
    unsigned index=(unsigned)active_slot;
    rotation_baseline_valid=success&&slots[index].generation==__atomic_load_n(&recovery_epoch,__ATOMIC_ACQUIRE);
    portENTER_CRITICAL(&stats_lock);if(!gpu_finished_at)gpu_finished_at=clock_us();
    if(success)stats.worker_completed_frames++;
    portEXIT_CRITICAL(&stats_lock);
    worker_complete_slot(index,success);active_slot=-1;
    compose.view_count=compose.job_count=compose.job_index=compose.cpu_count=compose.cpu_pixels=0;
    pipeline_set(IDLE);__atomic_store_n(&worker_fault,false,__ATOMIC_RELEASE);
}
static bool slot_benchmark_valid(frame_slot_t *slot){
    if(!slot->benchmark_request)return true;
    portENTER_CRITICAL(&stats_lock);
    bool valid=benchmark_report.active&&benchmark_report.request_id==slot->benchmark_request;
    portEXIT_CRITICAL(&stats_lock);return valid;
}
static void worker_step(void){
    pipeline_poll();
    unsigned index;
    /* Drop queued stale plans even while an ACTIVE unknown DMA is retained. */
    while(xQueuePeek(ready_frames,&index,0)==pdTRUE){
        if(slots[index].generation==__atomic_load_n(&recovery_epoch,__ATOMIC_ACQUIRE)&&slot_benchmark_valid(&slots[index])&&!__atomic_load_n(&worker_fault,__ATOMIC_ACQUIRE))break;
        xQueueReceive(ready_frames,&index,0);
        portENTER_CRITICAL(&stats_lock);if(ready_count)ready_count--;portEXIT_CRITICAL(&stats_lock);
        if(!slot_benchmark_valid(&slots[index])&&slots[index].generation==__atomic_load_n(&recovery_epoch,__ATOMIC_ACQUIRE))worker_recovery(false);
        worker_complete_slot(index,false);
    }
    if(active_slot>=0||__atomic_load_n(&worker_fault,__ATOMIC_ACQUIRE))return;
    if(xQueueReceive(ready_frames,&index,0)!=pdTRUE)return;
    portENTER_CRITICAL(&stats_lock);if(ready_count)ready_count--;gpu_started_at=clock_us();gpu_finished_at=0;portEXIT_CRITICAL(&stats_lock);
    slot_set(index,SLOT_ACTIVE);active_slot=(int)index;frame_slot_t *slot=&slots[index];
    esp_async_fbcpy_handle_t copy=compose.copy;bool ready=compose.ready;
    compose=slot->plan;compose.copy=copy;compose.ready=ready;compose.copy_done=0;compose.job_index=0;
    frame=slot->pixels;
    pipeline.render_start=slot->render_start;pipeline.render_cost=slot->render_cost;
    pipeline.ppa_start=clock_us();pipeline.rotate_cost=0;pipeline.sample_us=slot->sample_us;
    pipeline.benchmark_request=slot->benchmark_request;pipeline.reference_epoch=slot->reference_epoch;
    pipeline.reference_ready=slot->reference_ready;pipeline.interactive=slot->interactive;
    pipeline.composing=true;pipeline.timed_out=pipeline.submit_failed=false;
    portENTER_CRITICAL(&stats_lock);stats.compose_frames++;portEXIT_CRITICAL(&stats_lock);
    compose_advance();pipeline_poll();
}
static void transfer_task(void *unused){
    (void)unused;__atomic_store_n(&transfer_task_handle,xTaskGetCurrentTaskHandle(),__ATOMIC_RELEASE);
    while(!__atomic_load_n(&started,__ATOMIC_ACQUIRE))ulTaskNotifyTake(pdTRUE,1);
    for(;;){
        worker_step();TickType_t ticks=pdMS_TO_TICKS(10);if(ticks<1)ticks=1;
        uint64_t begin=clock_us();ulTaskNotifyTake(pdTRUE,ticks);
        uint64_t elapsed=clock_us()-begin;
        portENTER_CRITICAL(&stats_lock);stats.wake_wait_us+=elapsed;portEXIT_CRITICAL(&stats_lock);
    }
}
static bool transport_idle(void){
    return slot_state(0)==SLOT_FREE&&slot_state(1)==SLOT_FREE;
}
static void debt_add(frame_slot_t *slot,const nui_area_t *areas,size_t count){
    if(slot->debt_full)return;
    if(count>48){slot->debt_full=true;slot->debt_count=0;return;}
    for(size_t i=0;i<count;i++){
        if(!area_valid(areas[i])||slot->debt_count>=48){slot->debt_full=true;slot->debt_count=0;return;}
        slot->debt[slot->debt_count++]=areas[i];
    }
}
static void ui_reap_locked(void){
    unsigned index;
    while(xQueueReceive(completed_frames,&index,0)==pdTRUE){} /* DONE scan is authoritative. */
    for(index=0;index<2;index++)if(slot_state(index)==SLOT_DONE){
        collecting=&slots[index].plan;compose_release_locked();
        if(!slots[index].success){slots[index].initialized=slots[index].reference_clean=false;slots[index].debt_full=true;}
        slot_set(index,SLOT_FREE);
    }
    collecting=NULL;
}
static uint64_t render_overlap(uint64_t begin,uint64_t end){
    portENTER_CRITICAL(&stats_lock);uint64_t gpu_begin=gpu_started_at,gpu_end=gpu_finished_at;
    if(!gpu_end||gpu_end>end)gpu_end=end;
    if(gpu_begin<begin)gpu_begin=begin;
    uint64_t overlap=gpu_started_at&&gpu_end>gpu_begin?gpu_end-gpu_begin:0;
    stats.overlap_us+=overlap;portEXIT_CRITICAL(&stats_lock);return overlap;
}
static bool ui_prepare_locked(unsigned index,uint64_t start,bool interactive,uint64_t sample_us,bool bench_run){
    frame_slot_t *slot=&slots[index];slot_set(index,SLOT_BUILDING);
    unsigned generation=__atomic_load_n(&recovery_epoch,__ATOMIC_ACQUIRE);
    nui_area_t delta[48],full={0,0,WIDTH-1,HEIGHT-1};
    size_t count=nui_native_get_pending_dirty(delta,48);
    if(!count){slot_set(index,SLOT_FREE);return false;}
    portENTER_CRITICAL(&stats_lock);unsigned epoch=verify_epoch;
    bool verify=verify_requested>verify_queued;
    if(verify)verify_queued++;
    portEXIT_CRITICAL(&stats_lock);
    bool baseline=verify&&reference_slot>=0&&reference_slot_epoch==epoch&&slots[reference_slot].reference_clean;
    if(baseline&&reference_slot!=(int)index){
        /* Diagnostic only: independent landscape baseline, never portrait or
         * canonical. Source can be ACTIVE/READY/read-only, target is FREE. */
        uint64_t begin=clock_us();memcpy(slot->pixels,slots[reference_slot].pixels,FRAME_BYTES);
        slot->reference_clean=slot->initialized=true;slot->debt_count=0;slot->debt_full=false;
        portENTER_CRITICAL(&stats_lock);stats.verify_us+=clock_us()-begin;portEXIT_CRITICAL(&stats_lock);
    }
    if(reference_slot==(int)index&&!verify)reference_slot=-1;
    if(!nui_native_bind_framebuffer(slot->pixels)){
        if(verify){portENTER_CRITICAL(&stats_lock);if(epoch==verify_epoch&&verify_queued)verify_queued--;portEXIT_CRITICAL(&stats_lock);}
        slot_set(index,SLOT_FREE);return false;
    }
    bool full_reference=!slot->initialized||(verify&&(!baseline||!slot->reference_clean))||slot->debt_full;
    if(full_reference)nui_native_invalidate(&full);
    else for(unsigned i=0;i<slot->debt_count;i++)nui_native_invalidate(&slot->debt[i]);
    slot->debt_count=0;slot->debt_full=false;
    collecting=&slot->plan;*collecting=(compose_state_t){.ready=composer_available};
    if(composer_available)nui_native_set_dirty_alignment(16);
    collecting->collecting=composer_available;
    uint64_t render_begin=clock_us();size_t dirty=nui_native_render();
    uint64_t overlap=render_overlap(render_begin,clock_us());collecting->collecting=false;
    if(dirty&&!compose_plan()){
        compose_release_locked();nui_native_set_surface_defer_cb(NULL,NULL);
        nui_native_invalidate(&full);render_begin=clock_us();dirty=nui_native_render();overlap+=render_overlap(render_begin,clock_us());
        nui_native_set_surface_defer_cb(composer_available?compose_accept:NULL,NULL);
        compose_append(full,-1);collecting->dirty[0]=full;collecting->dirty_count=1;
        full_reference=true;
        portENTER_CRITICAL(&stats_lock);stats.compose_fallbacks++;portEXIT_CRITICAL(&stats_lock);
    }
    bool reference=slot->reference_clean||full_reference;
    if(collecting->view_count){
        if(verify){
            uint64_t begin=clock_us();
            for(unsigned i=0;i<collecting->view_count;i++)if(!nui_surface_view_materialize(&collecting->views[i]))reference=false;
            portENTER_CRITICAL(&stats_lock);stats.verify_us+=clock_us()-begin;if(!reference)stats.verify_errors++;portEXIT_CRITICAL(&stats_lock);
        }else reference=false;
    }
    slot->reference_clean=reference;slot->reference_ready=verify&&reference;slot->reference_epoch=epoch;
    if(slot->reference_ready){reference_slot=(int)index;reference_slot_epoch=epoch;}
    if(verify&&!slot->reference_ready){portENTER_CRITICAL(&stats_lock);if(epoch==verify_epoch&&verify_queued)verify_queued--;portEXIT_CRITICAL(&stats_lock);}
    debt_add(&slots[1-index],delta,count);slot->initialized=true;
    if(!dirty||generation!=__atomic_load_n(&recovery_epoch,__ATOMIC_ACQUIRE)){
        if(slot->reference_ready){portENTER_CRITICAL(&stats_lock);if(epoch==verify_epoch&&verify_queued)verify_queued--;portEXIT_CRITICAL(&stats_lock);}
        compose_release_locked();slot->initialized=slot->reference_clean=false;slot->debt_full=true;
        nui_native_invalidate(&full);slot_set(index,SLOT_FREE);collecting=NULL;return false;
    }
    slot->generation=generation;slot->render_start=start;slot->render_cost=clock_us()-start;
    slot->interactive=interactive;slot->sample_us=sample_us;slot->benchmark_request=bench_run?benchmark.id:0;
    slot->benchmark_position=benchmark.position;
    portENTER_CRITICAL(&stats_lock);stats.renders++;stats.render_us+=slot->render_cost;if(overlap)stats.overlap_renders++;
    if(interactive){uint64_t wait=start-sample_us;stats.interactive_renders++;stats.sample_render_count++;stats.sample_render_us+=wait;
        if(wait>stats.input_render_wait_max_us)stats.input_render_wait_max_us=wait>UINT32_MAX?UINT32_MAX:(uint32_t)wait;
        if(wait>stats.sample_render_max_us)stats.sample_render_max_us=wait>UINT32_MAX?UINT32_MAX:(uint32_t)wait;}
    if(bench_run&&benchmark_report.active&&benchmark_report.request_id==benchmark.id){benchmark_report.accepted_frames++;benchmark.needs_step=benchmark_report.accepted_frames<benchmark_report.requested_frames;}
    portEXIT_CRITICAL(&stats_lock);
    portENTER_CRITICAL(&stats_lock);ready_count++;if(ready_count>stats.queued_peak)stats.queued_peak=ready_count;portEXIT_CRITICAL(&stats_lock);
    slot_set(index,SLOT_READY);collecting=NULL;
    if(xQueueSend(ready_frames,&index,0)!=pdTRUE){
        portENTER_CRITICAL(&stats_lock);if(ready_count)ready_count--;portEXIT_CRITICAL(&stats_lock);
        slot_set(index,SLOT_DONE);slot->success=false;worker_recovery(false);nui_native_invalidate(&full);return false;
    }
    TaskHandle_t h=__atomic_load_n(&transfer_task_handle,__ATOMIC_ACQUIRE);if(h)xTaskNotifyGive(h);
    return true;
}
static struct {
    uint32_t last_ms;unsigned seen_recovery;
    uint64_t last_render,last_touch_read,input_since;
    bool was_pressed,input_requested,pressed,touch_sampled;int x,y;
} producer;
static void producer_reset(void){
    producer=(__typeof__(producer)){.last_ms=(uint32_t)(clock_us()/1000),.seen_recovery=__atomic_load_n(&recovery_epoch,__ATOMIC_ACQUIRE)};
}
static void producer_step(void){
        uint64_t read_at=clock_us();bool fresh_touch=!producer.touch_sampled||read_at-producer.last_touch_read>=UINT64_C(10000);
        if(fresh_touch){
            portENTER_CRITICAL(&stats_lock);stats.touch_reads++;if(benchmark_report.active)benchmark_report.touch_reads++;
            if(producer.touch_sampled){uint64_t gap=read_at-producer.last_touch_read;if(gap>stats.touch_read_gap_max_us)stats.touch_read_gap_max_us=gap>UINT32_MAX?UINT32_MAX:(uint32_t)gap;}
            portEXIT_CRITICAL(&stats_lock);producer.last_touch_read=read_at;producer.touch_sampled=true;
            uint16_t tx=0,ty=0,strength=0;uint8_t points=0;uint64_t begin=clock_us();
            producer.pressed=esp_lcd_touch_read_data(touch)==ESP_OK&&esp_lcd_touch_get_coordinates(touch,&tx,&ty,&strength,&points,1)&&points;
            uint64_t elapsed=clock_us()-begin;portENTER_CRITICAL(&stats_lock);stats.touch_read_us+=elapsed;portEXIT_CRITICAL(&stats_lock);
            if(producer.pressed)producer.pressed=native_touch_position(tx,ty,&producer.x,&producer.y);
            if(producer.pressed||producer.was_pressed){if(!producer.input_requested)producer.input_since=clock_us();producer.input_requested=true;}
        }
        task_ui_lock();ui_reap_locked();
        unsigned current=__atomic_load_n(&recovery_epoch,__ATOMIC_ACQUIRE);
        if(current!=producer.seen_recovery){
            nui_area_t full={0,0,WIDTH-1,HEIGHT-1};nui_native_invalidate(&full);producer.seen_recovery=current;reference_slot=-1;
            for(unsigned i=0;i<2;i++){slots[i].initialized=slots[i].reference_clean=false;slots[i].debt_full=true;}
            if(__atomic_load_n(&fault_epoch,__ATOMIC_ACQUIRE)==current){
                tab5_native_scroll_benchmark_status_t failed;tab5_native_port_scroll_benchmark_status(&failed);
                if(failed.active||failed.pending)benchmark_finish(6,failed.request_id);
            }
        }
        uint64_t dispatch_start=clock_us();bool encoder=false;
#ifdef ESP_PLATFORM
        INPUT_LOCK();for(unsigned i=0;i<input_queue.count;i++){native_input_t *e=&input_queue.events[(input_queue.head+i)%TAB5_NATIVE_INPUT_CAPACITY];if(e->kind!=TAB5_UI_INPUT_TOUCH&&e->kind!=TAB5_UI_INPUT_IDLE)encoder=true;}INPUT_UNLOCK();
#endif
        bool suppress=benchmark_control(producer.pressed,encoder);
        uint32_t ms=(uint32_t)(clock_us()/1000);light_begin(ms);uint64_t tick_start=clock_us();nui_native_tick(ms-producer.last_ms);producer.last_ms=ms;uint64_t tick_end=clock_us();
#ifdef ESP_PLATFORM
        if(suppress){INPUT_LOCK();input_queue.stats.consumed+=input_queue.count;input_queue.head=input_queue.count=0;INPUT_UNLOCK();}
        else {if(fresh_touch&&(producer.pressed||producer.was_pressed)&&tab5_native_port_post_touch(producer.x,producer.y,producer.pressed,ms))producer.was_pressed=producer.pressed;
            if(input_dispatch(ms,false)){if(!producer.input_requested)producer.input_since=clock_us();producer.input_requested=true;}}
#else
        if(!suppress&&fresh_touch){
            if(!tab5_backlight_step(&backlight,light_extend(ms),producer.pressed?TAB5_BL_TOUCH_DOWN:TAB5_BL_TOUCH_UP))nui_native_touch(producer.x,producer.y,producer.pressed);
            producer.was_pressed=producer.pressed;
        }
#endif
        if(suppress){producer.was_pressed=false;producer.input_requested=false;}
        uint64_t start=clock_us();portENTER_CRITICAL(&stats_lock);stats.cpu_tick_us+=tick_end-tick_start;
        stats.input_dispatch_us+=(tick_start-dispatch_start)+(start-tick_end);portEXIT_CRITICAL(&stats_lock);
        bool bench_run=benchmark_running();tab5_native_scroll_benchmark_status_t report;tab5_native_port_scroll_benchmark_status(&report);
        uint64_t interval=bench_run?benchmark.interval:producer.input_requested?INTERACTION_RENDER_US:PERIODIC_RENDER_US;
        if(!__atomic_load_n(&worker_fault,__ATOMIC_ACQUIRE)&&start-producer.last_render>=interval&&(!bench_run||report.accepted_frames<report.requested_frames)){
            int free_slot=-1;for(unsigned i=0;i<2;i++)if(slot_state(i)==SLOT_FREE){free_slot=(int)i;break;}
            if(free_slot>=0){if(ui_prepare_locked((unsigned)free_slot,start,producer.input_requested,producer.input_since,bench_run)){producer.last_render=start;producer.input_requested=false;}}
            else {portENTER_CRITICAL(&stats_lock);stats.producer_backpressure++;portEXIT_CRITICAL(&stats_lock);}
        }
        xSemaphoreGive(ui_mutex);
        tab5_backlight_step(&backlight,light_extend(ms),TAB5_BL_IDLE);
        /* UI task owns policy/output; BSP writes never hold the UI mutex. */
        int desired=backlight.asleep?0:light_manual;
        if(desired!=light_applied&&bsp_display_brightness_set(desired)==ESP_OK)light_applied=desired;
}
static void native_task(void *arg){
    (void)arg;__atomic_store_n(&native_task_handle,xTaskGetCurrentTaskHandle(),__ATOMIC_RELEASE);
    while(!__atomic_load_n(&started,__ATOMIC_ACQUIRE))ulTaskNotifyTake(pdTRUE,1);
    producer_reset();
    for(;;){producer_step();TickType_t ticks=pdMS_TO_TICKS(10);if(ticks<1)ticks=1;ulTaskNotifyTake(pdTRUE,ticks);}
}
static void startup_cleanup(bool renderer_ready,bool display_attempted) {
    /* Startup cleanup only. A live/unknown DMA owner must never be freed. */
    if(active_slot>=0||pipeline.stage!=IDLE||__atomic_load_n(&native_task_handle,__ATOMIC_ACQUIRE)||__atomic_load_n(&transfer_task_handle,__ATOMIC_ACQUIRE))return;
    if(ui_mutex){task_ui_lock();for(unsigned i=0;i<2;i++){collecting=&slots[i].plan;compose_release_locked();}collecting=NULL;nui_native_set_surface_defer_cb(NULL,NULL);xSemaphoreGive(ui_mutex);}
    if(compose.copy){esp_async_fbcpy_uninstall(compose.copy);compose.copy=NULL;}compose.ready=false;
    rotation_baseline_valid=false;source_flush_reset();reference_slot=-1;ready_count=0;verify_queued=0;
    __atomic_store_n(&worker_fault,false,__ATOMIC_RELEASE);
    portENTER_CRITICAL(&stats_lock);stats.worker_enabled=false;portEXIT_CRITICAL(&stats_lock);
    __atomic_store_n(&verify_requested,0,__ATOMIC_RELEASE);
#ifdef ESP_PLATFORM
    tab5_native_port_input_close();
#endif
    if(renderer_ready){tab5_ui_destroy();nui_native_deinit();}
    if(touch){esp_lcd_panel_io_handle_t io=touch->io;esp_lcd_touch_del(touch);touch=NULL;if(io)esp_lcd_panel_io_del(io);}
    if(display_attempted){bsp_display_delete();bsp_feature_enable(BSP_FEATURE_TOUCH,false);bsp_feature_enable(BSP_FEATURE_LCD,false);}
    panel=NULL;
    if(ppa){ppa_unregister_client(ppa);ppa=NULL;}
    for(unsigned i=0;i<2;i++){free(slots[i].pixels);slots[i]=(frame_slot_t){0};}
    free(rotated);frame=rotated=NULL;
    if(ready_frames){vQueueDelete(ready_frames);ready_frames=NULL;}
    if(completed_frames){vQueueDelete(completed_frames);completed_frames=NULL;}
    if(transfer_done){vSemaphoreDelete(transfer_done);transfer_done=NULL;}
    if(ppa_done){vSemaphoreDelete(ppa_done);ppa_done=NULL;}
    if(ui_mutex){vSemaphoreDelete(ui_mutex);ui_mutex=NULL;}
}
esp_err_t tab5_native_port_start(tab5_ui_action_cb_t callback,void *user) {
    if(started)return ESP_ERR_INVALID_STATE;
    source_flush_reset();tab5_panel_observer_reset();
    esp_err_t e=ESP_ERR_NO_MEM;bool renderer_ready=false,display_attempted=false;
    ui_mutex=xSemaphoreCreateMutex();transfer_done=xSemaphoreCreateBinary();ppa_done=xSemaphoreCreateBinary();
    if(!ui_mutex||!transfer_done||!ppa_done)goto fail;
    ready_frames=xQueueCreate(2,sizeof(unsigned));completed_frames=xQueueCreate(2,sizeof(unsigned));
    if(!ready_frames||!completed_frames)goto fail;
    for(unsigned i=0;i<2;i++)slots[i].pixels=heap_caps_aligned_alloc(64,FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    frame=slots[0].pixels;
    rotated=heap_caps_aligned_alloc(64,FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!slots[0].pixels||!slots[1].pixels||!rotated)goto fail;
    bsp_lcd_handles_t handles={0};bsp_display_config_t cfg={0};
    display_attempted=true;e=bsp_display_new_with_handles(&cfg,&handles);
    if(e!=ESP_OK)goto fail;
    panel=handles.panel;
    if((e=bsp_touch_new(NULL,&touch))!=ESP_OK)goto fail;
    esp_lcd_dpi_panel_event_callbacks_t cbs={.on_color_trans_done=transfer_finished};
    if((e=esp_lcd_dpi_panel_register_event_callbacks(panel,&cbs,NULL))!=ESP_OK)goto fail;
    ppa_client_config_t pcfg={.oper_type=PPA_OPERATION_SRM};
    if((e=ppa_register_client(&pcfg,&ppa))!=ESP_OK)goto fail;
    ppa_event_callbacks_t ppa_cbs={.on_trans_done=ppa_finished};
    if((e=ppa_client_register_event_callbacks(ppa,&ppa_cbs))!=ESP_OK)goto fail;
    if((e=esp_lcd_panel_disp_on_off(panel,true))!=ESP_OK)goto fail;
    if(!tab5_panel_ready()){e=ESP_ERR_INVALID_STATE;goto fail;}
    esp_async_fbcpy_config_t copy_config={};
    if(esp_async_fbcpy_install(&copy_config,&compose.copy)==ESP_OK)compose.ready=composer_available=true;
    else {compose.copy=NULL;compose.ready=composer_available=false;}
    renderer_ready=true;
    if(!nui_native_init(WIDTH,HEIGHT,frame)){e=ESP_ERR_NO_MEM;goto fail;}
    if(composer_available)nui_native_set_surface_alignment(1);
    nui_native_set_clock_cb(clock_us);nui_native_set_surface_defer_cb(composer_available?compose_accept:NULL,NULL);tab5_ui_init(callback,user);
    if(!tab5_native_port_set_scroll_cache_hooks(tab5_ui_scroll_cache_enabled,
            tab5_ui_set_scroll_cache_enabled,tab5_ui_clear_scroll_cache)){
        e=ESP_ERR_INVALID_STATE;goto fail;
    }
#ifdef ESP_PLATFORM
    tab5_native_port_input_reset();
#endif
    if((e=bsp_display_brightness_set(60))!=ESP_OK)goto fail;
    light_applied=60;
    TaskHandle_t ui_created=NULL,worker_created=NULL;
    if(xTaskCreatePinnedToCore(native_task,"native_ui",8192,NULL,4,&ui_created,1)!=pdPASS){e=ESP_ERR_NO_MEM;goto fail;}
    if(xTaskCreatePinnedToCore(transfer_task,"native_gpu",8192,NULL,5,&worker_created,1)!=pdPASS){
        /* UI entry is gated on started: no renderer/DMA resources used yet. */
        vTaskDelete(ui_created);__atomic_store_n(&native_task_handle,NULL,__ATOMIC_RELEASE);e=ESP_ERR_NO_MEM;goto fail;
    }
    portENTER_CRITICAL(&stats_lock);stats.worker_enabled=true;portEXIT_CRITICAL(&stats_lock);
    __atomic_store_n(&started,true,__ATOMIC_RELEASE);xTaskNotifyGive(ui_created);xTaskNotifyGive(worker_created);return ESP_OK;
fail:
    startup_cleanup(renderer_ready,display_attempted);return e;
}
#endif
