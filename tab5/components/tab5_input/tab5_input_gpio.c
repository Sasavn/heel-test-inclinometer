#include "tab5_input_gpio.h"
#include "tab5_input.h"
#include "sdkconfig.h"
#if CONFIG_TAB5_INPUT_GPIO_ENABLE
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
static tab5_gpio_config cfg;
static tab5_gpio_callbacks callbacks;
static bool started;
static atomic_uint retry_count;
static bool allowed(int p) { return p==2 || p==3 || p==4 || p==48; }
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time()/1000); }
static unsigned ab(void) {
    return ((unsigned)gpio_get_level(cfg.a)<<1)|(unsigned)gpio_get_level(cfg.b);
}
static void poll_task(void *unused) {
    (void)unused;
    tab5_input_switch record,press;
    tab5_input_encoder encoder;
    uint32_t ms=now_ms();
    tab5_input_switch_init(&record,!gpio_get_level(cfg.record),ms,cfg.debounce_ms);
    tab5_input_switch_init(&press,!gpio_get_level(cfg.press),ms,cfg.debounce_ms);
    tab5_input_encoder_init(&encoder,(uint8_t)ab());
    TickType_t delay=pdMS_TO_TICKS(cfg.poll_ms);
    if(!delay) delay=1;
    tab5_input_event r=TAB5_INPUT_NONE,p=TAB5_INPUT_NONE;
    int d=0;
    uint32_t event_ms=ms;
    for(;;) {
        if(r==TAB5_INPUT_NONE && p==TAB5_INPUT_NONE && !d) {
            event_ms=now_ms();
            r=tab5_input_switch_sample(&record,!gpio_get_level(cfg.record),event_ms);
            p=tab5_input_switch_sample(&press,!gpio_get_level(cfg.press),event_ms);
            d=tab5_input_encoder_sample(&encoder,(uint8_t)ab());
        }
        if(r!=TAB5_INPUT_NONE) {
            if(!callbacks.record(r==TAB5_INPUT_ON,event_ms)) goto retry;
            r=TAB5_INPUT_NONE;
        }
        if(p!=TAB5_INPUT_NONE) {
            if(!callbacks.button(p==TAB5_INPUT_ON,event_ms)) goto retry;
            p=TAB5_INPUT_NONE;
        }
        if(d) {
            if(!callbacks.detent(d,event_ms)) goto retry;
            d=0;
        }
        vTaskDelay(delay);
        continue;
retry:
        atomic_fetch_add(&retry_count,1);
        vTaskDelay(delay);
    }
}
esp_err_t tab5_input_gpio_start(const tab5_gpio_config *c, const tab5_gpio_callbacks *cb) {
    if(!c || !cb || !cb->button || !cb->record || !cb->detent ||
       !c->debounce_ms || c->debounce_ms>1000 ||
       (c->poll_ms!=5 && c->poll_ms!=10)) return ESP_ERR_INVALID_ARG;
    int pins[]={c->a,c->b,c->press,c->record};
    uint64_t mask=0;
    for(unsigned i=0;i<4;i++) {
        if(!allowed(pins[i])) return ESP_ERR_INVALID_ARG;
        if(mask&(1ULL<<pins[i])) return ESP_ERR_INVALID_ARG;
        mask|=1ULL<<pins[i];
    }
    if(started) return ESP_ERR_INVALID_STATE;
    gpio_config_t g={.pin_bit_mask=mask,.mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,
        .intr_type=GPIO_INTR_DISABLE};
    esp_err_t err=gpio_config(&g);
    if(err!=ESP_OK) return err;
    cfg=*c; callbacks=*cb;
    if(xTaskCreate(poll_task,"tab5_input",3072,NULL,5,NULL)!=pdPASS) {
        for(unsigned i=0;i<4;i++) gpio_reset_pin(pins[i]);
        return ESP_ERR_NO_MEM;
    }
    started=true;
    return ESP_OK;
}
uint32_t tab5_input_gpio_retries(void) { return atomic_load(&retry_count); }
#else
esp_err_t tab5_input_gpio_start(const tab5_gpio_config *c, const tab5_gpio_callbacks *cb) {
    (void)c; (void)cb; return ESP_ERR_NOT_SUPPORTED;
}
uint32_t tab5_input_gpio_retries(void) { return 0; }
#endif
