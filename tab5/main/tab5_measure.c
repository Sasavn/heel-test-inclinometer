#include "tab5_measure.h"
#include <math.h>
#include <string.h>
static float clampf(float v, float lo, float hi, float fallback) {
    return !isfinite(v) ? fallback : v < lo ? lo : v > hi ? hi : v;
}
static unsigned clampu(unsigned v, unsigned lo, unsigned hi) { return v < lo ? lo : v > hi ? hi : v; }
void tab5_measure_config_default(tab5_measure_config_t *c) {
    *c = (tab5_measure_config_t){ .frequency_hz=10, .gap_ms=15, .alpha=.15f,
        .roll_window_s=20, .roll_rate_hz=5, .roll_calm_deg=1.5f, .roll_hysteresis_deg=.2f };
}
static tab5_measure_config_t normalized(const tab5_measure_config_t *in) {
    tab5_measure_config_t c;
    tab5_measure_config_default(&c);
    if (in) c=*in;
    c.frequency_hz=clampu(c.frequency_hz,1,50); c.gap_ms=clampu(c.gap_ms,15,100);
    c.alpha=clampf(c.alpha,.01f,.99f,.15f);
    c.roll_window_s=clampu(c.roll_window_s,10,60); c.roll_rate_hz=clampu(c.roll_rate_hz,2,5);
    c.roll_calm_deg=clampf(c.roll_calm_deg,.1f,10,1.5f);
    c.roll_hysteresis_deg=clampf(c.roll_hysteresis_deg,0,2,.2f);
    return c;
}
static void reset_roll(tab5_measure_core_t *c, unsigned i) {
    tab5_measure_filter_t *f=&c->filter[i];
    f->roll_used=false; f->count=f->next=0; f->roll_due_us=0;
    tab5_measure_sensor_t *s=&c->state.sensor[i];
    s->roll_x=s->roll_y=0; s->calm_x=s->calm_y=false; s->roll_fill_s=0;
}
static void stable(tab5_measure_core_t *c) {
    bool any=false, calm=true;
    for (unsigned i=0;i<TAB5_SENSOR_COUNT;i++) {
        const tab5_measure_sensor_t *s=&c->state.sensor[i];
        if(s->valid) { any=true; calm=calm && s->calm_x && s->calm_y; }
    }
    c->state.stable=any && calm;
}
void tab5_measure_core_init(tab5_measure_core_t *c, const tab5_measure_config_t *config, uint64_t now) {
    memset(c,0,sizeof(*c)); c->state.config=normalized(config);
    for(unsigned i=0;i<TAB5_SENSOR_COUNT;i++) c->state.sensor[i].address=(uint8_t)(i+2);
    c->next_cycle_us=now+ (1000000u+c->state.config.frequency_hz-1)/c->state.config.frequency_hz;
}
void tab5_measure_core_configure(tab5_measure_core_t *c, const tab5_measure_config_t *config, uint64_t now) {
    tab5_measure_config_t n=normalized(config), old=c->state.config;
    c->state.config=n;
    if(n.frequency_hz!=old.frequency_hz)
        c->next_cycle_us=now+(1000000u+n.frequency_hz-1)/n.frequency_hz;
    if(n.roll_window_s!=old.roll_window_s || n.roll_rate_hz!=old.roll_rate_hz ||
       n.frequency_hz!=old.frequency_hz || n.alpha!=old.alpha) {
        for(unsigned i=0;i<TAB5_SENSOR_COUNT;i++) reset_roll(c,i);
        stable(c);
    }
}
bool tab5_measure_cycle_due(tab5_measure_core_t *c, uint64_t now) {
    if(now<c->next_cycle_us) return false;
    /* Never catch up in bursts, nor start sooner than the requested period. */
    c->next_cycle_us=now+(1000000u+c->state.config.frequency_hz-1)/c->state.config.frequency_hz;
    c->state.cycles++; return true;
}
float tab5_measure_decode(uint16_t raw) { return (float)((int16_t)raw-10000)/100.f; }
uint16_t tab5_measure_crc16(const uint8_t *p, size_t n) {
    uint16_t crc=0xffff;
    for(size_t j=0;j<n;j++) { crc^=p[j]; for(unsigned k=0;k<8;k++) crc=(crc&1)?(crc>>1)^0xa001:crc>>1; }
    return crc;
}
void tab5_measure_request(uint8_t addr, uint8_t out[8]) {
    const uint8_t r[6]={addr,3,0,1,0,2}; memcpy(out,r,6);
    uint16_t crc=tab5_measure_crc16(out,6); out[6]=(uint8_t)crc; out[7]=(uint8_t)(crc>>8);
}
tab5_measure_error_t tab5_measure_parse(uint8_t addr, const uint8_t *p, size_t n, uint16_t raw[2]) {
    if(!p || n<5) return TAB5_MEASURE_FRAME;
    if(tab5_measure_crc16(p,n-2)!=(uint16_t)(p[n-2]|(p[n-1]<<8))) return TAB5_MEASURE_CRC;
    if(p[0]!=addr) return TAB5_MEASURE_FRAME;
    if(p[1]==0x83 && n==5) return TAB5_MEASURE_EXCEPTION;
    if(n!=9 || p[1]!=3 || p[2]!=4) return TAB5_MEASURE_FRAME;
    raw[0]=(uint16_t)((p[3]<<8)|p[4]); raw[1]=(uint16_t)((p[5]<<8)|p[6]); return TAB5_MEASURE_OK;
}
static float median(float a,float b,float c) { float t; if(a>b){t=a;a=b;b=t;} if(b>c)b=c; return a>b?a:b; }
static int16_t cdeg(float v) { v=clampf(v*100,-32767,32767,0); return (int16_t)(v>=0?v+.5f:v-.5f); }
static void roll(tab5_measure_core_t *c,unsigned i,uint64_t now) {
    tab5_measure_filter_t *f=&c->filter[i]; tab5_measure_sensor_t *s=&c->state.sensor[i];
    const tab5_measure_config_t *cfg=&c->state.config;
    uint64_t step=1000000u/cfg->roll_rate_hz, window=(uint64_t)cfg->roll_window_s*1000000u;
    /* Original tolerance: at most 20 ms early; missing samples are never fabricated. */
    if(f->roll_used && now+20000<f->roll_due_us) return;
    uint64_t next=f->roll_due_us+step;
    if(!f->roll_used || now+20000>=next) next=now+step;
    f->roll_due_us=next; f->roll_used=true;
    f->roll[0][f->next]=cdeg(s->filtered_x); f->roll[1][f->next]=cdeg(s->filtered_y); f->time[f->next]=now;
    f->next=(f->next+1)%TAB5_ROLL_CAPACITY; if(f->count<TAB5_ROLL_CAPACITY)f->count++;
    int16_t lo[2]={32767,32767}, hi[2]={-32767,-32767}; uint64_t oldest=0;
    for(unsigned k=0;k<f->count;k++) {
        unsigned j=(f->next+TAB5_ROLL_CAPACITY-1-k)%TAB5_ROLL_CAPACITY;
        uint64_t age=now-f->time[j]; if(age>window)break; oldest=age;
        for(unsigned a=0;a<2;a++){int16_t v=f->roll[a][j];if(v<lo[a])lo[a]=v;if(v>hi[a])hi[a]=v;}
    }
    s->roll_x=(hi[0]-lo[0])*.01f; s->roll_y=(hi[1]-lo[1])*.01f;
    bool full=oldest+step>=window;
    s->roll_fill_s=(uint8_t)((oldest+step)/1000000u);
    if(s->roll_fill_s>cfg->roll_window_s)s->roll_fill_s=cfg->roll_window_s;
    s->calm_x=full && (s->calm_x?s->roll_x<=cfg->roll_calm_deg+cfg->roll_hysteresis_deg:s->roll_x<cfg->roll_calm_deg);
    s->calm_y=full && (s->calm_y?s->roll_y<=cfg->roll_calm_deg+cfg->roll_hysteresis_deg:s->roll_y<cfg->roll_calm_deg);
}
bool tab5_measure_core_sample(tab5_measure_core_t *c,unsigned i,const uint16_t raw[2],uint64_t now) {
    if(i>=TAB5_SENSOR_COUNT || !raw)return false;
    tab5_measure_sensor_t *s=&c->state.sensor[i]; tab5_measure_filter_t *f=&c->filter[i];
    float v[2]={tab5_measure_decode(raw[0]),tab5_measure_decode(raw[1])}, out[2];
    float prev[2]={s->filtered_x,s->filtered_y};
    for(unsigned a=0;a<2;a++) {
        float *h=f->history[a];
        if(!f->initialized) {h[0]=h[1]=h[2]=v[a];out[a]=v[a];}
        else {h[2]=h[1];h[1]=h[0];h[0]=v[a];out[a]=c->state.config.alpha*median(h[0],h[1],h[2])+(1-c->state.config.alpha)*prev[a];}
    }
    f->initialized=true; s->raw_x=raw[0];s->raw_y=raw[1];s->decoded_x=v[0];s->decoded_y=v[1];
    s->filtered_x=out[0];s->filtered_y=out[1];s->x=out[0]-s->zero_x;s->y=out[1]-s->zero_y;
    s->timestamp_us=now;s->valid=true;s->fail_streak=0;s->good++;s->last_error=TAB5_MEASURE_OK;
    roll(c,i,now);stable(c);return true;
}
void tab5_measure_core_error(tab5_measure_core_t *c,unsigned i,tab5_measure_error_t error) {
    if(i>=TAB5_SENSOR_COUNT || error==TAB5_MEASURE_OK)return;
    tab5_measure_sensor_t *s=&c->state.sensor[i];s->errors++;s->last_error=error;
    if(s->fail_streak<255)s->fail_streak++;
    if(s->fail_streak>=3) {
        s->valid=false;c->filter[i].initialized=false;reset_roll(c,i);stable(c);
    }
}
void tab5_measure_core_zero(tab5_measure_core_t *c,bool reset) {
    for(unsigned i=0;i<TAB5_SENSOR_COUNT;i++) {
        tab5_measure_sensor_t *s=&c->state.sensor[i];
        if(!reset && !s->valid)continue;
        s->zero_x=reset?0:s->filtered_x;s->zero_y=reset?0:s->filtered_y;
        s->x=s->filtered_x-s->zero_x;s->y=s->filtered_y-s->zero_y;
    }
}
#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
static tab5_measure_core_t device;
static SemaphoreHandle_t mutex;
static TaskHandle_t worker;
static tab5_measure_sample_cb_t sample_cb;
static void *sample_user;
#define PORT UART_NUM_1
/* Ceiling ticks and monotonic deadline ensure a low FreeRTOS tick rate cannot
 * shorten the 15 ms gap or the 30 ms RX deadline. */
static TickType_t ticks_ceil(uint32_t ms) { return (ms*configTICK_RATE_HZ+999)/1000; }
static void wait_until(uint64_t deadline) {
    while((uint64_t)esp_timer_get_time()<deadline) {
        uint64_t current=(uint64_t)esp_timer_get_time();
        if(current>=deadline)break;
        uint64_t left=deadline-current;
        if(left>0 && left<1000) { taskYIELD(); } else vTaskDelay(1);
    }
}
static tab5_measure_error_t transact(unsigned i,uint16_t raw[2],uint64_t *end) {
    uint16_t gap;
    xSemaphoreTake(mutex,portMAX_DELAY);gap=device.state.config.gap_ms;xSemaphoreGive(mutex);
    wait_until(*end+(uint64_t)gap*1000);
    uint8_t req[8],reply[9];size_t n=0;tab5_measure_request((uint8_t)(i+2),req);
    uart_flush_input(PORT);
    gpio_set_level(GPIO_NUM_34,1);
    int sent=uart_write_bytes(PORT,req,sizeof(req));
    esp_err_t tx=uart_wait_tx_done(PORT,ticks_ceil(30));
    gpio_set_level(GPIO_NUM_34,0);
    tab5_measure_error_t result=TAB5_MEASURE_IO;
    if(sent==8 && tx==ESP_OK) {
        uint64_t deadline=(uint64_t)esp_timer_get_time()+30000;
        size_t want=9;
        while((uint64_t)esp_timer_get_time()<deadline && n<want) {
            int got=uart_read_bytes(PORT,reply+n,1,0);
            if(got<0)break;
            if(got==0){vTaskDelay(1);continue;}
            n++;
            if(n==2 && reply[1]==0x83)want=5;
        }
        result=n?tab5_measure_parse((uint8_t)(i+2),reply,n,raw):TAB5_MEASURE_TIMEOUT;
    }
    *end=(uint64_t)esp_timer_get_time();return result;
}
static void measure_task(void *unused) {
    (void)unused;uint64_t end=(uint64_t)esp_timer_get_time();
    for(;;) {
        xSemaphoreTake(mutex,portMAX_DELAY);
        bool due=tab5_measure_cycle_due(&device,(uint64_t)esp_timer_get_time());
        xSemaphoreGive(mutex);
        if(!due){vTaskDelay(1);continue;}
        for(unsigned i=0;i<TAB5_SENSOR_COUNT;i++) {
            uint16_t raw[2];tab5_measure_error_t e=transact(i,raw,&end);
            tab5_measure_sensor_t sample;
            xSemaphoreTake(mutex,portMAX_DELAY);
            if(e==TAB5_MEASURE_OK){tab5_measure_core_sample(&device,i,raw,end);sample=device.state.sensor[i];}
            else tab5_measure_core_error(&device,i,e);
            xSemaphoreGive(mutex);
            if(e==TAB5_MEASURE_OK && sample_cb)sample_cb(i,&sample,sample_user);
        }
    }
}
esp_err_t tab5_measure_start(const tab5_measure_config_t *config,tab5_measure_sample_cb_t cb,void *user) {
    if(mutex || worker)return ESP_ERR_INVALID_STATE;
    mutex=xSemaphoreCreateMutex();if(!mutex)return ESP_ERR_NO_MEM;
    uart_config_t u={.baud_rate=115200,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    gpio_config_t g={.pin_bit_mask=1ULL<<34,.mode=GPIO_MODE_OUTPUT};
    bool installed=false;
    esp_err_t e=gpio_config(&g);
    if(e==ESP_OK)e=gpio_set_level(GPIO_NUM_34,0);
    if(e==ESP_OK)e=uart_param_config(PORT,&u);
    if(e==ESP_OK)e=uart_set_pin(PORT,20,21,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if(e==ESP_OK){e=uart_driver_install(PORT,256,0,0,NULL,0);installed=e==ESP_OK;}
    if(e==ESP_OK){
        tab5_measure_core_init(&device,config,(uint64_t)esp_timer_get_time());sample_cb=cb;sample_user=user;
        if(xTaskCreate(measure_task,"tab5_measure",4096,NULL,5,&worker)!=pdPASS)e=ESP_ERR_NO_MEM;
    }
    if(e!=ESP_OK){if(installed)uart_driver_delete(PORT);vSemaphoreDelete(mutex);mutex=NULL;worker=NULL;}
    return e;
}
void tab5_measure_snapshot(tab5_measure_snapshot_t *out) {
    if(!out)return;
    if(!mutex){memset(out,0,sizeof(*out));return;}
    xSemaphoreTake(mutex,portMAX_DELAY);*out=device.state;xSemaphoreGive(mutex);
}
void tab5_measure_configure(const tab5_measure_config_t *config) {
    if(!mutex)return;
    xSemaphoreTake(mutex,portMAX_DELAY);tab5_measure_core_configure(&device,config,(uint64_t)esp_timer_get_time());xSemaphoreGive(mutex);
}
void tab5_measure_zero(bool reset) {
    if(!mutex)return;
    xSemaphoreTake(mutex,portMAX_DELAY);tab5_measure_core_zero(&device,reset);xSemaphoreGive(mutex);
}
#endif
