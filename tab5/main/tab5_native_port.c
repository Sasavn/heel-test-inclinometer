#include "tab5_native_port.h"
#if defined(ESP_PLATFORM) || defined(TAB5_NATIVE_INPUT_ONLY)
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
    bool pressed;
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
void tab5_native_port_input_reset(void) {
    INPUT_LOCK();input_queue.head=input_queue.count=0;
    input_queue.stats=(tab5_native_input_stats_t){0};input_queue.open=true;
    INPUT_UNLOCK();input_x=input_y=0;input_pressed=false;
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
bool tab5_native_port_post_touch(int x,int y,bool pressed,uint32_t ms) {
    return input_post((native_input_t){.kind=TAB5_UI_INPUT_TOUCH,.ms=ms,.x=x,.y=y,.pressed=pressed});
}
void tab5_native_port_input_stats(tab5_native_input_stats_t *out) {
    if(!out)return;
    INPUT_LOCK();*out=input_queue.stats;out->pending=input_queue.count;INPUT_UNLOCK();
}
bool tab5_native_port_input_dispatch(uint32_t now_ms) {
    bool changed=false,touched=false;
    for(unsigned i=0;i<TAB5_NATIVE_INPUT_CAPACITY;i++) {
        native_input_t e;
        INPUT_LOCK();
        bool available=input_queue.open&&input_queue.count;
        if(available){e=input_queue.events[input_queue.head];input_queue.head=(input_queue.head+1)%TAB5_NATIVE_INPUT_CAPACITY;input_queue.count--;input_queue.stats.consumed++;}
        INPUT_UNLOCK();
        if(!available)break;
        changed=true;
        if(e.kind==TAB5_UI_INPUT_TOUCH){
            input_x=e.x;input_y=e.y;input_pressed=e.pressed;touched=true;
            tab5_ui_touch(input_x,input_y,input_pressed,e.ms);
        }else tab5_ui_input(e.kind,e.delta,e.ms);
    }
    if(!touched)tab5_ui_touch(input_x,input_y,input_pressed,now_ms);
    tab5_ui_input(TAB5_UI_INPUT_IDLE,0,now_ms);
    return changed;
}
#endif

#ifndef TAB5_NATIVE_INPUT_ONLY
#include "tab5_panel.h"
#include "native/native_ui.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "driver/ppa.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_timer.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define WIDTH 1280
#define HEIGHT 720
#define FRAME_BYTES (WIDTH*HEIGHT*sizeof(uint16_t))
/* Acquisition/UI state still updates at 10 Hz in main.c. Only an observed
 * interaction can request the shorter, bounded display interval. No second
 * render or rotation is allowed while the previous LCD transfer owns buffers. */
#define PERIODIC_RENDER_US UINT64_C(100000)
#define INTERACTION_RENDER_US UINT64_C(40000)
static SemaphoreHandle_t ui_mutex,transfer_done,ppa_done;
static esp_lcd_panel_handle_t panel;
static esp_lcd_touch_handle_t touch;
static ppa_client_handle_t ppa;
static uint16_t *frame,*rotated;
static portMUX_TYPE stats_lock=portMUX_INITIALIZER_UNLOCKED;
static tab5_native_port_stats_t stats;
static bool started;
static unsigned verify_requested;
typedef enum { IDLE=0,PPA_PENDING=1,LCD_PENDING=2 } pipeline_stage_t;
static struct {
    pipeline_stage_t stage;
    uint64_t render_start,ppa_start,submitted,lcd_submit_return,sample_us;
    bool interactive,timed_out,submit_failed;
} pipeline;
static uint64_t ppa_completed_at,lcd_completed_at;
/* Candidate: RGB565 dirty union, conservatively expanded to rev1 16x16
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
    if(reset){uint32_t state=stats.pipeline_state;stats=(tab5_native_port_stats_t){0};stats.pipeline_state=state;}
    if(out)*out=stats;
    portEXIT_CRITICAL(&stats_lock);
}
static uint64_t clock_us(void);
/* Diagnostic only: 1..2 future successful rotations, never a startup scan.
 * UI task owns both buffers here: PPA complete, no LCD transfer pending. */
bool tab5_native_port_request_verify(unsigned frames){
    if(frames<1||frames>2||!__atomic_load_n(&started,__ATOMIC_ACQUIRE))return false;
    __atomic_store_n(&verify_requested,frames,__ATOMIC_RELEASE);return true;
}
static uint64_t native_rotation_mismatches(const uint16_t *src,const uint16_t *dst){
    uint64_t mismatches=0;
    for(unsigned y=0;y<HEIGHT;y++)for(unsigned x=0;x<WIDTH;x++)
        mismatches+=dst[(WIDTH-1-x)*HEIGHT+y]!=src[y*WIDTH+x];
    return mismatches;
}
static uint64_t verify_rotation_once(void){
    unsigned count=__atomic_load_n(&verify_requested,__ATOMIC_ACQUIRE);
    do {if(!count)return 0;}while(!__atomic_compare_exchange_n(&verify_requested,&count,count-1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
    uint64_t begin=clock_us();
    /* Read actual DMA-written memory, including retained pixels outside ROI. */
    esp_err_t error=esp_cache_msync(rotated,FRAME_BYTES,ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    uint64_t mismatches=error==ESP_OK?native_rotation_mismatches(frame,rotated):0;
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
static bool ppa_finished(ppa_client_handle_t client,ppa_event_data_t *event,void *ctx){
    (void)client;(void)event;(void)ctx;BaseType_t wake=pdFALSE;
    portENTER_CRITICAL_ISR(&stats_lock);ppa_completed_at=clock_us();portEXIT_CRITICAL_ISR(&stats_lock);
    xSemaphoreGiveFromISR(ppa_done,&wake);return wake==pdTRUE;
}
static bool transfer_finished(esp_lcd_panel_handle_t handle,esp_lcd_dpi_panel_event_data_t *event,void *ctx){
    (void)handle;(void)event;(void)ctx;BaseType_t wake=pdFALSE;
    portENTER_CRITICAL_ISR(&stats_lock);lcd_completed_at=clock_us();portEXIT_CRITICAL_ISR(&stats_lock);
    xSemaphoreGiveFromISR(transfer_done,&wake);return wake==pdTRUE;
}
static uint64_t clock_us(void){return (uint64_t)esp_timer_get_time();}
static void invalidate_full(void){
    rotation_baseline_valid=false;nui_area_t full={0,0,WIDTH-1,HEIGHT-1};
    xSemaphoreTake(ui_mutex,portMAX_DELAY);nui_native_invalidate(&full);xSemaphoreGive(ui_mutex);
}
static void pipeline_error(esp_err_t error){
    portENTER_CRITICAL(&stats_lock);stats.errors++;stats.submit_errors++;portEXIT_CRITICAL(&stats_lock);
    pipeline.submit_failed=true;rotation_baseline_valid=false;
    /* INVALID_ARG is rejected before DMA submission in the pinned PPA/DPI
     * drivers. Any other failure has unknown ownership: retain both buffers
     * until the corresponding completion; no cancel/free/retry on a timeout. */
    if(error==ESP_ERR_INVALID_ARG){pipeline_set(IDLE);invalidate_full();}
}
static void pipeline_poll(void){
    if(pipeline.stage==PPA_PENDING&&xSemaphoreTake(ppa_done,0)==pdTRUE){
        portENTER_CRITICAL(&stats_lock);uint64_t done=ppa_completed_at;
        stats.rotate_us+=done-pipeline.ppa_start;portEXIT_CRITICAL(&stats_lock);
        if(pipeline.submit_failed){pipeline_set(IDLE);invalidate_full();return;}
        /* PPA has stopped reading frame/writing rotated. Verify before LCD. */
        verify_rotation_once();
        xSemaphoreTake(transfer_done,0);
        pipeline.submitted=clock_us();pipeline_set(LCD_PENDING);
        esp_err_t error=esp_lcd_panel_draw_bitmap(panel,0,0,HEIGHT,WIDTH,rotated);
        uint64_t submitted=clock_us();pipeline.lcd_submit_return=submitted;
        portENTER_CRITICAL(&stats_lock);stats.flush_us+=submitted-pipeline.submitted;
        if(error==ESP_OK)stats.flushes++;
        portEXIT_CRITICAL(&stats_lock);
        if(error!=ESP_OK)pipeline_error(error);
    }
    if(pipeline.stage==LCD_PENDING&&xSemaphoreTake(transfer_done,0)==pdTRUE){
        portENTER_CRITICAL(&stats_lock);uint64_t done=lcd_completed_at;
        uint64_t elapsed=done-pipeline.render_start;
        stats.wait_us+=done>pipeline.lcd_submit_return?done-pipeline.lcd_submit_return:0;stats.refreshes++;stats.refresh_us+=elapsed;
        if(elapsed>stats.refresh_max_us)stats.refresh_max_us=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
        if(pipeline.interactive){uint64_t age=done-pipeline.sample_us;stats.sample_lcd_count++;stats.sample_lcd_us+=age;
            if(age>stats.sample_lcd_max_us)stats.sample_lcd_max_us=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;}
        portEXIT_CRITICAL(&stats_lock);
        bool recover=pipeline.timed_out||pipeline.submit_failed;pipeline_set(IDLE);
        if(recover)invalidate_full();else rotation_baseline_valid=true;
    }
    uint64_t now=clock_us();
    if(pipeline.stage!=IDLE&&!pipeline.timed_out&&
       now-(pipeline.stage==PPA_PENDING?pipeline.ppa_start:pipeline.submitted)>=1000000){
        pipeline.timed_out=true;rotation_baseline_valid=false;
        portENTER_CRITICAL(&stats_lock);stats.errors++;
        if(pipeline.stage==PPA_PENDING)stats.ppa_timeouts++;else stats.lcd_timeouts++;
        portEXIT_CRITICAL(&stats_lock);
    }
}
static void native_task(void *arg){
    (void)arg;uint32_t last_ms=(uint32_t)(clock_us()/1000);
    uint64_t last_render=0,last_touch_read=0,input_since=0;
    bool was_pressed=false,input_requested=false;int x=0,y=0;
    for(;;){
        pipeline_poll();
        uint64_t read_at=clock_us();
        portENTER_CRITICAL(&stats_lock);stats.touch_reads++;
        if(last_touch_read){uint64_t gap=read_at-last_touch_read;
            if(gap>stats.touch_read_gap_max_us)stats.touch_read_gap_max_us=gap>UINT32_MAX?UINT32_MAX:(uint32_t)gap;}
        portEXIT_CRITICAL(&stats_lock);last_touch_read=read_at;
        uint16_t tx=0,ty=0,strength=0;uint8_t points=0;
        bool pressed=esp_lcd_touch_read_data(touch)==ESP_OK&&
            esp_lcd_touch_get_coordinates(touch,&tx,&ty,&strength,&points,1)&&points;
        if(pressed)pressed=native_touch_position(tx,ty,&x,&y);
        if(pressed||was_pressed){if(!input_requested)input_since=clock_us();input_requested=true;}
#ifdef ESP_PLATFORM
        if((pressed||was_pressed)&&tab5_native_port_post_touch(x,y,pressed,(uint32_t)(clock_us()/1000)))was_pressed=pressed;
#else
        was_pressed=pressed;
#endif
        xSemaphoreTake(ui_mutex,portMAX_DELAY);
        uint32_t ms=(uint32_t)(clock_us()/1000);nui_native_tick(ms-last_ms);last_ms=ms;
#ifdef ESP_PLATFORM
        if(tab5_native_port_input_dispatch(ms)){if(!input_requested)input_since=clock_us();input_requested=true;}
#else
        nui_native_touch(x,y,pressed);
#endif
        uint64_t start=clock_us();size_t dirty=0;
        nui_area_t rotation_area={0,0,WIDTH-1,HEIGHT-1};bool interactive=input_requested;
        uint64_t interval=interactive?INTERACTION_RENDER_US:PERIODIC_RENDER_US;
        uint64_t input_wait=interactive?start-input_since:0;
        if(pipeline.stage==IDLE&&start-last_render>=interval){
            last_render=start;dirty=nui_native_render();
            if(dirty&&rotation_baseline_valid){nui_area_t areas[ROTATION_DIRTY_CAPACITY];
                size_t count=nui_native_get_dirty(areas,ROTATION_DIRTY_CAPACITY);
                native_rotation_bounds(areas,count,&rotation_area);}
            input_requested=false;
        }
        uint64_t rendered=clock_us();xSemaphoreGive(ui_mutex);
        if(dirty){
            pipeline.render_start=start;pipeline.ppa_start=rendered;pipeline.sample_us=input_since;
            pipeline.interactive=interactive;pipeline.timed_out=false;pipeline.submit_failed=false;
            portENTER_CRITICAL(&stats_lock);stats.renders++;stats.render_us+=rendered-start;
            if(interactive){stats.interactive_renders++;stats.sample_render_count++;stats.sample_render_us+=input_wait;
                if(input_wait>stats.input_render_wait_max_us)stats.input_render_wait_max_us=input_wait>UINT32_MAX?UINT32_MAX:(uint32_t)input_wait;
                if(input_wait>stats.sample_render_max_us)stats.sample_render_max_us=input_wait>UINT32_MAX?UINT32_MAX:(uint32_t)input_wait;}
            portEXIT_CRITICAL(&stats_lock);
            xSemaphoreTake(ppa_done,0);pipeline_set(PPA_PENDING);
            /* IDF copies this config into its transaction before returning.
             * The two pixel buffers remain exclusively owned until both EOFs. */
            ppa_srm_oper_config_t op={
                .in={.buffer=frame,.pic_w=WIDTH,.pic_h=HEIGHT,.block_w=(uint32_t)(rotation_area.x2-rotation_area.x1+1),.block_h=(uint32_t)(rotation_area.y2-rotation_area.y1+1),.block_offset_x=(uint32_t)rotation_area.x1,.block_offset_y=(uint32_t)rotation_area.y1,.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
                .out={.buffer=rotated,.buffer_size=FRAME_BYTES,.pic_w=HEIGHT,.pic_h=WIDTH,.block_offset_x=(uint32_t)rotation_area.y1,.block_offset_y=(uint32_t)(WIDTH-1-rotation_area.x2),.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
                .rotation_angle=PPA_SRM_ROTATION_ANGLE_90,.scale_x=1,.scale_y=1,.mode=PPA_TRANS_MODE_NON_BLOCKING};
            esp_err_t error=ppa_do_scale_rotate_mirror(ppa,&op);
            if(error!=ESP_OK)pipeline_error(error);
            pipeline_poll(); /* Includes immediate callback completion. */
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
static void startup_cleanup(bool renderer_ready,bool display_attempted) {
    /* Startup cleanup only. A live/unknown DMA owner must never be freed. */
    if(pipeline.stage!=IDLE)return;
    rotation_baseline_valid=false;
    __atomic_store_n(&verify_requested,0,__ATOMIC_RELEASE);
#ifdef ESP_PLATFORM
    tab5_native_port_input_close();
#endif
    if(renderer_ready){tab5_ui_destroy();nui_native_deinit();}
    if(touch){esp_lcd_panel_io_handle_t io=touch->io;esp_lcd_touch_del(touch);touch=NULL;if(io)esp_lcd_panel_io_del(io);}
    if(display_attempted){bsp_display_delete();bsp_feature_enable(BSP_FEATURE_TOUCH,false);bsp_feature_enable(BSP_FEATURE_LCD,false);}
    panel=NULL;
    if(ppa){ppa_unregister_client(ppa);ppa=NULL;}
    free(frame);free(rotated);frame=rotated=NULL;
    if(transfer_done){vSemaphoreDelete(transfer_done);transfer_done=NULL;}
    if(ppa_done){vSemaphoreDelete(ppa_done);ppa_done=NULL;}
    if(ui_mutex){vSemaphoreDelete(ui_mutex);ui_mutex=NULL;}
}
esp_err_t tab5_native_port_start(tab5_ui_action_cb_t callback,void *user) {
    if(started)return ESP_ERR_INVALID_STATE;
    tab5_panel_observer_reset();
    esp_err_t e=ESP_ERR_NO_MEM;bool renderer_ready=false,display_attempted=false;
    ui_mutex=xSemaphoreCreateMutex();transfer_done=xSemaphoreCreateBinary();ppa_done=xSemaphoreCreateBinary();
    if(!ui_mutex||!transfer_done||!ppa_done)goto fail;
    frame=heap_caps_aligned_alloc(64,FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    rotated=heap_caps_aligned_alloc(64,FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!frame||!rotated)goto fail;
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
    renderer_ready=true;
    if(!nui_native_init(WIDTH,HEIGHT,frame)){e=ESP_ERR_NO_MEM;goto fail;}
    nui_native_set_clock_cb(clock_us);tab5_ui_init(callback,user);
#ifdef ESP_PLATFORM
    tab5_native_port_input_reset();
#endif
    if((e=bsp_display_brightness_set(60))!=ESP_OK)goto fail;
    if(xTaskCreatePinnedToCore(native_task,"native_ui",8192,NULL,4,NULL,1)!=pdPASS){e=ESP_ERR_NO_MEM;goto fail;}
    __atomic_store_n(&started,true,__ATOMIC_RELEASE);return ESP_OK;
fail:
    startup_cleanup(renderer_ready,display_attempted);return e;
}
#endif
