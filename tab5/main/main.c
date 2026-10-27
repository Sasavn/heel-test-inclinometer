#include "tab5_measure.h"
#include "tab5_ui.h"
#include "tab5_board.h"
#include "tab5_services.h"
#include "tab5_panel.h"
#include "tab5_time.h"
#include "tab5_time_sync.h"
#include "tab5_resources.h"
#include "tab5_draw_profile.h"
#include "tab5_native_port.h"
#include "native/native_ui.h"
#include "bsp/esp-bsp.h"
#include "driver/usb_serial_jtag.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
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
static uint8_t brightness_pct=60;
static uint32_t rejected_actions; /* atomic bitset, UI producer/main consumer */
static int sync_offset_minutes=180;
static bool automatic_time_attempted;
static char time_sync_status[128]="NTP: автоматически при подключении WiFi";
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
static const char *filesystem_name(tab5_fs_type_t type) {
    return type==TAB5_FS_FAT32?"FAT32":type==TAB5_FS_EXFAT?"exFAT":"unknown";
}

static struct {
    bool known,cpu_known;
    unsigned cpu;
    uint32_t idle[configNUMBER_OF_CORES];
    uint64_t at;
    size_t ram_used,ram_total,psram_used,psram_total;
} resources;
/* Main and status hold app_mutex. No task enumeration or per-update allocation. */
static void resources_refresh(uint64_t now) {
    uint64_t dt=now-resources.at;
    if(resources.known&&dt<1000000)return;
    uint64_t idle_delta=0;
    for(unsigned core=0;core<configNUMBER_OF_CORES;core++){
        uint32_t idle=(uint32_t)ulTaskGetIdleRunTimeCounterForCore(core);
        idle_delta+=(uint32_t)(idle-resources.idle[core]);resources.idle[core]=idle;
    }
    resources.cpu_known=resources.known&&dt>0&&dt<=UINT32_MAX;
    if(resources.cpu_known)resources.cpu=tab5_cpu_busy_pct(dt,idle_delta,configNUMBER_OF_CORES);
    resources.ram_total=heap_caps_get_total_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    size_t free_ram=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    resources.ram_used=free_ram<resources.ram_total?resources.ram_total-free_ram:0;
    resources.psram_total=heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t free_psram=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    resources.psram_used=free_psram<resources.psram_total?resources.psram_total-free_psram:0;
    resources.at=now;resources.known=true;
}

/* Wall times, not CPU utilization. Display events run in the LVGL task;
 * protocol reads/reset run in another task, hence the short critical sections. */
typedef struct {
    uint64_t since,render_start,flush_start,refresh_start,wait_start;
    uint64_t render_us,flush_us,refresh_us,update_us,wait_us;
    uint32_t renders,flushes,refreshes,updates,lock_misses,refresh_max_us;
    uint32_t sample_age_us[2],sample_age_max_us[2];
} ui_perf_t;
static ui_perf_t ui_perf;
static portMUX_TYPE ui_perf_lock=portMUX_INITIALIZER_UNLOCKED;
static void ui_perf_reply(bool reset,tab5_service_reply_fn reply,void *ctx) {
    if(reset)tab5_draw_profile_snapshot(NULL,true);
    uint64_t now=esp_timer_get_time();ui_perf_t p;
    portENTER_CRITICAL(&ui_perf_lock);
    if(reset){ui_perf=(ui_perf_t){.since=now};}
    p=ui_perf;portEXIT_CRITICAL(&ui_perf_lock);
    tab5_native_port_stats_t native;tab5_native_port_stats(&native,reset);
    p.renders=native.renders;p.render_us=native.render_us;p.flushes=native.flushes;p.flush_us=native.flush_us;
    p.refreshes=native.refreshes;p.refresh_us=native.refresh_us;p.refresh_max_us=native.refresh_max_us;p.wait_us=native.wait_us;
    char out[640];snprintf(out,sizeof(out),
        "{\"elapsed_ms\":%llu,\"updates\":%lu,\"update_us\":%llu,\"renders\":%lu,\"render_us\":%llu,\"flushes\":%lu,\"flush_us\":%llu,\"refreshes\":%lu,\"refresh_us\":%llu,\"refresh_max_us\":%lu,\"lock_misses\":%lu,\"sample_age_us\":[%lu,%lu],\"sample_age_max_us\":[%lu,%lu],\"flush_wait_us\":%llu,\"rotate_us\":%llu,\"draw_errors\":%lu,\"backend\":\"native\",\"ppa\":%s,\"reset_reason\":%d}",
        (unsigned long long)((now-p.since)/1000),(unsigned long)p.updates,(unsigned long long)p.update_us,
        (unsigned long)p.renders,(unsigned long long)p.render_us,(unsigned long)p.flushes,(unsigned long long)p.flush_us,
        (unsigned long)p.refreshes,(unsigned long long)p.refresh_us,(unsigned long)p.refresh_max_us,(unsigned long)p.lock_misses,
        (unsigned long)p.sample_age_us[0],(unsigned long)p.sample_age_us[1],(unsigned long)p.sample_age_max_us[0],(unsigned long)p.sample_age_max_us[1],
        (unsigned long long)p.wait_us,(unsigned long long)native.rotate_us,(unsigned long)native.errors,
        "true"
        ,(int)esp_reset_reason()
    );reply(ctx,out);
    snprintf(out,sizeof(out),"{\"schedule\":\"native\",\"interactive_renders\":%lu,\"input_render_wait_max_us\":%lu}",
             (unsigned long)native.interactive_renders,(unsigned long)native.input_render_wait_max_us);
    reply(ctx,out);
    snprintf(out,sizeof(out),"{\"rotation_verify\":{\"checks\":%lu,\"errors\":%lu,\"mismatches\":%llu,\"scan_us\":%llu}}",
             (unsigned long)native.verify_checks,(unsigned long)native.verify_errors,
             (unsigned long long)native.verify_mismatches,(unsigned long long)native.verify_us);
    reply(ctx,out);
    snprintf(out,sizeof(out),"{\"input_timing\":\"sampled_to_copy_eof\",\"touch_reads\":%lu,\"touch_read_gap_max_us\":%lu,\"sample_render_count\":%lu,\"sample_render_us\":%llu,\"sample_render_max_us\":%lu,\"sample_lcd_count\":%lu,\"sample_lcd_us\":%llu,\"sample_lcd_max_us\":%lu,\"ppa_timeouts\":%lu,\"lcd_timeouts\":%lu,\"submit_errors\":%lu,\"pipeline_state\":%lu}",
             (unsigned long)native.touch_reads,(unsigned long)native.touch_read_gap_max_us,
             (unsigned long)native.sample_render_count,(unsigned long long)native.sample_render_us,(unsigned long)native.sample_render_max_us,
             (unsigned long)native.sample_lcd_count,(unsigned long long)native.sample_lcd_us,(unsigned long)native.sample_lcd_max_us,
             (unsigned long)native.ppa_timeouts,(unsigned long)native.lcd_timeouts,(unsigned long)native.submit_errors,(unsigned long)native.pipeline_state);
    reply(ctx,out);
    snprintf(out,sizeof(out),"{\"transport\":\"roi_or_canonical\",\"lcd_copy_bytes\":%llu,\"full_lcd_copies\":%lu,\"partial_lcd_copies\":%lu}",
             (unsigned long long)native.lcd_copy_bytes,(unsigned long)native.full_lcd_copies,(unsigned long)native.partial_lcd_copies);
    reply(ctx,out);
    snprintf(out,sizeof(out),"{\"gpu_compose\":true,\"frames\":%lu,\"copy_jobs\":%lu,\"ppa_jobs\":%lu,\"fallbacks\":%lu,\"timeouts\":%lu,\"copy_bytes\":%llu,\"copy_us\":%llu,\"ppa_us\":%llu,\"cpu_jobs\":%lu,\"cpu_pixels\":%llu,\"cpu_us\":%llu}",
             (unsigned long)native.compose_frames,(unsigned long)native.compose_copy_jobs,(unsigned long)native.compose_ppa_jobs,
             (unsigned long)native.compose_fallbacks,(unsigned long)native.compose_timeouts,
             (unsigned long long)native.compose_copy_bytes,(unsigned long long)native.compose_copy_us,(unsigned long long)native.compose_ppa_us,
             (unsigned long)native.compose_cpu_jobs,(unsigned long long)native.compose_cpu_pixels,(unsigned long long)native.compose_cpu_us);
    reply(ctx,out);
    snprintf(out,sizeof(out),"{\"cpu_tiles_enabled\":%s}",tab5_native_port_cpu_tiles_enabled()?"true":"false");reply(ctx,out);
    snprintf(out,sizeof(out),"{\"pipeline_overlap\":true,\"worker_enabled\":%s,\"renders\":%lu,\"us\":%llu,\"queued_peak\":%lu,\"completed_frames\":%lu,\"backpressure\":%lu,\"unaligned_views\":%lu}",
             native.worker_enabled?"true":"false",(unsigned long)native.overlap_renders,(unsigned long long)native.overlap_us,
             (unsigned long)native.queued_peak,(unsigned long)native.worker_completed_frames,
             (unsigned long)native.producer_backpressure,(unsigned long)native.compose_unaligned_views);reply(ctx,out);
    snprintf(out,sizeof(out),"{\"cache_source_sync\":true,\"jobs\":%lu,\"hits\":%lu,\"bytes\":%llu}",
             (unsigned long)native.compose_source_sync_jobs,(unsigned long)native.compose_source_sync_hits,
             (unsigned long long)native.compose_source_sync_bytes);reply(ctx,out);
    snprintf(out,sizeof(out),"{\"runtime_phases\":true,\"touch_read_us\":%llu,\"cpu_tick_us\":%llu,\"input_dispatch_us\":%llu,\"ui_lock_wait_us\":%llu,\"wake_wait_us\":%llu,\"callback_process_delay_us\":%llu,\"cache_callback_process_delay_us\":%llu,\"ppa_callback_process_delay_us\":%llu,\"lcd_callback_process_delay_us\":%llu}",
             (unsigned long long)native.touch_read_us,(unsigned long long)native.cpu_tick_us,
             (unsigned long long)native.input_dispatch_us,(unsigned long long)native.ui_lock_wait_us,
             (unsigned long long)native.wake_wait_us,(unsigned long long)native.callback_process_delay_us,
             (unsigned long long)native.cache_callback_process_delay_us,(unsigned long long)native.ppa_callback_process_delay_us,
             (unsigned long long)native.lcd_callback_process_delay_us);reply(ctx,out);
    nui_layer_cache_stats_t cache={0};nui_native_stats_t renderer={0};unsigned surface_alignment=0;bool cache_known=tab5_native_port_lock(20);
    if(cache_known){if(reset)nui_layer_cache_reset_stats();nui_layer_cache_get_stats(&cache);nui_native_profile_snapshot(&renderer);surface_alignment=nui_native_surface_alignment();tab5_native_port_unlock();}
    snprintf(out,sizeof(out),"{\"surface_views\":true,\"known\":%s,\"alignment\":%u,\"unaligned_views\":%llu}",
             cache_known?"true":"false",surface_alignment,(unsigned long long)cache.deferred_unaligned_views);reply(ctx,out);
    snprintf(out,sizeof(out),"{\"renderer_totals\":true,\"known\":%s,\"frames\":%llu,\"render_us\":%llu,\"cpu_cache_pixels\":%llu}",
             cache_known?"true":"false",(unsigned long long)renderer.frames,(unsigned long long)renderer.render_us,(unsigned long long)cache.cpu_blit_pixels);
    reply(ctx,out);
    snprintf(out,sizeof(out),"{\"renderer_phases\":true,\"known\":%s,\"align_us\":%llu,\"logical_render_us\":%llu,\"gc_us\":%llu,\"callback_audit_us\":%llu,\"cache_cpu_blit_us\":%llu}",
             cache_known?"true":"false",(unsigned long long)renderer.align_us,(unsigned long long)renderer.logical_render_us,
             (unsigned long long)renderer.gc_us,(unsigned long long)renderer.callback_audit_us,
             (unsigned long long)renderer.cache_cpu_blit_us);reply(ctx,out);
    snprintf(out,sizeof(out),"{\"layer_cache\":true,\"known\":%s,\"hits\":%llu,\"misses\":%llu,\"builds\":%llu,\"updates\":%llu,\"build_us\":%llu,\"blit_pixels\":%llu,\"fallbacks\":%llu,\"bytes\":%u,\"peak_bytes\":%u,\"limit_bytes\":%u}",
             cache_known?"true":"false",(unsigned long long)cache.hits,(unsigned long long)cache.misses,
             (unsigned long long)cache.builds,(unsigned long long)cache.updates,(unsigned long long)cache.build_us,
             (unsigned long long)cache.blit_pixels,(unsigned long long)cache.fallbacks,
             (unsigned)cache.bytes,(unsigned)cache.peak_bytes,(unsigned)cache.limit_bytes);
    reply(ctx,out);
    snprintf(out,sizeof(out),"{\"cache_occlusion\":true,\"known\":%s,\"splits\":%llu,\"pixels\":%llu,\"fallbacks\":%llu}",
             cache_known?"true":"false",(unsigned long long)cache.occlusion_splits,
             (unsigned long long)cache.occlusion_pixels,(unsigned long long)cache.occlusion_fallbacks);
    reply(ctx,out);
    snprintf(out,sizeof(out),"{\"deferred_cache\":true,\"known\":%s,\"views\":%llu,\"pixels\":%llu,\"rejects\":%llu,\"portrait_updates\":%llu,\"portrait_us\":%llu,\"materialize_pixels\":%llu,\"pins\":%llu}",
             cache_known?"true":"false",(unsigned long long)cache.deferred_views,(unsigned long long)cache.deferred_pixels,
             (unsigned long long)cache.defer_rejects,(unsigned long long)cache.portrait_updates,
             (unsigned long long)cache.portrait_us,(unsigned long long)cache.materialize_pixels,(unsigned long long)cache.pins);
    reply(ctx,out);
    tab5_draw_profile_snapshot_t draw;tab5_draw_profile_snapshot(&draw,false);
    for(unsigned i=0;i<TAB5_DRAW_PROFILE_COUNT;i++){
        tab5_draw_profile_format(out,sizeof(out),&draw.entries[i]);reply(ctx,out);
    }
    reply(ctx,"OK ui perf");
}

static void ui_benchmark_reply(tab5_service_reply_fn reply,void *ctx){
    tab5_native_scroll_benchmark_status_t b;tab5_native_port_scroll_benchmark_status(&b);
    cJSON *j=cJSON_CreateObject();if(!j){reply(ctx,"ERR benchmark JSON");return;}
    cJSON_AddStringToObject(j,"benchmark","settings_scroll");
    cJSON_AddBoolToObject(j,"pending",b.pending);cJSON_AddBoolToObject(j,"active",b.active);
    cJSON_AddBoolToObject(j,"finished",b.finished);cJSON_AddBoolToObject(j,"restored",b.restored);
    cJSON_AddBoolToObject(j,"cache_enabled",b.cache_enabled);
#define BENCH_NUM(name) cJSON_AddNumberToObject(j,#name,b.name)
    BENCH_NUM(request_id);BENCH_NUM(requested_frames);BENCH_NUM(completed_frames);BENCH_NUM(accepted_frames);
    BENCH_NUM(interval_us);BENCH_NUM(reason);BENCH_NUM(touch_reads);BENCH_NUM(fps_milli);
    BENCH_NUM(scroll_min);BENCH_NUM(scroll_max);BENCH_NUM(started_us);BENCH_NUM(elapsed_us);
    BENCH_NUM(setup_us);BENCH_NUM(cold_frame_us);BENCH_NUM(render_us);BENCH_NUM(rotate_us);
    BENCH_NUM(lcd_submit_us);BENCH_NUM(lcd_wait_us);BENCH_NUM(refresh_us);BENCH_NUM(lcd_copy_bytes);
#undef BENCH_NUM
    char *out=cJSON_PrintUnformatted(j);if(out){reply(ctx,out);cJSON_free(out);}else reply(ctx,"ERR benchmark JSON");
    cJSON_Delete(j);
}
static void boot_failed(void) {
    printf("BOOTFAIL caller=%p internal_free=%u internal_largest=%u psram_free=%u\n",
        __builtin_return_address(0),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
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
    (void)unused;
    if(xQueueSend(actions,a,0)!=pdTRUE&&(unsigned)a->kind<32)
        __atomic_fetch_or(&rejected_actions,1u<<(unsigned)a->kind,__ATOMIC_RELEASE);
}
static void consume_rejected_actions(void){
    uint32_t rejected=__atomic_exchange_n(&rejected_actions,0,__ATOMIC_ACQ_REL);
    for(unsigned kind=0;kind<32;kind++)if(rejected&(1u<<kind)){
        ui_result_kind=(tab5_ui_action_kind_t)kind;ui_result_success=false;ui_result_serial++;
        snprintf(notice,sizeof(notice),"Очередь команд переполнена; повторите");
        if(kind==TAB5_UI_TIME||kind==TAB5_UI_TIME_SYNC)snprintf(time_sync_status,sizeof(time_sync_status),"Очередь команд переполнена; повторите");
    }
}
static void broadcast(const char *line) { usb_reply(NULL,line);tab5_services_publish(line); }
static bool persist(void) {
    nvs_handle_t n;
    if(nvs_open("tab5",NVS_READWRITE,&n)!=ESP_OK){snprintf(notice,sizeof(notice),"Не удалось открыть настройки");return false;}
    tab5_measure_snapshot_t s;tab5_measure_snapshot(&s);
    esp_err_t e=nvs_set_blob(n,"measure",&s.config,sizeof(s.config));
    if(e==ESP_OK)e=nvs_set_blob(n,"network",&network,sizeof(network));
    if(e==ESP_OK)e=nvs_set_u8(n,"theme",light_theme);
    if(e==ESP_OK)e=nvs_set_u8(n,"brightness",brightness_pct);
    if(e==ESP_OK)e=nvs_set_u32(n,"session",measurement_number);
    if(e==ESP_OK)e=nvs_commit(n);
    nvs_close(n);if(e!=ESP_OK)snprintf(notice,sizeof(notice),"Настройки не сохранены: %s",esp_err_to_name(e));
    return e==ESP_OK;
}
/* Only the offset is durable: never save an epoch pretending to advance while
 * powered off. Main/app_mutex owns all clock and RTC writes. */
static bool persist_timezone(int offset){
    nvs_handle_t n;if(nvs_open("tab5",NVS_READWRITE,&n)!=ESP_OK)return false;
    esp_err_t e=nvs_set_i16(n,"timezone",(int16_t)offset);
    if(e==ESP_OK)e=nvs_commit(n);
    nvs_close(n);return e==ESP_OK;
}
static bool apply_wall_time(int64_t utc,int offset){
    tab5_time_apply_result_t r=tab5_board_set_utc(utc,offset);
    if(!r.valid){snprintf(time_sync_status,sizeof(time_sync_status),"Недопустимая дата/пояс: RTC поддерживает 2000–2099");return false;}
    if(!r.system_ok){snprintf(time_sync_status,sizeof(time_sync_status),"Не удалось установить системные часы");return false;}
    tab5_time_set_offset(offset);
    bool saved=persist_timezone(offset);
    if(!r.rtc_ok)snprintf(time_sync_status,sizeof(time_sync_status),"Системные часы установлены; ошибка записи/флагов RTC%s",saved?"":"; пояс не сохранён");
    else if(!saved)snprintf(time_sync_status,sizeof(time_sync_status),"Часы и RTC установлены; пояс не сохранён во flash");
    else snprintf(time_sync_status,sizeof(time_sync_status),"%s",recording?"Время скорректировано; запись продолжается":"Дата, время и часовой пояс установлены");
    /* Ms, record_start_us, record_name and measurement timebase stay unchanged.
     * Date/Time columns may jump; protocol analysis uses monotonic Ms. */
    return r.rtc_ok&&saved;
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
    uint8_t brightness=60;if(nvs_get_u8(n,"brightness",&brightness)==ESP_OK&&brightness>=10&&brightness<=100)brightness_pct=brightness;
    int16_t offset=180;if(nvs_get_i16(n,"timezone",&offset)==ESP_OK&&tab5_time_offset_valid(offset))tab5_time_set_offset(offset);
    uint32_t session=0;nvs_get_u32(n,"session",&session);measurement_number=session;nvs_close(n);
}
static esp_err_t set_recording(bool on) {
    if(on==recording)return ESP_OK;
    if(on)tab5_native_port_cancel_scroll_benchmark();
    if(!on){xSemaphoreTake(route_mutex,portMAX_DELAY);route_recording=false;xSemaphoreGive(route_mutex);
        tab5_services_record_stop();record_duration_s=(uint32_t)((esp_timer_get_time()-record_start_us)/1000000);
        recording=false;snprintf(notice,sizeof(notice),"Запись остановлена");return ESP_OK;}
    if(!tab5_board_time_valid()){snprintf(notice,sizeof(notice),"Перед записью установите дату и время");return ESP_ERR_INVALID_STATE;}
    measurement_number++;persist();
    time_t now=time(NULL);struct tm t;tab5_time_local_now(now,&t);char session[64];
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
    cJSON_AddBoolToObject(j,"time_tz_offset",true);cJSON_AddNumberToObject(j,"timezone_minutes",tab5_time_offset());
    cJSON_AddNumberToObject(j,"proto",2);cJSON_AddNumberToObject(j,"chip_revision",chip.revision);
    cJSON_AddBoolToObject(j,"time_sync_busy",tab5_time_sync_busy());
    cJSON_AddStringToObject(j,"time_sync_status",time_sync_status);
    cJSON_AddNumberToObject(j,"ms",esp_timer_get_time()/1000);cJSON_AddNumberToObject(j,"up",esp_timer_get_time()/1000000);
    char date[32]="unsynchronized";time_t now=time(NULL);struct tm t;tab5_time_local_now(now,&t);
    if(tab5_board_time_valid())strftime(date,sizeof(date),"%Y-%m-%d %H:%M:%S",&t);
    cJSON_AddStringToObject(j,"time",date);cJSON_AddBoolToObject(j,"rtc",tab5_board_rtc_valid());
    cJSON_AddNumberToObject(j,"freq",s.config.frequency_hz);cJSON_AddNumberToObject(j,"alpha",s.config.alpha);
    cJSON_AddNumberToObject(j,"gap",s.config.gap_ms);cJSON_AddStringToObject(j,"theme",light_theme?"light":"dark");
    cJSON_AddNumberToObject(j,"rollwin",s.config.roll_window_s);cJSON_AddNumberToObject(j,"rollhz",s.config.roll_rate_hz);
    cJSON_AddNumberToObject(j,"rollcalm",s.config.roll_calm_deg);cJSON_AddNumberToObject(j,"rollhyst",s.config.roll_hysteresis_deg);
    if(resources.cpu_known)cJSON_AddNumberToObject(j,"cpu",resources.cpu);else cJSON_AddNullToObject(j,"cpu");
    cJSON_AddNullToObject(j,"rate");
    cJSON *memory=cJSON_AddObjectToObject(j,"memory");
    if(resources.known){cJSON_AddNumberToObject(memory,"ram_used",resources.ram_used);cJSON_AddNumberToObject(memory,"ram_total",resources.ram_total);cJSON_AddNumberToObject(memory,"psram_used",resources.psram_used);cJSON_AddNumberToObject(memory,"psram_total",resources.psram_total);}
    cJSON *a=cJSON_AddArrayToObject(j,"sens");
    for(unsigned i=0;i<2;i++){
        const tab5_measure_sensor_t *p=&s.sensor[i];cJSON *d=cJSON_CreateObject();cJSON_AddItemToArray(a,d);
        cJSON_AddNumberToObject(d,"addr",p->address);cJSON_AddStringToObject(d,"st",p->valid?"OK":p->good?"LOST":"ABSENT");
        add_num(d,"x",p->valid?p->x:NAN);add_num(d,"y",p->valid?p->y:NAN);
        add_num(d,"live_x",p->valid?p->decoded_x-p->zero_x:NAN);add_num(d,"live_y",p->valid?p->decoded_y-p->zero_y:NAN);
        add_num(d,"ox",p->zero_x);add_num(d,"oy",p->zero_y);add_num(d,"roll_x",p->roll_x);add_num(d,"roll_y",p->roll_y);
        cJSON_AddBoolToObject(d,"calm_x",p->calm_x);cJSON_AddBoolToObject(d,"calm_y",p->calm_y);
        cJSON_AddNumberToObject(d,"fill",p->roll_fill_s);cJSON_AddNumberToObject(d,"ok",p->good);cJSON_AddNumberToObject(d,"err",p->errors);
        static const char *const error_names[]={"none","timeout","crc","frame","exception","io"};
        cJSON_AddStringToObject(d,"last_err",error_names[p->last_error<=TAB5_MEASURE_IO?p->last_error:0]);
        cJSON_AddNumberToObject(d,"fail_streak",p->fail_streak);cJSON_AddNumberToObject(d,"sample_us",p->timestamp_us);
        if(p->good)cJSON_AddNumberToObject(d,"age",(esp_timer_get_time()-p->timestamp_us)/1000);else cJSON_AddNullToObject(d,"age");
    }
    cJSON_AddBoolToObject(j,"stable",s.stable);cJSON_AddStringToObject(j,"sd",sd.failed?"ERR":sd.recording?"REC":sd.mounted?"READY":"NOCARD");
    cJSON_AddBoolToObject(j,"sd_known",sd.known);cJSON_AddStringToObject(j,"sd_fs",filesystem_name(sd.fs_type));
    cJSON_AddNumberToObject(j,"sd_err",sd.error);cJSON_AddNumberToObject(j,"rows",sd.recording?sd.written:usb.written);
    cJSON *capacities=cJSON_AddObjectToObject(j,"storage_capacity");
    for(unsigned i=0;i<2;i++){
        const tab5_medium_status_t *m=i?&usb:&sd;
        cJSON *c=cJSON_AddObjectToObject(capacities,i?"usb":"sd");
        cJSON_AddBoolToObject(c,"known",m->mounted&&m->capacity_known);
        cJSON_AddNumberToObject(c,"volume_id",m->volume_id);
        cJSON_AddNumberToObject(c,"total_bytes",m->total_bytes);
        cJSON_AddNumberToObject(c,"free_bytes",m->free_bytes);
    }
    cJSON_AddNumberToObject(j,"rec_s",recording?(esp_timer_get_time()-record_start_us)/1000000:0);
    cJSON_AddNumberToObject(j,"file",measurement_number);cJSON_AddNumberToObject(j,"rec_mask",(sd.recording||usb.recording)?3:0);cJSON_AddBoolToObject(j,"rec_sw",recording);
    add_num(j,"bat_v",battery);cJSON_AddBoolToObject(j,"bat_present",isfinite(battery)&&battery>5);cJSON_AddNullToObject(j,"bat_pct");
    cJSON_AddStringToObject(j,"svc","idle");cJSON_AddStringToObject(j,"xfer","idle");
    cJSON *u=cJSON_AddObjectToObject(j,"usb_storage");cJSON_AddBoolToObject(u,"mounted",usb.mounted);cJSON_AddBoolToObject(u,"recording",usb.recording);
    cJSON_AddBoolToObject(u,"known",usb.known);cJSON_AddStringToObject(u,"fs",filesystem_name(usb.fs_type));
    cJSON_AddBoolToObject(u,"failed",usb.failed);cJSON_AddNumberToObject(u,"written",usb.written);cJSON_AddNumberToObject(u,"dropped",usb.dropped);
    cJSON *n=cJSON_AddObjectToObject(j,"network");cJSON_AddBoolToObject(n,"ap",net.ap_started);cJSON_AddBoolToObject(n,"sta",net.sta_connected);
    cJSON_AddBoolToObject(n,"known",net.known);
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
    else if(strcmp(line,"ui bench status")==0)ui_benchmark_reply(reply,ctx);
    else if(strcmp(line,"ui bench cancel")==0){tab5_native_port_cancel_scroll_benchmark();reply(ctx,"OK benchmark cancel queued");}
    else if(strcmp(line,"ui bench scroll off")==0 || strcmp(line,"ui bench scroll on")==0 || strcmp(line,"ui bench scroll legacy")==0){
        bool cache=strcmp(line,"ui bench scroll on")==0;uint32_t interval=strcmp(line,"ui bench scroll legacy")==0?40000:16667;
        if(recording)reply(ctx,"ERR benchmark forbidden while recording");
        else reply(ctx,tab5_native_port_request_scroll_benchmark(120,interval,cache)?"OK benchmark queued":"ERR benchmark unavailable/busy");
    }
    else if(strcmp(line,"ui compose cpu on")==0 || strcmp(line,"ui compose cpu off")==0){
        if(recording)reply(ctx,"ERR diagnostic forbidden while recording");
        else {bool enabled=strcmp(line,"ui compose cpu on")==0;tab5_native_port_set_cpu_tiles_enabled(enabled);reply(ctx,enabled?"OK CPU tiles on":"OK CPU tiles off");}
    }
    else if(strcmp(line,"ui verify rotate")==0)reply(ctx,tab5_native_port_request_verify(2)?"OK rotation verification queued":"ERR renderer not ready");
    else if(strcmp(line,"ui perf")==0 || strcmp(line,"ui perf reset")==0)ui_perf_reply(strcmp(line,"ui perf reset")==0,reply,ctx);
    else if(strcmp(line,"sd auto on")==0 || strcmp(line,"sd auto off")==0){
        bool enabled=strcmp(line,"sd auto on")==0;
        reply(ctx,tab5_services_set_auto_retry(TAB5_SD,enabled)==ESP_OK?(enabled?"OK sd auto on":"OK sd auto off"):"ERR sd auto");
    }
    else if(strcmp(line,"sd retry")==0)reply(ctx,tab5_services_retry(TAB5_SD)==ESP_OK?"OK sd ready":"ERR sd unavailable");
    else if(strcmp(line,"ui tasks")==0){
        UBaseType_t cap=uxTaskGetNumberOfTasks()+8;
        TaskStatus_t *tasks=cap<=96?calloc(cap,sizeof(*tasks)):NULL;
        if(!tasks)reply(ctx,"ERR task snapshot memory");
        else{configRUN_TIME_COUNTER_TYPE total=0;UBaseType_t count=uxTaskGetSystemState(tasks,cap,&total);
            cJSON *j=cJSON_CreateObject();cJSON_AddNumberToObject(j,"task_clock",total);
            cJSON *a=cJSON_AddArrayToObject(j,"tasks");
            for(UBaseType_t i=0;i<count;i++){TaskStatus_t *t=&tasks[i];cJSON *d=cJSON_CreateObject();cJSON_AddItemToArray(a,d);
                cJSON_AddStringToObject(d,"task",t->pcTaskName);cJSON_AddNumberToObject(d,"run",t->ulRunTimeCounter);cJSON_AddNumberToObject(d,"priority",t->uxCurrentPriority);
                cJSON_AddNumberToObject(d,"stack_min",t->usStackHighWaterMark);}
            char *text=cJSON_PrintUnformatted(j);if(text){reply(ctx,text);cJSON_free(text);}else reply(ctx,"ERR task snapshot JSON");
            cJSON_Delete(j);free(tasks);reply(ctx,"OK ui tasks");}
    }
    else if(strcmp(line,"zero")==0 || strcmp(line,"zero reset")==0){tab5_measure_zero(strcmp(line,"zero reset")==0);reply(ctx,"OK zero");}
    else if(strcmp(line,"rec on")==0 || strcmp(line,"rec off")==0){reply(ctx,set_recording(strcmp(line,"rec on")==0)==ESP_OK?"OK recording":"ERR recording unavailable");}
    else if(strcmp(line,"samples on")==0 || strcmp(line,"samples off")==0){samples_on=strcmp(line,"samples on")==0;reply(ctx,samples_on?"OK samples on, dropped 0":"OK samples off, dropped 0");}
    else if(strncmp(line,"stream ",7)==0){char *end;unsigned long ms=strtoul(line+7,&end,10);if(*end || (ms && (ms<50 || ms>5000)))reply(ctx,"ERR stream range 0 or 50..5000");else{stream_ms=ms;reply(ctx,"# S,t_ms,s2,x2,y2,s3,x3,y3,bat_v");reply(ctx,"OK stream");}}
    else if(strncmp(line,"set time ",9)==0){
        struct tm t;int offset;bool explicit_offset;int64_t utc;
        if(tab5_time_sync_busy())reply(ctx,"ERR time sync busy");
        else if(!tab5_time_parse_command(line+9,tab5_time_offset(),&t,&offset,&explicit_offset)||
                !tab5_time_local_to_utc(&t,offset,&utc))reply(ctx,"ERR time/calendar/timezone");
        else{
            bool ok=apply_wall_time(utc,offset);
            snprintf(notice,sizeof(notice),"%s",time_sync_status);
            reply(ctx,ok?"OK time":"ERR clock/RTC/timezone; system clock may be updated");
        }}
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
    else if(strcmp(line,"help")==0){reply(ctx,"ver status stream N samples on|off zero [reset] rec on|off set time YYYY-MM-DD HH:MM:SS [tz=minutes] set freq|alpha|gap|rollwin|rollhz|rollcalm|rollhyst N");reply(ctx,"OTA: ota info/begin/chunk/end/abort; network configured on touchscreen");reply(ctx,"OK");}
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
    time_t now=time(NULL);struct tm t;tab5_time_local_now(now,&t);
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
    tab5_time_set_offset(180); /* local-wall RTC compatibility before board_init */
    tab5_measure_config_t cfg;load_config(&cfg);
    if(bsp_i2c_init()!=ESP_OK){boot_failed();return;}
    tab5_board_init();
    usb_serial_jtag_driver_config_t usb={.tx_buffer_size=4096,.rx_buffer_size=4096};
    if(usb_serial_jtag_driver_install(&usb)!=ESP_OK){boot_failed();return;}
    if(tab5_measure_start(&cfg,sample_callback,NULL)!=ESP_OK){boot_failed();return;}
    /* Display/input must exist before optional storage/radio initialization.
     * In particular, inserting an unreadable card must not hide application
     * startup behind a synchronous filesystem probe. Media mount is deferred
     * to its service worker; the UI initially shows unknown/not-ready media. */
    ui_perf.since=esp_timer_get_time();
    esp_err_t display_result=tab5_native_port_start(ui_callback,NULL);
    if(display_result!=ESP_OK){printf("Native display startup: %s\n",esp_err_to_name(display_result));boot_failed();return;}
    if(bsp_display_brightness_set(brightness_pct)!=ESP_OK)snprintf(notice,sizeof(notice),"Не удалось восстановить яркость экрана");
    tab5_services_config_t services={.command=command,.network=network};
    esp_err_t services_result=tab5_services_init(&services);
    if(services_result!=ESP_OK){boot_failed();return;}
    if(xTaskCreate(storage_router_task,"sample_storage",6144,NULL,4,NULL)!=pdPASS){boot_failed();return;}
    if(xTaskCreate(usb_task,"protocol_usb",16384,NULL,4,NULL)!=pdPASS){boot_failed();return;}
    persist();
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
                bool backlight_ok=true;
                if(a.brightness_pct){
                    if(a.brightness_pct<10||a.brightness_pct>100)backlight_ok=false;
                    else if(bsp_display_brightness_set(a.brightness_pct)!=ESP_OK)backlight_ok=false;
                    else brightness_pct=a.brightness_pct;
                }
                ui_result_success=persist()&&backlight_ok;ui_result_kind=a.kind;ui_result_serial++;
                if(!backlight_ok)snprintf(notice,sizeof(notice),"Яркость не применена: проверьте дисплей");
                if(ui_result_success)snprintf(notice,sizeof(notice),"Настройки сохранены");}
            else if(a.kind==TAB5_UI_TIME || a.kind==TAB5_UI_TIME_SYNC){
                ui_result_success=false;ui_result_kind=a.kind;
                int offset=a.timezone_set?a.timezone_minutes:tab5_time_offset();
                if(tab5_time_sync_busy()){
                    snprintf(notice,sizeof(notice),"%s",tab5_time_sync_message(TAB5_TIME_SYNC_BUSY));
                    /* Do not turn a second click into a completed NTP result. */
                    if(a.kind!=TAB5_UI_TIME_SYNC)ui_result_serial++;
                }else if(!tab5_time_offset_valid(offset)){
                    snprintf(time_sync_status,sizeof(time_sync_status),"Пояс: UTC−12:00…+14:00, шаг 15 мин");
                    snprintf(notice,sizeof(notice),"%s",time_sync_status);ui_result_serial++;
                }else if(a.kind==TAB5_UI_TIME_SYNC){
                    tab5_network_snapshot_t net;tab5_services_network_snapshot(&net);
                    if(!network.sta_enabled||!net.sta_connected){
                        snprintf(time_sync_status,sizeof(time_sync_status),"%s",tab5_time_sync_message(TAB5_TIME_SYNC_NO_STA));ui_result_serial++;
                    }else if(!tab5_time_sync_start(true)){
                        snprintf(time_sync_status,sizeof(time_sync_status),"%s",tab5_time_sync_message(TAB5_TIME_SYNC_RESOURCE));ui_result_serial++;
                    }else{sync_offset_minutes=offset;snprintf(time_sync_status,sizeof(time_sync_status),"NTP: ожидание pool.ntp.org (до 8 с)");}
                    snprintf(notice,sizeof(notice),"%s",time_sync_status);
                }else{
                    const tab5_ui_datetime_t *d=&a.datetime;
                    struct tm t={.tm_year=d->year-1900,.tm_mon=d->month-1,.tm_mday=d->day,
                                 .tm_hour=d->hour,.tm_min=d->minute,.tm_sec=d->second};
                    int64_t utc;
                    if(!tab5_time_local_to_utc(&t,offset,&utc))snprintf(time_sync_status,sizeof(time_sync_status),"Недопустимая дата или время");
                    else ui_result_success=apply_wall_time(utc,offset);
                    snprintf(notice,sizeof(notice),"%s",time_sync_status);ui_result_serial++;
                }}
            else if(a.kind==TAB5_UI_NETWORK){tab5_network_config_t next=network;next.ap_enabled=a.network.ap_enabled;next.sta_enabled=a.network.sta_enabled;
                memcpy(next.ap_ssid,a.network.ap_ssid,sizeof(next.ap_ssid));memcpy(next.ap_password,a.network.ap_password,sizeof(next.ap_password));
                memcpy(next.sta_ssid,a.network.sta_ssid,sizeof(next.sta_ssid));memcpy(next.sta_password,a.network.sta_password,sizeof(next.sta_password));strcpy(next.auth_token,next.ap_password);
                ui_result_success=false;ui_result_kind=a.kind;ui_result_serial++;
                if(tab5_services_network_configure(&next)==ESP_OK){network=next;ui_result_success=persist();
                    if(ui_result_success)snprintf(notice,sizeof(notice),"Настройки сети применены");}else snprintf(notice,sizeof(notice),"Настройки сети не применены");}}
        tab5_network_snapshot_t time_net;tab5_services_network_snapshot(&time_net);
        if(tab5_time_sync_auto_due(&automatic_time_attempted,
                network.sta_enabled&&time_net.sta_connected,tab5_time_sync_busy(),recording)){
            sync_offset_minutes=tab5_time_offset();
            if(tab5_time_sync_start(true))
                snprintf(time_sync_status,sizeof(time_sync_status),"NTP: автоматическая синхронизация (до 8 с)");
            else snprintf(time_sync_status,sizeof(time_sync_status),"%s",tab5_time_sync_message(TAB5_TIME_SYNC_RESOURCE));
            snprintf(notice,sizeof(notice),"%s",time_sync_status);
        }
        tab5_time_sync_result_t sync;
        if(tab5_time_sync_poll(&sync)){
            ui_result_kind=TAB5_UI_TIME_SYNC;ui_result_serial++;ui_result_success=false;
            int64_t age=esp_timer_get_time()-sync.received_us;
            if(sync.code==TAB5_TIME_SYNC_OK&&age>=0&&age<=10000000){
                tab5_network_snapshot_t current;tab5_services_network_snapshot(&current);
                if(!network.sta_enabled||!current.sta_connected)snprintf(time_sync_status,sizeof(time_sync_status),"%s",tab5_time_sync_message(TAB5_TIME_SYNC_NO_STA));
                else ui_result_success=apply_wall_time(sync.utc_seconds+age/1000000,sync_offset_minutes);
            }else snprintf(time_sync_status,sizeof(time_sync_status),"%s",tab5_time_sync_message(sync.code==TAB5_TIME_SYNC_OK?TAB5_TIME_SYNC_TIMEOUT:sync.code));
            snprintf(notice,sizeof(notice),"%s",time_sync_status);
        }
        consume_rejected_actions();
        uint64_t now=esp_timer_get_time();resources_refresh(now);if(now-last_battery>=1000000){battery=tab5_board_battery();last_battery=now;
            xSemaphoreTake(route_mutex,portMAX_DELAY);route_battery=battery;xSemaphoreGive(route_mutex);}
        tab5_ui_state_t ui={.recording=recording,.light_theme=light_theme,.brightness_pct=brightness_pct};tab5_measure_snapshot(&ui.measurement);
        ui.timezone_known=true;ui.timezone_minutes=(int16_t)tab5_time_offset();ui.time_sync_busy=tab5_time_sync_busy();
        snprintf(ui.time_sync_status,sizeof(ui.time_sync_status),"%s",time_sync_status);
        ui.resources_known=resources.known;ui.cpu_known=resources.cpu_known;ui.cpu_load_pct=resources.cpu;
        ui.ram_used_bytes=resources.ram_used;ui.ram_total_bytes=resources.ram_total;
        ui.psram_used_bytes=resources.psram_used;ui.psram_total_bytes=resources.psram_total;
        ui.action_result_known=ui_result_serial!=0;ui.action_result_serial=ui_result_serial;
        ui.action_result_kind=ui_result_kind;ui.action_result_success=ui_result_success;
        ui.now_known=true;ui.now_us=now;ui.rec_known=true;ui.rec_started_us=record_start_us;
        snprintf(ui.rec_file,sizeof(ui.rec_file),"%s",record_name);
        ui.rec_duration_known=record_name[0]!=0;
        ui.rec_duration_s=recording?(uint32_t)((now-record_start_us)/1000000):record_duration_s;
        char clock_text[64]="Установите часы";time_t wall=time(NULL);struct tm wall_tm;tab5_time_local_now(wall,&wall_tm);
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
        ui.sd=(tab5_ui_medium_t){.known=sd.known,.mounted=sd.mounted,.recording=sd.recording,.failed=sd.failed,.written=sd.written};
        ui.usb=(tab5_ui_medium_t){.known=flash.known,.mounted=flash.mounted,.recording=flash.recording,.failed=flash.failed,.written=flash.written};
        ui.capacity[0].details_known=sd.known&&sd.mounted;ui.capacity[1].details_known=flash.known&&flash.mounted;
        for(unsigned i=0;i<2;i++){
            const tab5_medium_status_t *m=i?&flash:&sd;
            ui.capacity[i].known=m->mounted&&m->capacity_known&&m->total_bytes&&m->free_bytes<=m->total_bytes;
            if(ui.capacity[i].known){ui.capacity[i].total_bytes=m->total_bytes;ui.capacity[i].free_bytes=m->free_bytes;}
        }
        for(unsigned i=0;i<2;i++)if(ui.capacity[i].details_known){
            snprintf(ui.capacity[i].filesystem,sizeof(ui.capacity[i].filesystem),"%s",filesystem_name(i?flash.fs_type:sd.fs_type));
            snprintf(ui.capacity[i].folder,sizeof(ui.capacity[i].folder),"/");}
        snprintf(ui.status,sizeof(ui.status),"%s",notice);
        tab5_network_snapshot_t net;tab5_services_network_snapshot(&net);snprintf(ui.network_status,sizeof(ui.network_status),"AP %s  STA %s | TCP %u | %s",net.ap_started?net.ap_ip:"off",net.sta_connected?net.sta_ip:"off",network.tcp_port,esp_err_to_name(net.last_error));
        ui.link=(tab5_ui_link_t){.known=net.known,.ap_started=net.ap_started,.sta_connected=net.sta_connected,.tcp_port=network.tcp_port,.error=net.last_error};
        ui.tcp_clients_known=net.known;ui.tcp_clients=(uint16_t)net.clients;
        snprintf(ui.link.ap_ip,sizeof(ui.link.ap_ip),"%s",net.ap_ip);snprintf(ui.link.sta_ip,sizeof(ui.link.sta_ip),"%s",net.sta_ip);
        xSemaphoreGive(app_mutex);
        if(now-last_ui>=100000){last_ui=now;if(tab5_native_port_lock(20)){
            /* Service/battery calls and the display lock can take time. Take the
             * measurement first, then its time bound; never reject a new reply
             * as "from the future" against the old loop-start timestamp. */
            tab5_measure_snapshot(&ui.measurement);
            ui.now_us=esp_timer_get_time();ui.live_unfiltered=true;
            portENTER_CRITICAL(&ui_perf_lock);
            for(unsigned i=0;i<2;i++)if(ui.measurement.sensor[i].valid&&ui.measurement.sensor[i].timestamp_us<=ui.now_us){
                uint64_t age=ui.now_us-ui.measurement.sensor[i].timestamp_us;
                ui_perf.sample_age_us[i]=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;
                if(ui_perf.sample_age_us[i]>ui_perf.sample_age_max_us[i])ui_perf.sample_age_max_us[i]=ui_perf.sample_age_us[i];
            }
            portEXIT_CRITICAL(&ui_perf_lock);
            uint64_t update_start=esp_timer_get_time();tab5_ui_update(&ui);
            uint64_t update_elapsed=esp_timer_get_time()-update_start;portENTER_CRITICAL(&ui_perf_lock);ui_perf.updates++;ui_perf.update_us+=update_elapsed;portEXIT_CRITICAL(&ui_perf_lock);tab5_native_port_unlock();
            if(!trial_confirmed && now>=2000000 && esp_get_free_heap_size()>16384){
                esp_err_t health=tab5_services_confirm_boot();
                trial_confirmed=health==ESP_OK || health==ESP_ERR_NOT_FOUND;
            }}else {portENTER_CRITICAL(&ui_perf_lock);ui_perf.lock_misses++;portEXIT_CRITICAL(&ui_perf_lock);}}
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
