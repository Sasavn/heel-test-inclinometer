/* Real LVGL software display, no OS window, SDL, device or private UI hooks.
 * Finds controls through public object tree and drives a synthetic touch input.
 * Captures actual flush pixels and audits rendered object coordinates. */
#include "tab5_ui.h"
/* Pinned LVGL9.3 diagnostic geometry; production UI uses only public APIs. */
#include "src/widgets/buttonmatrix/lv_buttonmatrix_private.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    for(uint32_t i=0;i<lv_obj_get_child_count(o);i++){lv_obj_t *r=find_label(lv_obj_get_child(o,i),text);if(r)return r;}
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
static bool contains(lv_area_t a,lv_area_t b){return b.x1>=a.x1 && b.y1>=a.y1 && b.x2<=a.x2 && b.y2<=a.y2;}
static bool intersect(lv_area_t a,lv_area_t b){return a.x1<=b.x2 && a.x2>=b.x1 && a.y1<=b.y2 && a.y2>=b.y1;}
static void audit(lv_obj_t *o,lv_area_t clip,unsigned depth,lv_area_t *overlay){
    if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN))return;
    lv_area_t a;lv_obj_get_coords(o,&a);
    if(!intersect(a,clip))return;
    bool covered=overlay && contains(*overlay,a) && !lv_obj_check_type(o,&lv_keyboard_class);
    bool is_label=lv_obj_check_type(o,&lv_label_class);
    bool control=lv_obj_check_type(o,&lv_button_class)||lv_obj_check_type(o,&lv_switch_class)||lv_obj_check_type(o,&lv_textarea_class)||lv_obj_check_type(o,&lv_keyboard_class);
    if((is_label || control) && !covered){
        const char *text=is_label?lv_label_get_text(o):control?"[touch]":"";
        /* Textareas intentionally scroll their inner label horizontally. */
        bool scroll_text=is_label && lv_obj_check_type(lv_obj_get_parent(o),&lv_textarea_class);
        bool fits=contains(clip,a) || scroll_text;
        bool overlay_cut=overlay && intersect(a,*overlay) && !contains(*overlay,a) && !lv_obj_check_type(o,&lv_keyboard_class);
        fprintf(report,"  %s (%d,%d)-(%d,%d) %s%s %.*s\n",control?"CONTROL":"LABEL",a.x1,a.y1,a.x2,a.y2,fits?"IN":"CLIPPED",overlay_cut?" OVERLAY-CUT":"",100,text);
        if(!fits || overlay_cut){failures++;}
        if(control && (lv_area_get_width(&a)<40 || lv_area_get_height(&a)<40)){fprintf(report,"  FAIL touch target smaller than 40px\n");failures++;}
    }
    lv_area_t next={a.x1>clip.x1?a.x1:clip.x1,a.y1>clip.y1?a.y1:clip.y1,a.x2<clip.x2?a.x2:clip.x2,a.y2<clip.y2?a.y2:clip.y2};
    /* Keyboard descendants are on top of the overlay, not behind it. */
    if(lv_obj_check_type(o,&lv_keyboard_class)){
        overlay=NULL;lv_buttonmatrix_t *m=(lv_buttonmatrix_t*)o;
        for(unsigned i=0;i<m->btn_cnt;i++){
            lv_area_t b=m->button_areas[i];b.x1+=a.x1;b.x2+=a.x1;b.y1+=a.y1;b.y2+=a.y1;
            bool fits=contains(a,b) && lv_area_get_width(&b)>=40 && lv_area_get_height(&b)>=40;
            fprintf(report,"  KEY (%d,%d)-(%d,%d) %s %s\n",b.x1,b.y1,b.x2,b.y2,fits?"IN":"FAIL",lv_buttonmatrix_get_button_text(o,i));
            if(!fits)failures++;
        }
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(o);i++)audit(lv_obj_get_child(o,i),next,depth+1,overlay);
}
static void capture(const char *name){
    pump();char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",output,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1280 720\n255\n");
    for(unsigned i=0;i<1280*720;i++){uint8_t rgb[3]={(uint8_t)(pixels[i]>>16),(uint8_t)(pixels[i]>>8),(uint8_t)pixels[i]};fwrite(rgb,1,3,f);}fclose(f);
    fprintf(report,"\nCASE %s\n",name);lv_area_t screen={0,0,1279,719},ka,*overlay=NULL;lv_obj_t *k=keyboard(lv_screen_active());if(k && !lv_obj_has_flag(k,LV_OBJ_FLAG_HIDDEN)){lv_obj_get_coords(k,&ka);overlay=&ka;}
    audit(lv_screen_active(),screen,0,overlay);cases++;
}
int main(int argc,char **argv){
    assert(argc==2);output=argv[1];char path[1024];snprintf(path,sizeof(path),"%s/geometry.txt",output);report=fopen(path,"w");assert(report);
    lv_init();display=lv_display_create(1280,720);lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);lv_display_set_flush_cb(display,flush);lv_display_set_buffers(display,drawbuf,NULL,sizeof(drawbuf),LV_DISPLAY_RENDER_MODE_PARTIAL);
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(touch,read_touch);
    tab5_ui_init(action,NULL);tab5_ui_state_t s={0};tab5_measure_config_default(&s.measurement.config);
    snprintf(s.status,sizeof(s.status),"Карта готова / измерение 001");snprintf(s.network_status,sizeof(s.network_status),"AP 192.168.4.1 / STA подключено");
    snprintf(s.clock_power,sizeof(s.clock_power),"21.10.2026 22:00:00 | внешнее питание");
    strcpy(s.network.ap_ssid,"Tab5-Inclinometer");strcpy(s.network.ap_password,"test-pass-123");strcpy(s.network.sta_ssid,"Laboratory");strcpy(s.network.sta_password,"secret-pass-123");s.network.ap_enabled=s.network.sta_enabled=true;
    for(unsigned i=0;i<2;i++){s.measurement.sensor[i]=(tab5_measure_sensor_t){.valid=true,.x=-89.99f,.y=89.99f,.roll_x=1.6f,.roll_y=.25f,.good=123456,.errors=12,.roll_fill_s=20,.calm_y=true,.raw_x=1001,.raw_y=18999};}
    tab5_ui_update(&s);capture("01_measure_dark");click("Старт записи");assert(callbacks==1 && last.kind==TAB5_UI_START);s.recording=true;tab5_ui_update(&s);click("Стоп записи");assert(last.kind==TAB5_UI_STOP);click("Установить ноль");assert(last.kind==TAB5_UI_ZERO);click("Сбросить ноль");assert(last.kind==TAB5_UI_ZERO_RESET);
    s.light_theme=true;tab5_ui_update(&s);capture("02_measure_light");click("Настройки");capture("03_settings");
    lv_obj_t *freq=field("Частота 1–50 Гц");click_obj(freq);capture("04_settings_keyboard");lv_textarea_set_text(freq,"");key_click("2");key_click("0");assert(!strcmp(lv_textarea_get_text(freq),"20"));key_click(LV_SYMBOL_OK);pump();click("Применить настройки");assert(last.kind==TAB5_UI_SETTINGS && last.config.frequency_hz==20);capture("05_settings_applied");
    click("Сеть");capture("06_network");
    click("Показать пароли");assert(!lv_textarea_get_password_mode(field("AP пароль")) && !lv_textarea_get_password_mode(field("STA пароль")));capture("06b_network_revealed");
    click("Скрыть пароли");assert(lv_textarea_get_password_mode(field("AP пароль")) && lv_textarea_get_password_mode(field("STA пароль")));
    click_obj(field("AP SSID"));capture("07_network_ssid_keyboard");
    lv_obj_send_event(keyboard(lv_screen_active()),LV_EVENT_CANCEL,NULL);pump();click_obj(field("STA пароль"));key_click("x");capture("08_network_password_keyboard");assert(lv_textarea_get_password_mode(field("STA пароль")));
    key_click(LV_SYMBOL_OK);pump();click("Применить сеть");assert(last.kind==TAB5_UI_NETWORK && !strcmp(last.network.sta_password,"secret-pass-123x"));capture("09_network_applied");
    fprintf(report,"\nRESULT cases=%u callbacks=%u geometry_failures=%u\n",cases,callbacks,failures);fclose(report);printf("headless LVGL: %u cases, %u geometry failures\n",cases,failures);tab5_ui_destroy();lv_display_delete(display);lv_deinit();return failures?1:0;
}
