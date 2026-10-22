#include "tab5_native_port.h"
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
    bool pending=false,timeout_reported=false;int x=0,y=0;
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
        xSemaphoreTake(ui_mutex,portMAX_DELAY);
        uint32_t ms=(uint32_t)(clock_us()/1000);
        nui_native_tick(ms-last_ms);last_ms=ms;
        nui_native_touch(x,y,pressed);
        uint64_t start=clock_us();size_t dirty=0;
        if(!pending&&start-last_render>=100000) {
            last_render=start;dirty=nui_native_render();
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
    if((e=bsp_display_brightness_set(60))!=ESP_OK)goto fail;
    if(xTaskCreatePinnedToCore(native_task,"native_ui",8192,NULL,4,NULL,1)!=pdPASS){e=ESP_ERR_NO_MEM;goto fail;}
    __atomic_store_n(&started,true,__ATOMIC_RELEASE);return ESP_OK;
fail:
    startup_cleanup(renderer_ready,display_attempted);return e;
}
