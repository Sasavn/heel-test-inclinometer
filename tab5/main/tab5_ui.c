#include "tab5_ui.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
LV_FONT_DECLARE(font_tab5_20);
LV_FONT_DECLARE(font_tab5_28);
LV_FONT_DECLARE(font_tab5_64);
static lv_font_t keyboard_font;
static struct {
    lv_obj_t *root,*pages[3],*card[2],*angle[2][2],*health[2],*roll[2];
    lv_obj_t *status,*net_status,*clock_power,*start,*keyboard;
    lv_obj_t *setting[7],*theme,*ap,*sta,*net[4],*reveal;
    tab5_ui_action_cb_t cb; void *user;
    tab5_ui_state_t state;
    bool settings_dirty,network_dirty;
} ui;
static lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,int w,const lv_font_t *font) {
    lv_obj_t *o=lv_label_create(parent);lv_label_set_text(o,text);lv_obj_set_pos(o,x,y);
    lv_obj_set_width(o,w);lv_obj_set_style_text_font(o,font,0);return o;
}
static void dispatch(tab5_ui_action_kind_t kind) {
    tab5_ui_action_t action={.kind=kind,.config=ui.state.measurement.config,
        .light_theme=ui.state.light_theme,.network=ui.state.network};
    if(ui.cb)ui.cb(&action,ui.user);
}
static void button_event(lv_event_t *e) {
    intptr_t value=(intptr_t)lv_event_get_user_data(e);
    if(value>=100) {
        for(unsigned i=0;i<3;i++) {
            if(i==(unsigned)(value-100))lv_obj_remove_flag(ui.pages[i],LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(ui.pages[i],LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(ui.keyboard,LV_OBJ_FLAG_HIDDEN);return;
    }
    if(value==TAB5_UI_START)dispatch(ui.state.recording?TAB5_UI_STOP:TAB5_UI_START);
    else dispatch((tab5_ui_action_kind_t)value);
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int w,int h,intptr_t value) {
    lv_obj_t *o=lv_button_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_t *l=lv_label_create(o);lv_label_set_text(l,text);lv_obj_set_style_text_font(l,&font_tab5_28,0);lv_obj_center(l);
    lv_obj_add_event_cb(o,button_event,LV_EVENT_CLICKED,(void*)value);return o;
}
static void edited(lv_event_t *e) {
    if((intptr_t)lv_event_get_user_data(e))ui.network_dirty=true;else ui.settings_dirty=true;
}
static void focus(lv_event_t *e) {
    lv_obj_t *target=lv_event_get_target(e);
    lv_keyboard_set_textarea(ui.keyboard,target);
    lv_keyboard_set_mode(ui.keyboard,lv_textarea_get_accepted_chars(target)?LV_KEYBOARD_MODE_NUMBER:LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_remove_flag(ui.keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(ui.keyboard);
}
static void keyboard_event(lv_event_t *e) {
    (void)e;lv_keyboard_set_textarea(ui.keyboard,NULL);lv_obj_add_flag(ui.keyboard,LV_OBJ_FLAG_HIDDEN);
}
static lv_obj_t *field(lv_obj_t *parent,const char *name,int x,int y,int w,bool numeric,bool network) {
    label(parent,name,x,y,w,&font_tab5_28);
    lv_obj_t *o=lv_textarea_create(parent);lv_obj_set_pos(o,x,y+36);lv_obj_set_size(o,w,56);
    lv_textarea_set_one_line(o,true);lv_obj_set_style_text_font(o,&font_tab5_28,0);
    lv_obj_set_style_pad_top(o,8,0);lv_obj_set_style_pad_bottom(o,8,0);lv_obj_set_style_pad_left(o,12,0);
    lv_textarea_set_max_length(o,numeric?8:64);
    if(numeric)lv_textarea_set_accepted_chars(o,"0123456789.");
    lv_obj_add_event_cb(o,focus,LV_EVENT_FOCUSED,NULL);
    lv_obj_add_event_cb(o,edited,LV_EVENT_VALUE_CHANGED,(void*)(intptr_t)network);return o;
}
static void apply_settings(lv_event_t *e) {
    (void)e;tab5_ui_action_t a={.kind=TAB5_UI_SETTINGS};
    double values[7];
    for(unsigned i=0;i<7;i++) {
        const char *s=lv_textarea_get_text(ui.setting[i]);char *end;
        values[i]=strtod(s,&end);
        if(!*s || *end || !isfinite(values[i])){lv_label_set_text(ui.status,"Проверьте числовые настройки");return;}
    }
    if(values[0]<1 || values[0]>50 || values[1]<.01 || values[1]>.99 || values[2]<15 || values[2]>100 ||
       values[3]<10 || values[3]>60 || values[4]<2 || values[4]>5 || values[5]<.1 || values[5]>10 || values[6]<0 || values[6]>2) {
        lv_label_set_text(ui.status,"Настройки вне допустимого диапазона");return;
    }
    for(unsigned i=0;i<5;i++) {
        if(i!=1 && values[i]!=(unsigned)values[i]) {lv_label_set_text(ui.status,"Частоты, пауза и окно должны быть целыми");return;}
    }
    a.config=(tab5_measure_config_t){.frequency_hz=(uint16_t)values[0],.alpha=(float)values[1],.gap_ms=(uint16_t)values[2],
        .roll_window_s=(uint8_t)values[3],.roll_rate_hz=(uint8_t)values[4],.roll_calm_deg=(float)values[5],.roll_hysteresis_deg=(float)values[6]};
    a.light_theme=lv_obj_has_state(ui.theme,LV_STATE_CHECKED);
    if(ui.cb)ui.cb(&a,ui.user);
    ui.settings_dirty=false;lv_obj_add_flag(ui.keyboard,LV_OBJ_FLAG_HIDDEN);
}
static void copy_text(char *dest,size_t n,lv_obj_t *o) {snprintf(dest,n,"%s",lv_textarea_get_text(o));}
static void apply_network(lv_event_t *e) {
    (void)e;tab5_ui_action_t a={.kind=TAB5_UI_NETWORK};
    a.network.ap_enabled=lv_obj_has_state(ui.ap,LV_STATE_CHECKED);a.network.sta_enabled=lv_obj_has_state(ui.sta,LV_STATE_CHECKED);
    for(unsigned i=0;i<4;i++)if(strlen(lv_textarea_get_text(ui.net[i]))>(i%2?63u:32u)) {
        lv_label_set_text(ui.net_status,"Слишком длинное значение: SSID до 32 байт, пароль до 63 байт");return;
    }
    copy_text(a.network.ap_ssid,sizeof(a.network.ap_ssid),ui.net[0]);copy_text(a.network.ap_password,sizeof(a.network.ap_password),ui.net[1]);
    copy_text(a.network.sta_ssid,sizeof(a.network.sta_ssid),ui.net[2]);copy_text(a.network.sta_password,sizeof(a.network.sta_password),ui.net[3]);
    size_t ap_len=strlen(a.network.ap_password),sta_len=strlen(a.network.sta_password);
    if(ap_len<8 || ap_len>63 || (a.network.ap_enabled && !a.network.ap_ssid[0]) ||
       (a.network.sta_enabled && (!a.network.sta_ssid[0] || (sta_len && sta_len<8) || sta_len>63))) {
        lv_label_set_text(ui.net_status,"Укажите SSID; пароль AP: 8–63 байта, STA: пустой или 8–63 байта");return;
    }
    if(ui.cb)ui.cb(&a,ui.user);
    ui.network_dirty=false;lv_obj_add_flag(ui.keyboard,LV_OBJ_FLAG_HIDDEN);
}
static void reveal_passwords(lv_event_t *e) {
    (void)e;
    bool masked=!lv_textarea_get_password_mode(ui.net[1]);
    lv_textarea_set_password_mode(ui.net[1],masked);
    lv_textarea_set_password_mode(ui.net[3],masked);
    lv_label_set_text(lv_obj_get_child(ui.reveal,0),masked?"Показать пароли":"Скрыть пароли");
}
static void text_number(lv_obj_t *o,float v,unsigned decimals) {
    char b[32];snprintf(b,sizeof(b),"%.*f",(int)decimals,(double)v);
    if(strcmp(lv_textarea_get_text(o),b))lv_textarea_set_text(o,b);
}
static void checked(lv_obj_t *o,bool value) {if(value)lv_obj_add_state(o,LV_STATE_CHECKED);else lv_obj_remove_state(o,LV_STATE_CHECKED);}
void tab5_ui_init(tab5_ui_action_cb_t cb,void *user) {
    if(ui.root)tab5_ui_destroy();
    memset(&ui,0,sizeof(ui));ui.cb=cb;ui.user=user;
    ui.root=lv_obj_create(lv_screen_active());lv_obj_set_size(ui.root,1280,720);lv_obj_set_pos(ui.root,0,0);
    lv_obj_set_style_pad_all(ui.root,0,0);lv_obj_set_style_border_width(ui.root,0,0);lv_obj_set_style_radius(ui.root,0,0);
    lv_obj_remove_flag(ui.root,LV_OBJ_FLAG_SCROLLABLE);
    label(ui.root,"КРЕНОМЕР  /  BWM427",24,2,550,&font_tab5_28);
    ui.clock_power=label(ui.root,"",24,40,550,&font_tab5_20);
    button(ui.root,"Измерения",600,6,205,52,100);button(ui.root,"Настройки",820,6,205,52,101);button(ui.root,"Сеть",1040,6,205,52,102);
    for(unsigned i=0;i<3;i++) {
        ui.pages[i]=lv_obj_create(ui.root);lv_obj_set_pos(ui.pages[i],0,68);lv_obj_set_size(ui.pages[i],1280,580);
        lv_obj_set_style_pad_all(ui.pages[i],0,0);lv_obj_remove_flag(ui.pages[i],LV_OBJ_FLAG_SCROLLABLE);
        if(i)lv_obj_add_flag(ui.pages[i],LV_OBJ_FLAG_HIDDEN);
    }
    for(unsigned i=0;i<2;i++) {
        lv_obj_t *c=ui.card[i]=lv_obj_create(ui.pages[0]);lv_obj_set_pos(c,24+(int)i*628,18);lv_obj_set_size(c,604,410);
        lv_obj_remove_flag(c,LV_OBJ_FLAG_SCROLLABLE);
        char b[48];snprintf(b,sizeof(b),"Датчик %u  /  RS485",i+2);label(c,b,12,8,550,&font_tab5_28);
        label(c,"X",12,72,45,&font_tab5_28);label(c,"Y",12,158,45,&font_tab5_28);
        for(unsigned axis=0;axis<2;axis++) {
            ui.angle[i][axis]=label(c,"—",72,55+(int)axis*86,495,&font_tab5_64);
        }
        ui.roll[i]=label(c,"Качка: накопление окна",12,218,550,&font_tab5_28);
        ui.health[i]=label(c,"Нет данных",12,332,550,&font_tab5_20);
    }
    ui.start=button(ui.pages[0],"Старт записи",24,458,380,84,TAB5_UI_START);
    button(ui.pages[0],"Установить ноль",448,458,380,84,TAB5_UI_ZERO);
    button(ui.pages[0],"Сбросить ноль",872,458,380,84,TAB5_UI_ZERO_RESET);
    const char *names[]={"Частота 1–50 Гц","EMA 0.01–0.99","Пауза 15–100 мс","Окно 10–60 с","Качка 2–5 Гц","Покой 0.1–10°","Гистерезис 0–2°"};
    for(unsigned i=0;i<7;i++)ui.setting[i]=field(ui.pages[1],names[i],24+(int)(i%4)*312,16+(int)(i/4)*110,284,true,false);
    ui.theme=lv_switch_create(ui.pages[1]);lv_obj_set_size(ui.theme,64,42);lv_obj_set_pos(ui.theme,960,160);label(ui.pages[1],"Светлая тема",960,126,284,&font_tab5_28);
    lv_obj_add_event_cb(ui.theme,edited,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_t *apply=button(ui.pages[1],"Применить настройки",24,260,590,66,200);
    lv_obj_remove_event_cb(apply,button_event);lv_obj_add_event_cb(apply,apply_settings,LV_EVENT_CLICKED,NULL);
    label(ui.pages[1],"При нехватке времени шины фактическая частота будет ниже заданной.",24,340,1200,&font_tab5_28);
    ui.ap=lv_switch_create(ui.pages[2]);lv_obj_set_size(ui.ap,64,42);lv_obj_set_pos(ui.ap,24,12);label(ui.pages[2],"Точка доступа AP",100,14,470,&font_tab5_28);
    ui.sta=lv_switch_create(ui.pages[2]);lv_obj_set_size(ui.sta,64,42);lv_obj_set_pos(ui.sta,652,12);label(ui.pages[2],"Подключение STA",728,14,470,&font_tab5_28);
    lv_obj_add_event_cb(ui.ap,edited,LV_EVENT_VALUE_CHANGED,(void*)1);lv_obj_add_event_cb(ui.sta,edited,LV_EVENT_VALUE_CHANGED,(void*)1);
    ui.net[0]=field(ui.pages[2],"AP SSID",24,64,580,false,true);ui.net[1]=field(ui.pages[2],"AP пароль",24,164,580,false,true);
    ui.net[2]=field(ui.pages[2],"STA SSID",652,64,580,false,true);ui.net[3]=field(ui.pages[2],"STA пароль",652,164,580,false,true);
    lv_textarea_set_max_length(ui.net[0],32);lv_textarea_set_max_length(ui.net[2],32);
    lv_textarea_set_max_length(ui.net[1],63);lv_textarea_set_max_length(ui.net[3],63);
    lv_textarea_set_password_mode(ui.net[1],true);lv_textarea_set_password_mode(ui.net[3],true);
    apply=button(ui.pages[2],"Применить сеть",24,260,590,66,200);lv_obj_remove_event_cb(apply,button_event);lv_obj_add_event_cb(apply,apply_network,LV_EVENT_CLICKED,NULL);
    ui.reveal=button(ui.pages[2],"Показать пароли",652,260,580,66,200);
    lv_obj_remove_event_cb(ui.reveal,button_event);lv_obj_add_event_cb(ui.reveal,reveal_passwords,LV_EVENT_CLICKED,NULL);
    ui.net_status=label(ui.pages[2],"",24,350,1208,&font_tab5_28);
    ui.status=label(ui.root,"",24,664,1230,&font_tab5_28);
    lv_obj_set_height(ui.status,40);lv_label_set_long_mode(ui.status,LV_LABEL_LONG_DOT);
    ui.keyboard=lv_keyboard_create(ui.root);
    keyboard_font=font_tab5_28;keyboard_font.fallback=LV_FONT_DEFAULT;
    lv_obj_set_style_text_font(ui.keyboard,&keyboard_font,LV_PART_ITEMS);lv_obj_set_size(ui.keyboard,1280,320);lv_obj_align(ui.keyboard,LV_ALIGN_BOTTOM_MID,0,0);
    lv_obj_add_flag(ui.keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_add_event_cb(ui.keyboard,keyboard_event,LV_EVENT_READY,NULL);lv_obj_add_event_cb(ui.keyboard,keyboard_event,LV_EVENT_CANCEL,NULL);
    tab5_ui_state_t initial={0};tab5_measure_config_default(&initial.measurement.config);tab5_ui_update(&initial);
}
void tab5_ui_update(const tab5_ui_state_t *s) {
    if(!s || !ui.root)return;
    ui.state=*s;
    lv_label_set_text(ui.clock_power,s->clock_power);
    lv_color_t bg=lv_color_hex(s->light_theme?0xf0f3f8:0x101722),fg=lv_color_hex(s->light_theme?0x172333:0xe7efff);
    lv_obj_set_style_bg_color(ui.root,bg,0);lv_obj_set_style_text_color(ui.root,fg,0);
    for(unsigned i=0;i<3;i++){lv_obj_set_style_bg_color(ui.pages[i],bg,0);lv_obj_set_style_text_color(ui.pages[i],fg,0);}
    for(unsigned i=0;i<2;i++) {
        const tab5_measure_sensor_t *m=&s->measurement.sensor[i];char b[256];
        lv_obj_set_style_bg_color(ui.card[i],lv_color_hex(s->light_theme?0xffffff:0x202e40),0);lv_obj_set_style_text_color(ui.card[i],fg,0);
        for(unsigned axis=0;axis<2;axis++) {
            float v=axis?m->y:m->x;
            if(m->valid)snprintf(b,sizeof(b),"%s%.2f°",v<0?"−":"+",(double)fabsf(v));
            else snprintf(b,sizeof(b),"—");
            lv_label_set_text(ui.angle[i][axis],b);
        }
        snprintf(b,sizeof(b),"Качка X %.2f°  %s\nКачка Y %.2f°  %s\nОкно %u / %u с",(double)m->roll_x,m->calm_x?"ПОКОЙ":"—",(double)m->roll_y,m->calm_y?"ПОКОЙ":"—",m->roll_fill_s,s->measurement.config.roll_window_s);lv_label_set_text(ui.roll[i],b);
        snprintf(b,sizeof(b),"%s  |  OK %" PRIu64 "  |  ERR %" PRIu64 "\nRaw %u / %u",m->valid?(m->fail_streak?"Пропуск ответа":"Связь есть"):"Нет свежих данных",m->good,m->errors,m->raw_x,m->raw_y);lv_label_set_text(ui.health[i],b);
    }
    lv_label_set_text(lv_obj_get_child(ui.start,0),s->recording?"Стоп записи":"Старт записи");
    lv_obj_set_style_bg_color(ui.start,lv_color_hex(s->recording?0xb93646:0x167e68),0);
    char status[256];snprintf(status,sizeof(status),"%s  |  %s",s->measurement.stable?"ГОТОВ":"Накопление / качка",s->status);lv_label_set_text(ui.status,status);
    char net_status[256];snprintf(net_status,sizeof(net_status),"Доступ к API: пароль AP.\n%s",s->network_status);lv_label_set_text(ui.net_status,net_status);
    if(!ui.settings_dirty) {
        const tab5_measure_config_t *c=&s->measurement.config;
        text_number(ui.setting[0],c->frequency_hz,0);text_number(ui.setting[1],c->alpha,2);text_number(ui.setting[2],c->gap_ms,0);
        text_number(ui.setting[3],c->roll_window_s,0);text_number(ui.setting[4],c->roll_rate_hz,0);text_number(ui.setting[5],c->roll_calm_deg,2);text_number(ui.setting[6],c->roll_hysteresis_deg,2);
        checked(ui.theme,s->light_theme);ui.settings_dirty=false;
    }
    if(!ui.network_dirty) {
        checked(ui.ap,s->network.ap_enabled);checked(ui.sta,s->network.sta_enabled);
        const char *texts[]={s->network.ap_ssid,s->network.ap_password,s->network.sta_ssid,s->network.sta_password};
        for(unsigned i=0;i<4;i++)if(strcmp(lv_textarea_get_text(ui.net[i]),texts[i]))lv_textarea_set_text(ui.net[i],texts[i]);
        ui.network_dirty=false;
    }
}
void tab5_ui_destroy(void) {if(ui.root)lv_obj_delete(ui.root);memset(&ui,0,sizeof(ui));}
