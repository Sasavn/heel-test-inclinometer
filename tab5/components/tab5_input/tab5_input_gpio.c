#include "tab5_input_gpio.h"
#include "tab5_input.h"
#include "sdkconfig.h"
#include <string.h>
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
static portMUX_TYPE status_mux=portMUX_INITIALIZER_UNLOCKED;
static tab5_gpio_status status={.enabled=true,.config={.a=-1,.b=-1,.press=-1,.record=-1},.raw={-1,-1,-1,-1}};
static bool allowed(int p) { return p==2 || p==3 || p==4 || p==48; }
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time()/1000); }
static void read_raw(int8_t raw[4]) {
    /* Keep the existing control read order. These are also sampled during
     * queue backpressure for diagnostics; only the idle branch decodes them. */
    raw[3]=(int8_t)gpio_get_level(cfg.record);raw[2]=(int8_t)gpio_get_level(cfg.press);
    raw[0]=(int8_t)gpio_get_level(cfg.a);raw[1]=(int8_t)gpio_get_level(cfg.b);
}
static void publish_raw(const int8_t raw[4],uint32_t ms) {
    portENTER_CRITICAL(&status_mux);
    if(status.sampled){
        status.poll_gap_ms=ms-status.sampled_ms;
        if(status.poll_gap_ms>status.poll_gap_max_ms)status.poll_gap_max_ms=status.poll_gap_ms;
        for(unsigned i=0;i<4;i++)if(raw[i]!=status.raw[i])status.raw_changes[i]++;
        if(raw[0]!=status.raw[0]||raw[1]!=status.raw[1])status.ab_changes++;
    }
    memcpy(status.raw,raw,sizeof(status.raw));status.sampled=true;status.sampled_ms=ms;status.samples++;
    portEXIT_CRITICAL(&status_mux);
}
static bool callback_result(unsigned index,uint32_t event_ms,bool accepted) {
    uint32_t ms=now_ms(),age=ms-event_ms;
    portENTER_CRITICAL(&status_mux);
    status.attempts[index]++;
    if(accepted)status.accepted[index]++;else status.rejected[index]++;
    status.event_ms[index]=event_ms;status.attempt_ms[index]=ms;status.event_age_ms[index]=age;
    if(age>status.event_age_max_ms[index])status.event_age_max_ms[index]=age;
    portEXIT_CRITICAL(&status_mux);return accepted;
}
static void poll_task(void *unused) {
    (void)unused;
    tab5_input_switch record,press;
    tab5_input_encoder encoder;
    uint32_t ms=now_ms();
    int8_t raw[4];read_raw(raw);publish_raw(raw,ms);
    tab5_input_switch_init(&record,!raw[3],ms,cfg.debounce_ms);
    tab5_input_switch_init(&press,!raw[2],ms,cfg.debounce_ms);
    tab5_input_encoder_init(&encoder,(uint8_t)((raw[0]<<1)|raw[1]));
    TickType_t delay=pdMS_TO_TICKS(cfg.poll_ms);
    if(!delay) delay=1;
    tab5_input_event r=TAB5_INPUT_NONE,p=TAB5_INPUT_NONE;
    int d=0;
    uint32_t event_ms=ms;
    for(;;) {
        uint32_t sampled_ms=now_ms();read_raw(raw);publish_raw(raw,sampled_ms);
        if(r==TAB5_INPUT_NONE && p==TAB5_INPUT_NONE && !d) {
            event_ms=sampled_ms;
            r=tab5_input_switch_sample(&record,!raw[3],event_ms);
            p=tab5_input_switch_sample(&press,!raw[2],event_ms);
            uint8_t ab=(uint8_t)((raw[0]<<1)|raw[1]);
            bool invalid=(encoder.previous^ab)==3;
            d=tab5_input_encoder_sample(&encoder,ab);
            portENTER_CRITICAL(&status_mux);
            if(invalid)status.invalid_ab++;
            if(d>0)status.detent_positive++;else if(d<0)status.detent_negative++;
            if(p==TAB5_INPUT_ON)status.press_down++;else if(p==TAB5_INPUT_OFF_LOCAL)status.press_up++;
            if(r==TAB5_INPUT_ON)status.record_on++;else if(r==TAB5_INPUT_OFF_LOCAL)status.record_off++;
            status.decoder_partial=encoder.partial;
            portEXIT_CRITICAL(&status_mux);
        }
        if(r!=TAB5_INPUT_NONE) {
            if(!callback_result(2,event_ms,callbacks.record(r==TAB5_INPUT_ON,event_ms))) goto retry;
            r=TAB5_INPUT_NONE;
        }
        if(p!=TAB5_INPUT_NONE) {
            if(!callback_result(1,event_ms,callbacks.button(p==TAB5_INPUT_ON,event_ms))) goto retry;
            p=TAB5_INPUT_NONE;
        }
        if(d) {
            if(!callback_result(0,event_ms,callbacks.detent(d,event_ms))) goto retry;
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
       (c->poll_ms!=1 && c->poll_ms!=5 && c->poll_ms!=10)) return ESP_ERR_INVALID_ARG;
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
    if(err!=ESP_OK){portENTER_CRITICAL(&status_mux);status.start_error=err;portEXIT_CRITICAL(&status_mux);return err;}
    cfg=*c; callbacks=*cb;
    portENTER_CRITICAL(&status_mux);status.config=*c;status.start_error=ESP_OK;portEXIT_CRITICAL(&status_mux);
    if(xTaskCreate(poll_task,"tab5_input",3072,NULL,5,NULL)!=pdPASS) {
        for(unsigned i=0;i<4;i++) gpio_reset_pin(pins[i]);
        portENTER_CRITICAL(&status_mux);status.start_error=ESP_ERR_NO_MEM;portEXIT_CRITICAL(&status_mux);
        return ESP_ERR_NO_MEM;
    }
    started=true;
    portENTER_CRITICAL(&status_mux);status.initialized=true;portEXIT_CRITICAL(&status_mux);
    return ESP_OK;
}
uint32_t tab5_input_gpio_retries(void) { return atomic_load(&retry_count); }
void tab5_input_gpio_snapshot(tab5_gpio_status *out) {
    if(!out)return;
    portENTER_CRITICAL(&status_mux);*out=status;portEXIT_CRITICAL(&status_mux);
}
#else
esp_err_t tab5_input_gpio_start(const tab5_gpio_config *c, const tab5_gpio_callbacks *cb) {
    (void)c; (void)cb; return ESP_ERR_NOT_SUPPORTED;
}
uint32_t tab5_input_gpio_retries(void) { return 0; }
void tab5_input_gpio_snapshot(tab5_gpio_status *out) {
    if(!out)return;
    memset(out,0,sizeof(*out));out->config.a=out->config.b=out->config.press=out->config.record=-1;
    for(unsigned i=0;i<4;i++)out->raw[i]=-1;
    out->start_error=ESP_ERR_NOT_SUPPORTED;
}
#endif
