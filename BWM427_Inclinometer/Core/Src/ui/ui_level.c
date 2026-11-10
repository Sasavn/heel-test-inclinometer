/* Separate angle-meter screen; the historical heel screen is unchanged. */
#include "ui_internal.h"
#include <math.h>
static lv_obj_t *level_scr,*config_scr,*v[2],*roles,*units,*quality,*age,*notice;
static lv_group_t *level_group;
static lv_obj_t *cfg_value[4],*cfg_notice;
static void deleted(lv_event_t *e){
    lv_obj_t *obj=lv_event_get_current_target(e);
    if(obj==level_scr)level_scr=NULL;
    if(obj==config_scr)config_scr=NULL;
}
static void menu_cb(lv_event_t *e){(void)e;ui_menu_show();}
static void zero_cb(lv_event_t *e){(void)e;bool ok=app_relative_zero(false);ui_set_text(notice,ok?"REL ноль задан":"Ноль: нет пары или занято");}
static void reset_cb(lv_event_t *e){(void)e;bool ok=app_relative_zero(true);ui_set_text(notice,ok?"REL ноль сброшен":"Занято: запись или передача");}
static lv_obj_t *button(lv_obj_t *s,lv_group_t *g,int x,int y,int w,const char *text,lv_event_cb_t cb,void *data){
    lv_obj_t *b=ui_button_create(s,g,x,y,w,30);lv_obj_t *l=ui_label_create(b,UI_FONT_SMALL,UI_C_TEXT);
    lv_label_set_text_static(l,text);lv_obj_center(l);lv_obj_add_event_cb(b,cb,LV_EVENT_SHORT_CLICKED,data);return l;
}
void ui_level_show(void){
    level_group=lv_group_create();level_scr=ui_screen_create_temp(level_group);
    lv_obj_add_event_cb(level_scr,deleted,LV_EVENT_DELETE,NULL);
    ui_title_create(level_scr,LV_SYMBOL_LOOP,"Угломер");
    roles=ui_label_create(level_scr,UI_FONT_SMALL,UI_C_TEXT);lv_obj_set_pos(roles,8,28);
    lv_obj_t *caption=ui_label_create(level_scr,UI_FONT_SMALL,UI_C_DIM);
    lv_label_set_text_static(caption,"X: объект - опора       Y: объект - опора");lv_obj_set_pos(caption,8,48);
    for(unsigned i=0;i<2;i++){v[i]=ui_label_create(level_scr,UI_FONT_NUM,UI_C_TEXT);lv_obj_set_pos(v[i],8+i*160,70);}
    units=ui_label_create(level_scr,UI_FONT_SMALL,UI_C_DIM);lv_obj_set_pos(units,8,111);
    quality=ui_label_create(level_scr,UI_FONT_SMALL,UI_C_ORANGE);lv_obj_set_pos(quality,8,133);
    age=ui_label_create(level_scr,UI_FONT_SMALL,UI_C_DIM);lv_obj_set_pos(age,8,153);
    notice=ui_label_create(level_scr,UI_FONT_SMALL,UI_C_DIM);lv_obj_set_pos(notice,8,176);
    button(level_scr,level_group,4,204,102,"REL ноль",zero_cb,NULL);
    button(level_scr,level_group,110,204,102,"Сброс REL",reset_cb,NULL);
    button(level_scr,level_group,216,204,100,"Меню",menu_cb,NULL);
    ui_show(level_scr,level_group);ui_level_update(lv_tick_get());
}
static void config_cb(lv_event_t *e){
    unsigned action=(unsigned)(uintptr_t)lv_event_get_user_data(e);bool ok=true;
    switch(action){
    case 0:ok=app_set_instrument(g_relative.mode==REL_MODE_HEEL?REL_MODE_LEVEL:REL_MODE_HEEL);break;
    case 1:ok=app_relative_roles(g_relative.config.object_index+2,g_relative.config.reference_index+2);break;
    case 2:ok=app_relative_axes(!g_relative.config.axes_confirmed);break;
    case 3:app_relative_units(!g_app.level_units);break;
    default:ui_menu_back();return;
    }
    ui_set_text(cfg_notice,ok?"Оси: подтверждение, не калибровка":"Занято: запись, передача или сервис");
    ui_level_update(lv_tick_get());
}
void ui_instrument_show(void){
    lv_group_t *g=lv_group_create();config_scr=ui_screen_create_temp(g);
    lv_obj_add_event_cb(config_scr,deleted,LV_EVENT_DELETE,NULL);
    ui_title_create(config_scr,LV_SYMBOL_SETTINGS,"Прибор и оси");
    for(unsigned i=0;i<4;i++)cfg_value[i]=button(config_scr,g,6,30+i*33,308,"",config_cb,(void*)(uintptr_t)i);
    cfg_notice=ui_label_create(config_scr,UI_FONT_SMALL,UI_C_DIM);lv_obj_set_pos(cfg_notice,8,168);
    lv_obj_set_width(cfg_notice,304);lv_label_set_long_mode(cfg_notice,LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text_static(cfg_notice,"Оси: подтверждение, не калибровка");
    button(config_scr,g,6,204,308,"Назад",config_cb,(void*)(uintptr_t)4);
    ui_show(config_scr,g);ui_level_update(lv_tick_get());
}
void ui_level_update(uint32_t now){
    (void)now;char b[96];
    if(ui_screen_alive(config_scr)&&lv_screen_active()==config_scr){
        ui_set_text(cfg_value[0],g_relative.mode==REL_MODE_LEVEL?"Прибор: Угломер":"Прибор: Креномер");
        lv_snprintf(b,sizeof(b),"Опора Д%u / объект Д%u: поменять",g_relative.config.reference_index+2,g_relative.config.object_index+2);ui_set_text(cfg_value[1],b);
        ui_set_text(cfg_value[2],g_relative.config.axes_confirmed?"Оси подтверждены: сбросить":"Оси одинаково: подтвердить");
        ui_set_text(cfg_value[3],g_app.level_units?"Единицы: мм/м":"Единицы: градусы");
    }
    if(!ui_screen_alive(level_scr)||lv_screen_active()!=level_scr)return;
    if(g_relative.mode==REL_MODE_HEEL){ui_main_show();return;}
    relative_result_t r;app_relative_read(&r);
    lv_snprintf(b,sizeof(b),"Опора Д%u / объект Д%u (до нулей головок)",g_relative.config.reference_index+2,g_relative.config.object_index+2);ui_set_text(roles,b);
    bool slope_bad=false;
    for(unsigned i=0;i<2;i++){
        float value=r.relative[i];if(g_app.level_units){value=relative_mm_per_m(value);if(r.ready&&!isfinite(value))slope_bad=true;}
        ui_fmt_fixed(b,sizeof(b),value,g_app.level_units?1:2,"");
        lv_obj_set_style_text_font(v[i],ui_text_width(b,UI_FONT_NUM)>150?UI_FONT_MID:UI_FONT_NUM,0);ui_set_text(v[i],b);
    }
    lv_snprintf(b,sizeof(b),"%s / %s",g_app.level_units?"мм/м":"градусы",r.offset_valid?"REL ноль задан":"без REL нуля");ui_set_text(units,b);
    const char *reason=r.ready?"Пара готова":(r.quality&REL_PAIR_AXES)?"Подтвердите одинаковые оси":
        (r.quality&REL_PAIR_MISSING)?"Нет опоры или объекта":(r.quality&REL_PAIR_ERROR)?"Ошибка ответа головки":
        (r.quality&REL_PAIR_RANGE)?"Угол головки вне диапазона":(r.quality&REL_PAIR_STALE)?"Данные устарели":
        (r.quality&REL_PAIR_SKEW)?"Разнос ответов больше 50 мс":"Ждём свежие ответы обеих головок";
    if(slope_bad)reason="мм/м: угол вне диапазона тангенса";
    ui_set_text(quality,reason);ui_set_text_color(quality,r.ready&&!slope_bad?UI_C_GREEN:UI_C_ORANGE);
    lv_snprintf(b,sizeof(b),"Q:%lu / разнос %lu мс / поколение %lu",(unsigned long)r.quality,(unsigned long)(r.skew_us/1000),(unsigned long)r.generation);ui_set_text(age,b);
}
