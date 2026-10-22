/* Real LVGL software display, no OS window, SDL, device or private UI hooks.
 * Finds controls through public object tree and drives a synthetic touch input.
 * Captures actual flush pixels and audits rendered object coordinates. */
#include "tab5_ui.h"
/* Pinned LVGL9.3 diagnostic geometry; production UI uses only public APIs. */
#include "src/widgets/buttonmatrix/lv_buttonmatrix_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
LV_FONT_DECLARE(font_tab5_64);
static uint32_t pixels[1280*720],drawbuf[1280*100];
static lv_display_t *display;
static lv_indev_t *touch;
static lv_point_t point;
static bool pressed;
static unsigned callbacks, failures, cases;
static tab5_ui_action_t last;
static FILE *report;
static const char *output;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *data) {
    uint32_t *src=(uint32_t*)data;
    for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)pixels[y*1280+x]=*src++;
    lv_display_flush_ready(d);
}
static void read_touch(lv_indev_t *dev,lv_indev_data_t *data){(void)dev;data->point=point;data->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;}
static void pump(void){for(unsigned i=0;i<5;i++){lv_tick_inc(40);lv_timer_handler();}lv_obj_update_layout(lv_screen_active());lv_refr_now(display);}
static void action(const tab5_ui_action_t *a,void *u){(void)u;last=*a;callbacks++;}
static lv_obj_t *find_label(lv_obj_t *o,const char *text){
    if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN))return NULL;
    if(lv_obj_check_type(o,&lv_label_class) && !strcmp(lv_label_get_text(o),text))return o;
    for(int32_t i=(int32_t)lv_obj_get_child_count(o)-1;i>=0;i--){lv_obj_t *r=find_label(lv_obj_get_child(o,i),text);if(r)return r;}
    return NULL;
}
static void click_obj(lv_obj_t *o){assert(o);lv_area_t a;lv_obj_get_coords(o,&a);point.x=(a.x1+a.x2)/2;point.y=(a.y1+a.y2)/2;pressed=true;pump();pressed=false;pump();}
static void click(const char *text){lv_obj_t *o=find_label(lv_screen_active(),text);assert(o);click_obj(lv_obj_get_parent(o));}
static lv_obj_t *field(const char *name){lv_obj_t *l=find_label(lv_screen_active(),name);assert(l);lv_obj_t *parent=lv_obj_get_parent(l);for(uint32_t i=0;i<lv_obj_get_child_count(parent);i++)if(lv_obj_get_child(parent,i)==l)return lv_obj_get_child(parent,i+1);abort();}
static lv_obj_t *keyboard(lv_obj_t *o){if(lv_obj_check_type(o,&lv_keyboard_class))return o;for(uint32_t i=0;i<lv_obj_get_child_count(o);i++){lv_obj_t *r=keyboard(lv_obj_get_child(o,i));if(r)return r;}return NULL;}
static void key_click(const char *text){
    lv_obj_t *k=keyboard(lv_screen_active());assert(k && !lv_obj_has_flag(k,LV_OBJ_FLAG_HIDDEN));
    lv_buttonmatrix_t *matrix=(lv_buttonmatrix_t*)k;lv_area_t a;lv_obj_get_coords(k,&a);
    for(unsigned i=0;i<matrix->btn_cnt;i++)if(!strcmp(lv_buttonmatrix_get_button_text(k,i),text)){
        lv_area_t b=matrix->button_areas[i];point.x=a.x1+(b.x1+b.x2)/2;point.y=a.y1+(b.y1+b.y2)/2;
        pressed=true;pump();pressed=false;pump();return;
    }
    fprintf(stderr,"keyboard key not found: %s\n",text);abort();
}

static bool contains(lv_area_t a,lv_area_t b){return b.x1>=a.x1&&b.y1>=a.y1&&b.x2<=a.x2&&b.y2<=a.y2;}
static void audit(lv_obj_t *o,bool covered){
    if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN))return;
    lv_area_t a;lv_obj_get_coords(o,&a);lv_area_t screen={0,0,1279,719};
    bool control=lv_obj_check_type(o,&lv_button_class)||lv_obj_check_type(o,&lv_switch_class)||lv_obj_check_type(o,&lv_textarea_class)||lv_obj_check_type(o,&lv_keyboard_class);
    if(lv_obj_check_type(o,&lv_label_class)&&lv_obj_get_style_text_font(o,0)==&font_tab5_64){
        lv_point_t size;lv_text_get_size(&size,lv_label_get_text(o),&font_tab5_64,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        if(size.x>lv_obj_get_width(o)){fprintf(report,"FAIL angle glyph advance %d > %d\n",size.x,lv_obj_get_width(o));failures++;}
    }
    if(control&&!covered){
        if(!contains(screen,a)){fprintf(report,"FAIL outside screen (%d,%d)-(%d,%d)\n",a.x1,a.y1,a.x2,a.y2);failures++;}
        int ext=lv_obj_check_type(o,&lv_switch_class)?8:0;
        if(lv_area_get_width(&a)+2*ext<56||lv_area_get_height(&a)+2*ext<56){fprintf(report,"FAIL target <56 (%d,%d)-(%d,%d)\n",a.x1,a.y1,a.x2,a.y2);failures++;}
    }
    if(lv_obj_check_type(o,&lv_keyboard_class)){
        lv_buttonmatrix_t *m=(lv_buttonmatrix_t*)o;
        for(unsigned i=0;i<m->btn_cnt;i++)if(lv_area_get_width(&m->button_areas[i])<56||lv_area_get_height(&m->button_areas[i])<56){fprintf(report,"FAIL keyboard key <56: %s\n",lv_buttonmatrix_get_button_text(o,i));failures++;}
    }
    for(unsigned i=0;i<lv_obj_get_child_count(o);i++)audit(lv_obj_get_child(o,i),covered);
}
/* Explicit independent source coordinates: SPEC §1, §5 and reference PNG.
 * Search actual public object tree; no production layout constants imported. */
static bool rect_exists(lv_obj_t *o,int x,int y,int w,int h){
    if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN))return false;lv_area_t a;lv_obj_get_coords(o,&a);
    if(abs(a.x1-x)<=4&&abs(a.y1-y)<=4&&abs(lv_area_get_width(&a)-w)<=4&&abs(lv_area_get_height(&a)-h)<=4)return true;
    for(unsigned i=0;i<lv_obj_get_child_count(o);i++)if(rect_exists(lv_obj_get_child(o,i),x,y,w,h))return true;return false;
}
static void expect_rect(int x,int y,int w,int h){if(!rect_exists(lv_screen_active(),x,y,w,h)){fprintf(report,"FAIL SPEC rect %d,%d %dx%d\n",x,y,w,h);failures++;}}
static void geometry(const char *name){
    expect_rect(0,0,104,720);for(unsigned i=0;i<5;i++)expect_rect(0,88+i*88,104,88);
    if(strstr(name,"dashboard")){
        expect_rect(120,16,1144,128);expect_rect(120,160,564,376);expect_rect(700,160,564,376);
        expect_rect(120,552,336,152);expect_rect(472,552,272,152);expect_rect(760,552,256,152);expect_rect(1032,552,232,152);
        expect_rect(464,218,200,200);expect_rect(1044,218,200,200);expect_rect(1000,40,244,80);
    }
    if(strstr(name,"chart")){expect_rect(120,104,1144,440);for(int i=0;i<4;i++)expect_rect(120+i*290,560,274,144);}
    if(!strncmp(name,"recording_",10)){expect_rect(120,104,564,236);expect_rect(700,104,564,236);expect_rect(120,356,1144,348);}
    if(strstr(name,"settings")||strstr(name,"numeric")){expect_rect(120,104,564,600);expect_rect(700,104,564,268);expect_rect(700,388,564,316);}
    if(strstr(name,"numeric"))expect_rect(340,52,600,616);
    if(!strncmp(name,"network",7)&&!strstr(name,"keyboard")){expect_rect(120,104,564,380);expect_rect(700,104,564,380);expect_rect(120,500,1144,204);}
}
static void capture(const char *name){
    pump();char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",output,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1280 720\n255\n");
    for(unsigned i=0;i<1280*720;i++){uint8_t rgb[3]={(uint8_t)(pixels[i]>>16),(uint8_t)(pixels[i]>>8),(uint8_t)pixels[i]};fwrite(rgb,1,3,f);}fclose(f);
    fprintf(report,"CASE %s\n",name);geometry(name);audit(lv_screen_active(),false);cases++;
}
static void tap(int x,int y){point.x=x;point.y=y;pressed=true;pump();pressed=false;pump();}
static void nav(unsigned page){tap(52,132+page*88);}
static lv_obj_t *nth_textarea(lv_obj_t *o,unsigned *index){if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN))return NULL;if(lv_obj_check_type(o,&lv_textarea_class)){if(!(*index)--)return o;}for(unsigned i=0;i<lv_obj_get_child_count(o);i++){lv_obj_t *r=nth_textarea(lv_obj_get_child(o,i),index);if(r)return r;}return NULL;}
static lv_obj_t *textarea(unsigned n){return nth_textarea(lv_screen_active(),&n);}
static void fresh(tab5_ui_state_t *s){s->now_us+=100000;for(unsigned i=0;i<2;i++)if(s->measurement.sensor[i].valid)s->measurement.sensor[i].timestamp_us=s->now_us;tab5_ui_update(s);pump();}
int main(int argc,char **argv){
    assert(argc==2);output=argv[1];setbuf(stdout,NULL);setbuf(stderr,NULL);char path[1024];snprintf(path,sizeof(path),"%s/geometry.txt",output);report=fopen(path,"w");assert(report);setbuf(report,NULL);
    lv_init();display=lv_display_create(1280,720);lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);lv_display_set_flush_cb(display,flush);lv_display_set_buffers(display,drawbuf,NULL,sizeof(drawbuf),LV_DISPLAY_RENDER_MODE_PARTIAL);
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(touch,read_touch);
    for(unsigned theme=0;theme<2;theme++){
        tab5_ui_init(action,NULL);tab5_ui_state_t s={0};tab5_measure_config_default(&s.measurement.config);s.measurement.config.frequency_hz=20;s.measurement.config.gap_ms=20;s.measurement.config.alpha=.2;s.measurement.config.roll_calm_deg=.5;s.measurement.config.roll_rate_hz=2;s.measurement.config.roll_hysteresis_deg=.1;
        s.light_theme=theme;s.now_known=true;s.now_us=700000000;s.datetime_known=true;s.datetime=(tab5_ui_datetime_t){2026,10,22,14,32,5};s.clock=(tab5_ui_clock_t){.known=true,.time_valid=true};strcpy(s.clock.datetime,"22.10.2026 · 14:32:05");strcpy(s.panel,"ST7123 · определён автоматически");strcpy(s.firmware,"1.6.0-esp32");
        s.sd=(tab5_ui_medium_t){.known=true,.mounted=true};s.usb=(tab5_ui_medium_t){.known=true};s.tcp_clients_known=true;s.tcp_clients=1;
        s.link=(tab5_ui_link_t){.known=true,.ap_started=true,.sta_connected=true,.tcp_port=5000};strcpy(s.link.ap_ip,"192.168.4.1");strcpy(s.link.sta_ip,"10.20.4.117");
        s.network=(tab5_ui_network_t){.ap_enabled=true,.sta_enabled=true};strcpy(s.network.ap_ssid,"KRENOMER-7F3A");strcpy(s.network.sta_ssid,"SAFU-Lab");strcpy(s.network.ap_password,"test-pass-123");strcpy(s.network.sta_password,"secret-pass-123");
        for(unsigned i=0;i<2;i++)s.measurement.sensor[i]=(tab5_measure_sensor_t){.valid=true,.x=i?-.39f:-.42f,.y=i?1.41f:1.37f,.roll_x=i?.12f:.14f,.roll_y=i?.06f:.05f,.calm_x=true,.calm_y=true,.good=1,.timestamp_us=s.now_us,.roll_fill_s=20};s.measurement.stable=true;
        fresh(&s);char name[120];const char *tag=theme?"light":"dark";
        #define SHOT(stage) do{snprintf(name,sizeof(name),"%s_%s",stage,tag);capture(name);}while(0)
        SHOT("dashboard");assert(find_label(lv_screen_active(),"Ноль датчиков"));assert(find_label(lv_screen_active(),"14:32"));
        nav(1);
        for(unsigned i=0;i<250;i++){
            s.now_us+=500000;
            for(unsigned j=0;j<2;j++){float t=i*.5f;s.measurement.sensor[j].valid=!(i>205&&i<215);s.measurement.sensor[j].timestamp_us=s.now_us;s.measurement.sensor[j].good++;s.measurement.sensor[j].x=.2f+1.95f/(1+expf(-(t-62)/3))+.38f*sinf(t*.9f+j*.1f)*fminf(1,fmaxf(0,(t-55)/10));s.measurement.sensor[j].y=-.12f+.1f*sinf(t*.9f+j)*fminf(1,fmaxf(0,(t-55)/10));}
            tab5_ui_update(&s);
        }
        SHOT("chart");
        for(unsigned j=0;j<2;j++){s.measurement.sensor[j].x=j?-.39f:-.42f;s.measurement.sensor[j].y=j?1.41f:1.37f;}fresh(&s);nav(2);SHOT("recording");assert(find_label(lv_screen_active(),"— / данных истории нет"));nav(3);SHOT("settings");nav(4);SHOT("network");
        printf("Five native pages ready: %s\n",tag);
        nav(3);unsigned hold_before=callbacks;point=(lv_point_t){635,200};pressed=true;for(unsigned hold=0;hold<5;hold++)pump();pressed=false;pump();assert(callbacks==hold_before);tap(1140,52);assert(last.kind==TAB5_UI_SETTINGS&&last.config.frequency_hz>21);s.measurement.config=last.config;fresh(&s);SHOT("settings_longpress");
        click(theme?"Тёмная":"Светлая");assert(last.kind==TAB5_UI_SETTINGS&&last.light_theme!=s.light_theme);s.light_theme=last.light_theme;fresh(&s);click(theme?"Светлая":"Тёмная");s.light_theme=last.light_theme;fresh(&s);
        nav(0);unsigned before=callbacks;tap(1120,80);assert(callbacks==before+1&&last.kind==TAB5_UI_START);s.recording=true;s.rec_known=true;strcpy(s.rec_file,"2026-10-22_M001");s.rec_started_us=s.now_us-192000000;s.rec_duration_known=true;s.rec_duration_s=192;s.sd.recording=s.usb.recording=true;s.usb.mounted=true;s.sd.written=s.usb.written=3812;
        s.measurement.stable=false;s.measurement.sensor[0].x=2.18f;s.measurement.sensor[0].y=-.07f;s.measurement.sensor[0].roll_x=.86f;s.measurement.sensor[0].roll_y=.21f;s.measurement.sensor[0].calm_x=false;s.measurement.sensor[1].valid=false;s.measurement.sensor[1].fail_streak=3;s.measurement.sensor[1].errors=27;s.measurement.sensor[1].last_error=TAB5_MEASURE_TIMEOUT;fresh(&s);SHOT("dashboard_recording_error");
        before=callbacks;tap(220,655);assert(callbacks==before);SHOT("zero_confirm");click("Подтвердить");assert(last.kind==TAB5_UI_ZERO);fresh(&s);assert(find_label(lv_screen_active(),"не задан"));s.measurement.sensor[0].zero_x=2.18f;fresh(&s);assert(find_label(lv_screen_active(),"задан 14:32"));
        before=callbacks;tap(390,655);assert(callbacks==before);click("Отмена");assert(callbacks==before);tap(390,655);click("Подтвердить");assert(last.kind==TAB5_UI_ZERO_RESET);
        tap(1120,80);assert(last.kind==TAB5_UI_STOP);s.recording=false;s.sd.recording=s.usb.recording=false;fresh(&s);
        nav(3);tap(635,200);SHOT("settings_dirty");tap(535,200);click("C");click("7");click("5");SHOT("numeric");before=callbacks;click("Готово");assert(callbacks==before);click("C");click("2");click("0");SHOT("numeric_valid");click("Готово");
        /* Change another setting, reject navigation, then discard explicitly. */
        tap(635,276);nav(4);SHOT("dirty_navigation");click("Остаться");nav(4);click("Отбросить");
        nav(3);tap(635,200);tap(1140,52);assert(last.kind==TAB5_UI_SETTINGS);tab5_measure_config_t sent=last.config;fresh(&s);SHOT("settings_pending");s.measurement.config=sent;fresh(&s);SHOT("settings_ack");
        /* Invalid Gregorian dates do not produce a TIME action. */
        click("Изменить");lv_obj_t *date[6];for(unsigned i=0;i<6;i++){date[i]=textarea(i);assert(date[i]);}
        lv_textarea_set_text(date[0],"2025");lv_textarea_set_text(date[1],"2");lv_textarea_set_text(date[2],"29");before=callbacks;SHOT("time_invalid");click("Готово");assert(callbacks==before);
        lv_textarea_set_text(date[2],"31");click("Готово");assert(callbacks==before);lv_textarea_set_text(date[2],"29");lv_textarea_set_text(date[0],"2024");lv_textarea_set_text(date[3],"24");click("Готово");assert(callbacks==before);lv_textarea_set_text(date[3],"23");SHOT("time_valid");click("Готово");assert(last.kind==TAB5_UI_TIME&&last.datetime.year==2024&&last.datetime.day==29);
        nav(4);tap(400,204);if(find_label(lv_screen_active(),"РУС → EN"))click("РУС → EN");key_click("q");SHOT("network_keyboard_en");click("EN → РУС");key_click("я");SHOT("network_keyboard_ru");click("Готово");tap(1150,52);assert(last.kind==TAB5_UI_NETWORK);tab5_ui_network_t net_sent=last.network;fresh(&s);SHOT("network_pending");s.network=net_sent;fresh(&s);SHOT("network_ack");tap(400,272);lv_textarea_set_text(textarea(0),"short");click("Готово");unsigned rejected=callbacks;tap(1150,52);assert(callbacks==rejected);SHOT("network_invalid");tap(400,272);lv_textarea_set_text(textarea(0),s.network.ap_password);click("Готово");click("Показать пароли");SHOT("network_revealed");click("Скрыть пароли");
        nav(0);s.measurement.sensor[1].valid=true;for(unsigned i=0;i<2;i++){s.measurement.sensor[i].x=i?89.99f:-89.99f;s.measurement.sensor[i].y=i?-89.99f:89.99f;s.measurement.sensor[i].good++;}strcpy(s.link.ap_ip,"255.255.255.255");s.sd.written=s.usb.written=999999;s.sd.recording=true;s.measurement.sensor[0].roll_x=1.6;s.measurement.sensor[0].roll_y=.25;fresh(&s);SHOT("dashboard_extremes");assert(find_label(lv_screen_active(),"−89.99°"));assert(find_label(lv_screen_active(),"+89.99°"));assert(find_label(lv_screen_active(),"−179.98°"));assert(find_label(lv_screen_active(),"+179.98°"));
        nav(4);tap(400,204);char ssid[33];memset(ssid,'W',32);ssid[32]=0;lv_textarea_set_text(textarea(0),ssid);SHOT("network_keyboard_long");click("Готово");SHOT("network_long");nav(1);click("Отбросить");
        /* Only injected timestamped measurement samples populate chart. */
        for(unsigned i=0;i<1250;i++){s.now_us+=500000;for(unsigned j=0;j<2;j++){s.measurement.sensor[j].valid=!(i>1190&&i<1200);s.measurement.sensor[j].timestamp_us=s.now_us;s.measurement.sensor[j].good++;s.measurement.sensor[j].x=.2f+2.f*(float)i/1250+.35f*sinf(i*.5f+j*.1f);s.measurement.sensor[j].y=-.12f+.1f*sinf(i*.5f+j);}tab5_ui_update(&s);}
        SHOT("chart_samples_fixture");for(unsigned i=0;i<350;i++){s.now_us+=100000;for(unsigned j=0;j<2;j++){s.measurement.sensor[j].timestamp_us=s.now_us;s.measurement.sensor[j].x=2+.3f*sinf(i*.1f+j*.1f);s.measurement.sensor[j].y=.1f*sinf(i*.1f+j);}tab5_ui_update(&s);}click("30 с");SHOT("chart_30");click("10 мин");SHOT("chart_600");tap(668,52);assert(find_label(lv_screen_active(),"скрыт"));tap(800,300);SHOT("chart_touch_hidden");
        nav(0);s.measurement.sensor[0].valid=s.measurement.sensor[1].valid=false;fresh(&s);SHOT("dashboard_disconnected");
        s.action_result_known=true;s.action_result_serial=1;s.action_result_kind=TAB5_UI_START;s.action_result_success=false;
        strcpy(s.status,"Запись недоступна: ESP_FAIL");s.recording=false;fresh(&s);assert(find_label(lv_screen_active(),s.status));SHOT("backend_record_error");
        click("✗");fresh(&s);assert(!find_label(lv_screen_active(),s.status));
        s.action_result_serial++;fresh(&s);assert(find_label(lv_screen_active(),s.status));SHOT("backend_repeated_error");
        s.action_result_serial++;s.action_result_kind=TAB5_UI_TIME;strcpy(s.status,"Ошибка установки RTC: ESP_FAIL");fresh(&s);assert(find_label(lv_screen_active(),s.status));SHOT("backend_time_error");
        click("✗");nav(4);tap(400,204);lv_textarea_set_text(textarea(0),"BackendRetry");click("Готово");tap(1150,52);assert(last.kind==TAB5_UI_NETWORK);tab5_ui_network_t first_attempt=last.network;
        /* Configure failed: runtime state is unchanged, draft must remain retryable. */
        s.action_result_serial++;s.action_result_kind=TAB5_UI_NETWORK;s.action_result_success=false;
        strcpy(s.status,"Ошибка сохранения сети: ESP_FAIL");fresh(&s);assert(find_label(lv_screen_active(),s.status));lv_obj_t *net_apply=lv_obj_get_parent(find_label(lv_screen_active(),"Применить"));assert(!lv_obj_has_state(net_apply,LV_STATE_DISABLED));SHOT("backend_network_error");
        unsigned retry_before=callbacks;tap(1150,52);assert(callbacks==retry_before+1&&!strcmp(last.network.ap_ssid,first_attempt.ap_ssid));
        /* Runtime configured, but NVS failed: equal state must NOT erase failure. */
        s.network=last.network;s.action_result_serial++;s.action_result_success=false;strcpy(s.status,"Сеть применена, ошибка NVS: ESP_FAIL");fresh(&s);assert(!lv_obj_has_state(net_apply,LV_STATE_DISABLED));SHOT("backend_network_nvs_error");
        fresh(&s);assert(!lv_obj_has_state(net_apply,LV_STATE_DISABLED));
        retry_before=callbacks;tap(1150,52);assert(callbacks==retry_before+1);fresh(&s);assert(lv_obj_has_state(net_apply,LV_STATE_DISABLED));
        s.action_result_serial++;s.action_result_success=true;strcpy(s.status,"Настройки сети сохранены");fresh(&s);assert(lv_obj_has_state(net_apply,LV_STATE_DISABLED));SHOT("backend_network_retry_success");
        click("✗");nav(3);tap(635,200);tap(1140,52);assert(last.kind==TAB5_UI_SETTINGS);s.measurement.config=last.config;
        s.action_result_serial++;s.action_result_kind=TAB5_UI_SETTINGS;s.action_result_success=false;strcpy(s.status,"Ошибка сохранения настроек NVS: ESP_FAIL");fresh(&s);assert(find_label(lv_screen_active(),s.status));lv_obj_t *set_apply=lv_obj_get_parent(find_label(lv_screen_active(),"Применить"));assert(!lv_obj_has_state(set_apply,LV_STATE_DISABLED));SHOT("backend_nvs_error");
        retry_before=callbacks;tap(1140,52);assert(callbacks==retry_before+1);fresh(&s);assert(lv_obj_has_state(set_apply,LV_STATE_DISABLED));
        s.action_result_serial++;s.action_result_success=true;strcpy(s.status,"Настройки сохранены");fresh(&s);assert(lv_obj_has_state(set_apply,LV_STATE_DISABLED));SHOT("backend_settings_retry_success");
        tab5_ui_destroy();
        #undef SHOT
    }
    fprintf(report,"RESULT cases=%u callbacks=%u geometry_failures=%u\n",cases,callbacks,failures);fclose(report);printf("headless LVGL: %u cases, %u geometry failures\n",cases,failures);lv_display_delete(display);lv_deinit();return failures?1:0;
}
