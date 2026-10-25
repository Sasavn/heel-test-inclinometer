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
static SemaphoreHandle_t ui_mutex,transfer_done;
static esp_lcd_panel_handle_t panel;
static esp_lcd_touch_handle_t touch;
static ppa_client_handle_t ppa;
static uint16_t *frame,*rotated;
static portMUX_TYPE stats_lock=portMUX_INITIALIZER_UNLOCKED;
static tab5_native_port_stats_t stats;
static bool started;
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
    if(reset)stats=(tab5_native_port_stats_t){0};
    if(out)*out=stats;
    portEXIT_CRITICAL(&stats_lock);
}
static bool transfer_finished(esp_lcd_panel_handle_t handle,esp_lcd_dpi_panel_event_data_t *event,void *ctx) {
    (void)handle;(void)event;(void)ctx;BaseType_t wake=pdFALSE;
    xSemaphoreGiveFromISR(transfer_done,&wake);return wake==pdTRUE;
}
static uint64_t clock_us(void){return (uint64_t)esp_timer_get_time();}
static void native_task(void *arg) {
    (void)arg;uint32_t last_ms=(uint32_t)(clock_us()/1000);
    uint64_t last_render=0,pending_start=0,pending_submitted=0;
    bool pending=false,timeout_reported=false,was_pressed=false,input_requested=false;
    uint64_t input_since=0;int x=0,y=0;
    for(;;) {
        uint64_t now=clock_us();
        if(pending&&xSemaphoreTake(transfer_done,0)==pdTRUE) {
            uint64_t elapsed=now-pending_start;
            portENTER_CRITICAL(&stats_lock);
            stats.wait_us+=now-pending_submitted;stats.refreshes++;stats.refresh_us+=elapsed;
            if(elapsed>stats.refresh_max_us)stats.refresh_max_us=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
            portEXIT_CRITICAL(&stats_lock);
            pending=false;
        }
        if(pending&&!timeout_reported&&now-pending_submitted>=1000000) {
            portENTER_CRITICAL(&stats_lock);stats.errors++;portEXIT_CRITICAL(&stats_lock);
            timeout_reported=true;
            /* No completion means no buffer reuse. Keep servicing input and
             * state until the late completion arrives; never cancel DMA by
             * freeing its source. No UI mutex is held during this interval. */
        }
        uint16_t tx=0,ty=0,strength=0;uint8_t points=0;
        bool pressed=esp_lcd_touch_read_data(touch)==ESP_OK &&
            esp_lcd_touch_get_coordinates(touch,&tx,&ty,&strength,&points,1)&&points;
        if(pressed)pressed=native_touch_position(tx,ty,&x,&y);
        if(pressed||was_pressed){
            if(!input_requested)input_since=clock_us();
            input_requested=true;
        }
#ifdef ESP_PLATFORM
        /* Preserve release for retry if a full FIFO rejects this sample. */
        if((pressed||was_pressed)&&tab5_native_port_post_touch(x,y,pressed,(uint32_t)(clock_us()/1000)))was_pressed=pressed;
#else
        was_pressed=pressed;
#endif
        xSemaphoreTake(ui_mutex,portMAX_DELAY);
        uint32_t ms=(uint32_t)(clock_us()/1000);
        nui_native_tick(ms-last_ms);last_ms=ms;
#ifdef ESP_PLATFORM
        if(tab5_native_port_input_dispatch(ms)){
            if(!input_requested)input_since=clock_us();
            input_requested=true;
        }
#else
        nui_native_touch(x,y,pressed);
#endif
        uint64_t start=clock_us();size_t dirty=0;
        bool interactive=input_requested;
        uint64_t interval=interactive?INTERACTION_RENDER_US:PERIODIC_RENDER_US;
        uint64_t input_wait=interactive?start-input_since:0;
        if(!pending&&start-last_render>=interval) {
            last_render=start;dirty=nui_native_render();
            input_requested=false;
        }
        uint64_t rendered=clock_us();
        xSemaphoreGive(ui_mutex);
        /* Only this task renders frame. Main updates retained state under
         * ui_mutex, never framebuffer pixels. PPA/LCD waits need no UI lock. */
        if(dirty) {
            ppa_srm_oper_config_t op={
                .in={.buffer=frame,.pic_w=WIDTH,.pic_h=HEIGHT,.block_w=WIDTH,.block_h=HEIGHT,.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
                .out={.buffer=rotated,.buffer_size=FRAME_BYTES,.pic_w=HEIGHT,.pic_h=WIDTH,.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
                .rotation_angle=PPA_SRM_ROTATION_ANGLE_90,.scale_x=1,.scale_y=1,.mode=PPA_TRANS_MODE_BLOCKING};
            esp_err_t e=ppa_do_scale_rotate_mirror(ppa,&op);
            uint64_t rotated_at=clock_us();
            xSemaphoreTake(transfer_done,0); /* Drain BEFORE submit. */
            if(e==ESP_OK)e=esp_lcd_panel_draw_bitmap(panel,0,0,HEIGHT,WIDTH,rotated);
            uint64_t submitted=clock_us();
            portENTER_CRITICAL(&stats_lock);
            stats.renders++;stats.render_us+=rendered-start;
            if(interactive){
                stats.interactive_renders++;
                if(input_wait>stats.input_render_wait_max_us)
                    stats.input_render_wait_max_us=input_wait>UINT32_MAX?UINT32_MAX:(uint32_t)input_wait;
            }
            stats.rotate_us+=rotated_at-rendered;stats.flush_us+=submitted-rotated_at;
            if(e==ESP_OK)stats.flushes++;else stats.errors++;
            portEXIT_CRITICAL(&stats_lock);
            if(e==ESP_OK){pending=true;timeout_reported=false;pending_start=start;pending_submitted=submitted;}
            else {
                nui_area_t full={0,0,WIDTH-1,HEIGHT-1};
                xSemaphoreTake(ui_mutex,portMAX_DELAY);nui_native_invalidate(&full);xSemaphoreGive(ui_mutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
static void startup_cleanup(bool renderer_ready,bool display_attempted) {
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
    if(ui_mutex){vSemaphoreDelete(ui_mutex);ui_mutex=NULL;}
}
esp_err_t tab5_native_port_start(tab5_ui_action_cb_t callback,void *user) {
    if(started)return ESP_ERR_INVALID_STATE;
    tab5_panel_observer_reset();
    esp_err_t e=ESP_ERR_NO_MEM;bool renderer_ready=false,display_attempted=false;
    ui_mutex=xSemaphoreCreateMutex();transfer_done=xSemaphoreCreateBinary();
    if(!ui_mutex||!transfer_done)goto fail;
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
