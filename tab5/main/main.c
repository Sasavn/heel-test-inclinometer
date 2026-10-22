#include "tab5_measure.h"
#include "tab5_ui.h"
#include "tab5_board.h"
#include "tab5_services.h"
#include "tab5_panel.h"
#include "tab5_time.h"
#include "bsp/esp-bsp.h"
#include "driver/usb_serial_jtag.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FW_VERSION (esp_app_get_description()->version)
typedef struct { unsigned index; tab5_measure_sensor_t value; } sample_event_t;
typedef struct { char line[3072]; } tx_line_t;
static QueueHandle_t samples, storage_samples, actions, usb_tx;
static SemaphoreHandle_t app_mutex, route_mutex;
static bool route_recording;
static uint64_t route_start_us;
static float route_battery=NAN;
static tab5_network_config_t network;
static bool recording, samples_on, light_theme;
static unsigned stream_ms, measurement_number;
static uint32_t sample_drops, usb_drops, storage_drops, route_drops;
static uint64_t record_start_us;
static char record_name[40];
static uint32_t record_duration_s;
static uint32_t ui_result_serial;
static tab5_ui_action_kind_t ui_result_kind;
static bool ui_result_success;
static float battery = NAN;
static char notice[160]="Прибор готов. Перед записью установите часы";

static void boot_failed(void) {
    esp_ota_img_states_t state;
    if(esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY)
        esp_ota_mark_app_invalid_rollback_and_reboot();
}

static void usb_reply(void *ctx,const char *line) {
    (void)ctx;tx_line_t item;
    if(strlen(line)>=sizeof(item.line)){__atomic_add_fetch(&usb_drops,1,__ATOMIC_RELAXED);return;}
    strcpy(item.line,line);
    if(xQueueSend(usb_tx,&item,0)!=pdTRUE)__atomic_add_fetch(&usb_drops,1,__ATOMIC_RELAXED);
}
static void sample_callback(unsigned i,const tab5_measure_sensor_t *s,void *unused) {
    (void)unused;sample_event_t e={.index=i,.value=*s};
    if(xQueueSend(samples,&e,0)!=pdTRUE)__atomic_add_fetch(&sample_drops,1,__ATOMIC_RELAXED);
    if(xQueueSend(storage_samples,&e,0)!=pdTRUE)__atomic_add_fetch(&route_drops,1,__ATOMIC_RELAXED);
}
static void ui_callback(const tab5_ui_action_t *a,void *unused) {
    (void)unused;xQueueSend(actions,a,0);
}
static void broadcast(const char *line) { usb_reply(NULL,line);tab5_services_publish(line); }
static bool persist(void) {
    nvs_handle_t n;
    if(nvs_open("tab5",NVS_READWRITE,&n)!=ESP_OK){snprintf(notice,sizeof(notice),"Не удалось открыть настройки");return false;}
    tab5_measure_snapshot_t s;tab5_measure_snapshot(&s);
    esp_err_t e=nvs_set_blob(n,"measure",&s.config,sizeof(s.config));
    if(e==ESP_OK)e=nvs_set_blob(n,"network",&network,sizeof(network));
    if(e==ESP_OK)e=nvs_set_u8(n,"theme",light_theme);
    if(e==ESP_OK)e=nvs_set_u32(n,"session",measurement_number);
    if(e==ESP_OK)e=nvs_commit(n);
    nvs_close(n);if(e!=ESP_OK)snprintf(notice,sizeof(notice),"Настройки не сохранены: %s",esp_err_to_name(e));
    return e==ESP_OK;
}
static void load_config(tab5_measure_config_t *cfg) {
    tab5_measure_config_default(cfg);
    uint8_t mac[6]={0};esp_efuse_mac_get_default(mac);
    network=(tab5_network_config_t){.ap_enabled=true,.tcp_port=5000};
    snprintf(network.ap_ssid,sizeof(network.ap_ssid),"Krenomer-%02X%02X%02X",mac[3],mac[4],mac[5]);
    static const char alphabet[]="23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz";
    uint8_t random[20];esp_fill_random(random,sizeof(random));
    for(unsigned i=0;i<20;i++)network.ap_password[i]=alphabet[random[i]%(sizeof(alphabet)-1)];
    strcpy(network.auth_token,network.ap_password);
    nvs_handle_t n;
    if(nvs_open("tab5",NVS_READONLY,&n)!=ESP_OK)return;
    size_t len=sizeof(*cfg);tab5_measure_config_t read;
    if(nvs_get_blob(n,"measure",&read,&len)==ESP_OK && len==sizeof(read))*cfg=read;
    tab5_network_config_t net;len=sizeof(net);
    if(nvs_get_blob(n,"network",&net,&len)==ESP_OK && len==sizeof(net) &&
       memchr(net.ap_ssid,0,sizeof(net.ap_ssid)) && memchr(net.ap_password,0,sizeof(net.ap_password)) &&
       memchr(net.sta_ssid,0,sizeof(net.sta_ssid)) && memchr(net.sta_password,0,sizeof(net.sta_password)) &&
       memchr(net.auth_token,0,sizeof(net.auth_token)) && strlen(net.ap_password)>=8 && strlen(net.auth_token)>=8)network=net;
    uint8_t theme=0;nvs_get_u8(n,"theme",&theme);light_theme=theme==1;
    uint32_t session=0;nvs_get_u32(n,"session",&session);measurement_number=session;nvs_close(n);
}
static esp_err_t set_recording(bool on) {
    if(on==recording)return ESP_OK;
    if(!on){xSemaphoreTake(route_mutex,portMAX_DELAY);route_recording=false;xSemaphoreGive(route_mutex);
        tab5_services_record_stop();record_duration_s=(uint32_t)((esp_timer_get_time()-record_start_us)/1000000);
        recording=false;snprintf(notice,sizeof(notice),"Запись остановлена");return ESP_OK;}
    if(!tab5_board_time_valid()){snprintf(notice,sizeof(notice),"Перед записью установите дату и время");return ESP_ERR_INVALID_STATE;}
    measurement_number++;persist();
    time_t now=time(NULL);struct tm t;localtime_r(&now,&t);char session[64];
    snprintf(session,sizeof(session),"%04d-%02d-%02d_M%03u",t.tm_year+1900,t.tm_mon+1,t.tm_mday,measurement_number);
    esp_err_t e=tab5_services_record_start(session);
    if(e==ESP_OK){recording=true;record_start_us=esp_timer_get_time();
        snprintf(record_name,sizeof(record_name),"%.39s",session);record_duration_s=0;
        xSemaphoreTake(route_mutex,portMAX_DELAY);route_start_us=record_start_us;route_recording=true;xSemaphoreGive(route_mutex);
        snprintf(notice,sizeof(notice),"Запись %s",session);}
    else snprintf(notice,sizeof(notice),"Запись недоступна: %s",esp_err_to_name(e));
    return e;
}
static void add_num(cJSON *j,const char *key,double v) {
    if(isfinite(v))cJSON_AddNumberToObject(j,key,v);else cJSON_AddNullToObject(j,key);
}
static void status(tab5_service_reply_fn reply,void *ctx) {
    tab5_measure_snapshot_t s;tab5_measure_snapshot(&s);
    tab5_medium_status_t sd,usb;tab5_services_status(TAB5_SD,&sd);tab5_services_status(TAB5_USB,&usb);
    tab5_network_snapshot_t net;tab5_services_network_snapshot(&net);
    esp_chip_info_t chip;esp_chip_info(&chip);
    cJSON *j=cJSON_CreateObject();if(!j){reply(ctx,"ERR memory");return;}
    cJSON_AddNumberToObject(j,"v",2);cJSON_AddStringToObject(j,"fw",FW_VERSION);
    cJSON_AddStringToObject(j,"board","tab5");cJSON_AddStringToObject(j,"platform","esp32p4");
    cJSON_AddNumberToObject(j,"proto",2);cJSON_AddNumberToObject(j,"chip_revision",chip.revision);
    cJSON_AddNumberToObject(j,"ms",esp_timer_get_time()/1000);cJSON_AddNumberToObject(j,"up",esp_timer_get_time()/1000000);
    char date[32]="unsynchronized";time_t now=time(NULL);struct tm t;localtime_r(&now,&t);
    if(tab5_board_time_valid())strftime(date,sizeof(date),"%Y-%m-%d %H:%M:%S",&t);
    cJSON_AddStringToObject(j,"time",date);cJSON_AddBoolToObject(j,"rtc",tab5_board_rtc_valid());
    cJSON_AddNumberToObject(j,"freq",s.config.frequency_hz);cJSON_AddNumberToObject(j,"alpha",s.config.alpha);
    cJSON_AddNumberToObject(j,"gap",s.config.gap_ms);cJSON_AddStringToObject(j,"theme",light_theme?"light":"dark");
    cJSON_AddNumberToObject(j,"rollwin",s.config.roll_window_s);cJSON_AddNumberToObject(j,"rollhz",s.config.roll_rate_hz);
    cJSON_AddNumberToObject(j,"rollcalm",s.config.roll_calm_deg);cJSON_AddNumberToObject(j,"rollhyst",s.config.roll_hysteresis_deg);
    cJSON_AddNullToObject(j,"cpu");cJSON_AddNullToObject(j,"rate");
    cJSON *a=cJSON_AddArrayToObject(j,"sens");
    for(unsigned i=0;i<2;i++){
        const tab5_measure_sensor_t *p=&s.sensor[i];cJSON *d=cJSON_CreateObject();cJSON_AddItemToArray(a,d);
        cJSON_AddNumberToObject(d,"addr",p->address);cJSON_AddStringToObject(d,"st",p->valid?"OK":p->good?"LOST":"ABSENT");
        add_num(d,"x",p->valid?p->x:NAN);add_num(d,"y",p->valid?p->y:NAN);
        add_num(d,"ox",p->zero_x);add_num(d,"oy",p->zero_y);add_num(d,"roll_x",p->roll_x);add_num(d,"roll_y",p->roll_y);
        cJSON_AddBoolToObject(d,"calm_x",p->calm_x);cJSON_AddBoolToObject(d,"calm_y",p->calm_y);
        cJSON_AddNumberToObject(d,"fill",p->roll_fill_s);cJSON_AddNumberToObject(d,"ok",p->good);cJSON_AddNumberToObject(d,"err",p->errors);
        if(p->good)cJSON_AddNumberToObject(d,"age",(esp_timer_get_time()-p->timestamp_us)/1000);else cJSON_AddNullToObject(d,"age");
    }
    cJSON_AddBoolToObject(j,"stable",s.stable);cJSON_AddStringToObject(j,"sd",sd.failed?"ERR":sd.recording?"REC":sd.mounted?"READY":"NOCARD");
    cJSON_AddNumberToObject(j,"sd_err",sd.error);cJSON_AddNumberToObject(j,"rows",sd.recording?sd.written:usb.written);
    cJSON_AddNumberToObject(j,"rec_s",recording?(esp_timer_get_time()-record_start_us)/1000000:0);
    cJSON_AddNumberToObject(j,"file",measurement_number);cJSON_AddNumberToObject(j,"rec_mask",(sd.recording||usb.recording)?3:0);cJSON_AddBoolToObject(j,"rec_sw",recording);
    add_num(j,"bat_v",battery);cJSON_AddBoolToObject(j,"bat_present",isfinite(battery)&&battery>5);cJSON_AddNullToObject(j,"bat_pct");
    cJSON_AddStringToObject(j,"svc","idle");cJSON_AddStringToObject(j,"xfer","idle");
    cJSON *u=cJSON_AddObjectToObject(j,"usb_storage");cJSON_AddBoolToObject(u,"mounted",usb.mounted);cJSON_AddBoolToObject(u,"recording",usb.recording);
    cJSON_AddBoolToObject(u,"failed",usb.failed);cJSON_AddNumberToObject(u,"written",usb.written);cJSON_AddNumberToObject(u,"dropped",usb.dropped);
    cJSON *n=cJSON_AddObjectToObject(j,"network");cJSON_AddBoolToObject(n,"ap",net.ap_started);cJSON_AddBoolToObject(n,"sta",net.sta_connected);
    cJSON_AddStringToObject(n,"ap_ip",net.ap_ip);cJSON_AddStringToObject(n,"sta_ip",net.sta_ip);
    cJSON_AddNumberToObject(j,"sample_drops",__atomic_load_n(&sample_drops,__ATOMIC_RELAXED));
    cJSON_AddNumberToObject(j,"usb_drops",__atomic_load_n(&usb_drops,__ATOMIC_RELAXED));
    cJSON_AddNumberToObject(j,"storage_drops",__atomic_load_n(&storage_drops,__ATOMIC_RELAXED));
    cJSON_AddNumberToObject(j,"routing_queue_drops",__atomic_load_n(&route_drops,__ATOMIC_RELAXED));
    char *out=cJSON_PrintUnformatted(j);if(out){reply(ctx,out);cJSON_free(out);reply(ctx,"OK");}else reply(ctx,"ERR memory");cJSON_Delete(j);
}
static void command(void *unused,const char *line,tab5_service_reply_fn reply,void *ctx) {
    (void)unused;xSemaphoreTake(app_mutex,portMAX_DELAY);
    tab5_measure_snapshot_t s;tab5_measure_snapshot(&s);
    if(strcmp(line,"ver")==0){char version[160];snprintf(version,sizeof(version),"BWM427 inclinometer firmware v%s, board=tab5 chip=esp32p4 proto=2",FW_VERSION);reply(ctx,version);reply(ctx,"OK");}
    else if(strcmp(line,"status")==0 || strcmp(line,"diag")==0)status(reply,ctx);
    else if(strcmp(line,"zero")==0 || strcmp(line,"zero reset")==0){tab5_measure_zero(strcmp(line,"zero reset")==0);reply(ctx,"OK zero");}
    else if(strcmp(line,"rec on")==0 || strcmp(line,"rec off")==0){reply(ctx,set_recording(strcmp(line,"rec on")==0)==ESP_OK?"OK recording":"ERR recording unavailable");}
    else if(strcmp(line,"samples on")==0 || strcmp(line,"samples off")==0){samples_on=strcmp(line,"samples on")==0;reply(ctx,samples_on?"OK samples on, dropped 0":"OK samples off, dropped 0");}
    else if(strncmp(line,"stream ",7)==0){char *end;unsigned long ms=strtoul(line+7,&end,10);if(*end || (ms && (ms<50 || ms>5000)))reply(ctx,"ERR stream range 0 or 50..5000");else{stream_ms=ms;reply(ctx,"# S,t_ms,s2,x2,y2,s3,x3,y3,bat_v");reply(ctx,"OK stream");}}
    else if(strncmp(line,"set time ",9)==0){int y,mo,d,h,mi,se;char extra;struct tm t={0};
        if(sscanf(line+9,"%d-%d-%d %d:%d:%d%c",&y,&mo,&d,&h,&mi,&se,&extra)!=6 || y<2000 || y>2099 || mo<1 || mo>12 || d<1 || d>31 || h<0 || h>23 || mi<0 || mi>59 || se<0 || se>59)reply(ctx,"ERR time");
        else{t.tm_year=y-1900;t.tm_mon=mo-1;t.tm_mday=d;t.tm_hour=h;t.tm_min=mi;t.tm_sec=se;reply(ctx,tab5_board_set_time(&t)?"OK time":"ERR date");}}
    else if(strcmp(line,"set theme light")==0 || strcmp(line,"set theme dark")==0){light_theme=strstr(line,"light")!=NULL;persist();reply(ctx,"OK theme");}
    else if(strncmp(line,"set ",4)==0){char key[20],tail;double value;
        if(sscanf(line+4,"%19s %lf %c",key,&value,&tail)!=2 || !isfinite(value))reply(ctx,"ERR set syntax");
        else{bool ok=true;tab5_measure_config_t c=s.config;
            if(strcmp(key,"freq")==0 && value>=1 && value<=50 && value==floor(value))c.frequency_hz=(uint16_t)value;
            else if(strcmp(key,"alpha")==0 && value>=.01 && value<=.99)c.alpha=value;
            else if(strcmp(key,"gap")==0 && value>=15 && value<=100 && value==floor(value))c.gap_ms=value;
            else if(strcmp(key,"rollwin")==0 && value>=10 && value<=60 && value==floor(value))c.roll_window_s=value;
            else if(strcmp(key,"rollhz")==0 && value>=2 && value<=5 && value==floor(value))c.roll_rate_hz=value;
            else if(strcmp(key,"rollcalm")==0 && value>=.1 && value<=10)c.roll_calm_deg=value;
            else if(strcmp(key,"rollhyst")==0 && value>=0 && value<=2)c.roll_hysteresis_deg=value;
            else ok=false;
            if(ok){tab5_measure_configure(&c);persist();reply(ctx,"OK set");}else reply(ctx,"ERR setting/range");}}
    else if(strcmp(line,"help")==0){reply(ctx,"ver status stream N samples on|off zero [reset] rec on|off set time YYYY-MM-DD HH:MM:SS set freq|alpha|gap|rollwin|rollhz|rollcalm|rollhyst N");reply(ctx,"OTA: ota info/begin/chunk/end/abort; network configured on touchscreen");reply(ctx,"OK");}
    else reply(ctx,"ERR unsupported command on esp32");
    xSemaphoreGive(app_mutex);
}
static void usb_task(void *unused) {
    (void)unused;char line[TAB5_SERVICE_LINE_MAX];size_t used=0;bool overflow=false,was=false;
    for(;;){bool connected=usb_serial_jtag_is_connected();
        if(was && !connected){tab5_services_disconnect(1);used=0;overflow=false;}
        was=connected;char in[128];int n=usb_serial_jtag_read_bytes(in,sizeof(in),pdMS_TO_TICKS(10));
        for(int i=0;i<n;i++){char ch=in[i];if(ch=='\r')continue;
            if(ch=='\n'){if(overflow)usb_reply(NULL,"ERR line too long");else if(used){line[used]=0;
                if(!tab5_services_line(1,line,true,usb_reply,NULL))command(NULL,line,usb_reply,NULL);}
                used=0;overflow=false;}
            else if(!overflow){if(used+1<sizeof(line))line[used++]=ch;else overflow=true;}}
        if(connected && uxQueueMessagesWaiting(usb_tx)<8)tab5_services_poll(1,usb_reply,NULL);
        tx_line_t tx;
        while(xQueueReceive(usb_tx,&tx,0)==pdTRUE){if(!connected)continue;
            size_t size=strlen(tx.line),at=0;while(at<size){int wrote=usb_serial_jtag_write_bytes(tx.line+at,size-at,pdMS_TO_TICKS(20));if(wrote<=0){__atomic_add_fetch(&usb_drops,1,__ATOMIC_RELAXED);tab5_services_disconnect(1);break;}at+=wrote;}
            usb_serial_jtag_write_bytes("\n",1,pdMS_TO_TICKS(20));}
        if(connected && !uxQueueMessagesWaiting(usb_tx) && usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(10))==ESP_OK)
            tab5_services_response_flushed(1);
    }
}
static tab5_service_sample_t make_row(const sample_event_t *e,float voltage) {
    time_t now=time(NULL);struct tm t;localtime_r(&now,&t);
    const tab5_measure_sensor_t *p=&e->value;tab5_service_sample_t row={.sensor=e->index+2,
        .raw_x=p->decoded_x,.raw_y=p->decoded_y,.offset_x=p->zero_x,.offset_y=p->zero_y,
        .calc_x=p->decoded_x-p->zero_x,.calc_y=p->decoded_y-p->zero_y,.battery=isfinite(voltage)?voltage:0,.ms=p->timestamp_us/1000};
    strftime(row.date,sizeof(row.date),"%d.%m.%Y",&t);strftime(row.time,sizeof(row.time),"%H:%M:%S",&t);
    return row;
}
/* Separate from UI/commands: a slow Wi-Fi reconfiguration, NVS save or a
 * disconnected PC cannot hold up delivery to the independent media writers. */
static void storage_router_task(void *unused) {
    (void)unused;sample_event_t e;
    for(;;){if(xQueueReceive(storage_samples,&e,portMAX_DELAY)!=pdTRUE)continue;
        xSemaphoreTake(route_mutex,portMAX_DELAY);
        bool active=route_recording;uint64_t start=route_start_us;float voltage=route_battery;
        xSemaphoreGive(route_mutex);
        if(active && e.value.timestamp_us>=start){tab5_service_sample_t row=make_row(&e,voltage);
            if(tab5_services_submit(&row)!=ESP_OK)__atomic_add_fetch(&storage_drops,1,__ATOMIC_RELAXED);
        }
    }
}
static void consume_sample(const sample_event_t *e) {
    const tab5_measure_sensor_t *p=&e->value;
    tab5_service_sample_t row=make_row(e,battery);
    if(samples_on){char line[200];snprintf(line,sizeof(line),"R,%u,%lu,%lu,%ld,%ld,%ld,%ld,%ld",row.sensor,(unsigned long)p->good,(unsigned long)row.ms,
        (long)tab5_services_scaled(p->decoded_x,2),(long)tab5_services_scaled(p->decoded_y,2),
        (long)tab5_services_scaled(p->zero_x,3),(long)tab5_services_scaled(p->zero_y,3),
        (long)tab5_services_scaled(battery,1));broadcast(line);}
}
void app_main(void) {
    if(nvs_flash_init()!=ESP_OK){boot_failed();return;}
    app_mutex=xSemaphoreCreateMutex();route_mutex=xSemaphoreCreateMutex();
    samples=xQueueCreate(64,sizeof(sample_event_t));storage_samples=xQueueCreate(64,sizeof(sample_event_t));
    actions=xQueueCreate(8,sizeof(tab5_ui_action_t));usb_tx=xQueueCreate(16,sizeof(tx_line_t));
    if(!app_mutex || !route_mutex || !samples || !storage_samples || !actions || !usb_tx){boot_failed();return;}
    tab5_measure_config_t cfg;load_config(&cfg);
    if(bsp_i2c_init()!=ESP_OK){boot_failed();return;}
    tab5_board_init();
    usb_serial_jtag_driver_config_t usb={.tx_buffer_size=4096,.rx_buffer_size=4096};
    if(usb_serial_jtag_driver_install(&usb)!=ESP_OK){boot_failed();return;}
    if(tab5_measure_start(&cfg,sample_callback,NULL)!=ESP_OK){boot_failed();return;}
    tab5_services_config_t services={.command=command,.network=network};
    esp_err_t services_result=tab5_services_init(&services);
    if(xTaskCreate(storage_router_task,"sample_storage",6144,NULL,4,NULL)!=pdPASS){boot_failed();return;}
    if(xTaskCreate(usb_task,"protocol_usb",16384,NULL,4,NULL)!=pdPASS){boot_failed();return;}
    lv_display_t *display=bsp_display_start();if(!display || !tab5_panel_ready()){boot_failed();return;}
    if(bsp_display_lock(2000)){bsp_display_rotate(display,LV_DISPLAY_ROTATION_90);tab5_ui_init(ui_callback,NULL);bsp_display_unlock();}else {boot_failed();return;}
    bsp_display_brightness_set(60);persist();
    if(services_result!=ESP_OK){boot_failed();return;}
    bool trial_confirmed=false;
    uint64_t last_ui=0,last_battery=0,last_stream=0;
    for(;;){xSemaphoreTake(app_mutex,portMAX_DELAY);
        sample_event_t sample;while(xQueueReceive(samples,&sample,0)==pdTRUE)consume_sample(&sample);
        tab5_ui_action_t a;
        while(xQueueReceive(actions,&a,0)==pdTRUE){
            if(a.kind==TAB5_UI_START || a.kind==TAB5_UI_STOP){
                ui_result_success=set_recording(a.kind==TAB5_UI_START)==ESP_OK;
                ui_result_kind=a.kind;ui_result_serial++;}
            else if(a.kind==TAB5_UI_ZERO || a.kind==TAB5_UI_ZERO_RESET)tab5_measure_zero(a.kind==TAB5_UI_ZERO_RESET);
            else if(a.kind==TAB5_UI_SETTINGS){if(a.config.gap_ms<15)a.config.gap_ms=15;tab5_measure_configure(&a.config);light_theme=a.light_theme;
                ui_result_success=persist();ui_result_kind=a.kind;ui_result_serial++;
                if(ui_result_success)snprintf(notice,sizeof(notice),"Настройки сохранены");}
            else if(a.kind==TAB5_UI_TIME){
                const tab5_ui_datetime_t *d=&a.datetime;
                ui_result_success=false;ui_result_kind=a.kind;ui_result_serial++;
                if(!tab5_time_fields_valid(d->year,d->month,d->day,d->hour,d->minute,d->second))
                    snprintf(notice,sizeof(notice),"Недопустимая дата или время");
                else {struct tm t={.tm_year=d->year-1900,.tm_mon=d->month-1,.tm_mday=d->day,
                                  .tm_hour=d->hour,.tm_min=d->minute,.tm_sec=d->second};
                    bool ok=tab5_board_set_time(&t);ui_result_success=ok;
                    snprintf(notice,sizeof(notice),"%s",!ok?"Часы не установлены":tab5_board_rtc_valid()?"Дата и время установлены":"Часы установлены; RTC недоступны");}}
            else if(a.kind==TAB5_UI_NETWORK){tab5_network_config_t next=network;next.ap_enabled=a.network.ap_enabled;next.sta_enabled=a.network.sta_enabled;
                memcpy(next.ap_ssid,a.network.ap_ssid,sizeof(next.ap_ssid));memcpy(next.ap_password,a.network.ap_password,sizeof(next.ap_password));
                memcpy(next.sta_ssid,a.network.sta_ssid,sizeof(next.sta_ssid));memcpy(next.sta_password,a.network.sta_password,sizeof(next.sta_password));strcpy(next.auth_token,next.ap_password);
                ui_result_success=false;ui_result_kind=a.kind;ui_result_serial++;
                if(tab5_services_network_configure(&next)==ESP_OK){network=next;ui_result_success=persist();
                    if(ui_result_success)snprintf(notice,sizeof(notice),"Настройки сети применены");}else snprintf(notice,sizeof(notice),"Настройки сети не применены");}}
        uint64_t now=esp_timer_get_time();if(now-last_battery>=1000000){battery=tab5_board_battery();last_battery=now;
            xSemaphoreTake(route_mutex,portMAX_DELAY);route_battery=battery;xSemaphoreGive(route_mutex);}
        tab5_ui_state_t ui={.recording=recording,.light_theme=light_theme};tab5_measure_snapshot(&ui.measurement);
        ui.action_result_known=ui_result_serial!=0;ui.action_result_serial=ui_result_serial;
        ui.action_result_kind=ui_result_kind;ui.action_result_success=ui_result_success;
        ui.now_known=true;ui.now_us=now;ui.rec_known=true;ui.rec_started_us=record_start_us;
        snprintf(ui.rec_file,sizeof(ui.rec_file),"%s",record_name);
        ui.rec_duration_known=record_name[0]!=0;
        ui.rec_duration_s=recording?(uint32_t)((now-record_start_us)/1000000):record_duration_s;
        char clock_text[64]="Установите часы";time_t wall=time(NULL);struct tm wall_tm;localtime_r(&wall,&wall_tm);
        if(tab5_board_time_valid())strftime(clock_text,sizeof(clock_text),"%d.%m.%Y %H:%M:%S",&wall_tm);
        ui.datetime_known=tab5_board_time_valid();
        if(ui.datetime_known)ui.datetime=(tab5_ui_datetime_t){.year=wall_tm.tm_year+1900,.month=wall_tm.tm_mon+1,
            .day=wall_tm.tm_mday,.hour=wall_tm.tm_hour,.minute=wall_tm.tm_min,.second=wall_tm.tm_sec};
        ui.clock.known=true;ui.clock.time_valid=tab5_board_time_valid();
        ui.clock.battery_present=isfinite(battery) && battery>5;ui.clock.battery_v=battery;
        snprintf(ui.clock.datetime,sizeof(ui.clock.datetime),"%.31s",clock_text);
        tab5_panel_describe(ui.panel,sizeof(ui.panel));
        snprintf(ui.firmware,sizeof(ui.firmware),"%s",FW_VERSION);
        if(isfinite(battery) && battery>5)snprintf(ui.clock_power,sizeof(ui.clock_power),"%s | %.2f В",clock_text,(double)battery);
        else snprintf(ui.clock_power,sizeof(ui.clock_power),"%s | внешнее питание",clock_text);
        if(stream_ms && now-last_stream>=(uint64_t)stream_ms*1000){last_stream=now;char line[200];const tab5_measure_sensor_t *p=ui.measurement.sensor;
            snprintf(line,sizeof(line),"S,%lu,%c,%.3f,%.3f,%c,%.3f,%.3f,%.2f",(unsigned long)(now/1000),p[0].valid?'O':p[0].good?'L':'A',p[0].x,p[0].y,p[1].valid?'O':p[1].good?'L':'A',p[1].x,p[1].y,isfinite(battery)?battery:0);broadcast(line);}
        ui.network.ap_enabled=network.ap_enabled;ui.network.sta_enabled=network.sta_enabled;
        memcpy(ui.network.ap_ssid,network.ap_ssid,sizeof(ui.network.ap_ssid));memcpy(ui.network.ap_password,network.ap_password,sizeof(ui.network.ap_password));
        memcpy(ui.network.sta_ssid,network.sta_ssid,sizeof(ui.network.sta_ssid));memcpy(ui.network.sta_password,network.sta_password,sizeof(ui.network.sta_password));
        tab5_medium_status_t sd,flash;tab5_services_status(TAB5_SD,&sd);tab5_services_status(TAB5_USB,&flash);
        ui.sd=(tab5_ui_medium_t){.known=true,.mounted=sd.mounted,.recording=sd.recording,.failed=sd.failed,.written=sd.written};
        ui.usb=(tab5_ui_medium_t){.known=true,.mounted=flash.mounted,.recording=flash.recording,.failed=flash.failed,.written=flash.written};
        ui.capacity[0].details_known=sd.mounted;ui.capacity[1].details_known=flash.mounted;
        for(unsigned i=0;i<2;i++)if(ui.capacity[i].details_known){
            snprintf(ui.capacity[i].filesystem,sizeof(ui.capacity[i].filesystem),"FAT32");
            snprintf(ui.capacity[i].folder,sizeof(ui.capacity[i].folder),"/");}
        snprintf(ui.status,sizeof(ui.status),"%s",notice);
        tab5_network_snapshot_t net;tab5_services_network_snapshot(&net);snprintf(ui.network_status,sizeof(ui.network_status),"AP %s  STA %s | TCP %u | %s",net.ap_started?net.ap_ip:"off",net.sta_connected?net.sta_ip:"off",network.tcp_port,esp_err_to_name(net.last_error));
        ui.link=(tab5_ui_link_t){.known=true,.ap_started=net.ap_started,.sta_connected=net.sta_connected,.tcp_port=network.tcp_port,.error=net.last_error};
        ui.tcp_clients_known=true;ui.tcp_clients=(uint16_t)net.clients;
        snprintf(ui.link.ap_ip,sizeof(ui.link.ap_ip),"%s",net.ap_ip);snprintf(ui.link.sta_ip,sizeof(ui.link.sta_ip),"%s",net.sta_ip);
        xSemaphoreGive(app_mutex);
        if(now-last_ui>=100000){last_ui=now;if(bsp_display_lock(20)){tab5_ui_update(&ui);bsp_display_unlock();
            if(!trial_confirmed && now>=2000000 && esp_get_free_heap_size()>16384){
                esp_err_t health=tab5_services_confirm_boot();
                trial_confirmed=health==ESP_OK || health==ESP_ERR_NOT_FOUND;
            }}}
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
