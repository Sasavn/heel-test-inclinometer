/* Native LVGL port of tab5-feedback/2026-10-22-ui-design/SPEC.md.
 * Fixed 1280x720 geometry. Main owns hardware, validation and persistence. */
#include "tab5_ui.h"
#include "tab5_ui_graph.h"
#include "tab5_time.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
LV_FONT_DECLARE(font_tab5_16);LV_FONT_DECLARE(font_tab5_16m);LV_FONT_DECLARE(font_tab5_20);LV_FONT_DECLARE(font_tab5_20m);
LV_FONT_DECLARE(font_tab5_24);LV_FONT_DECLARE(font_tab5_32);
LV_FONT_DECLARE(font_tab5_40);LV_FONT_DECLARE(font_tab5_64);
#define F16 (&font_tab5_16)
#define F16M (&font_tab5_16m)
#define F20 (&font_tab5_20)
#define F20M (&font_tab5_20m)
#define F24 (&font_tab5_24)
#define F32 (&font_tab5_32)
#define F40 (&font_tab5_40)
#define F64 (&font_tab5_64)
enum {BG,CARD,ALT,FRAME,TEXT,DIM,MUTED,NAV,NAVSEL,NAVTEXT,NAVMUTED,PRIMARY,ORANGE,RED,OUTLINE,GHOST,OK,WARN,ERR,INFO,NEUTRAL,LINE,STYLE_COUNT};
enum {MEASURE,GRAPH,RECORD,SETTINGS,NETWORK};
enum {A_START=1,A_ZERO,A_RESET,A_APPLY,A_CANCEL,A_REVEAL,A_NET_APPLY,A_LIGHT,A_DARK,A_TIME,A_NUM_OK,A_MODAL_CANCEL,A_CONFIRM,A_DISCARD,A_STAY,A_LANG,A_TEXT_OK,A_TIME_OK,A_NOTICE_CLOSE};
static struct {
    lv_obj_t *notice,*notice_text;char last_notice[160];
    lv_obj_t *root,*rail,*pages[5],*nav[5],*selected[5],*badge,*rail_time,*rail_power;
    lv_style_t style[STYLE_COUNT];bool styles_ready,light;
    uint32_t bg,card,alt,frame,border,text,dim,muted,navc,navsel,action,actiontext,ok,warn,err,info,okbg,warnbg,errbg,infobg,sensor[2];
    lv_obj_t *hero,*hero_date,*hero_title,*hero_sub,*hero_dot,*hero_kpi[2],*hero_key[2],*start[2];
    lv_obj_t *sensor_card[2],*angle[2][2],*health[2],*lost[2],*lost_text[2],*level[2],*level_nodata[2],*roll_bar[2][2],*roll_mark[2][2],*roll_value[2][2],*roll_chip[2][2];
    lv_obj_t *zero_chip,*storage_lines[3],*storage_dot[2],*storage_detail[2],*network_lines[3],*power_lines[3],*pc_chip;
    lv_obj_t *rec_chip,*capacity_text[2],*capacity_bar[2],*media_chip[2],*media_detail[2],*history,*history_note,*history_rows[5][6];
    lv_obj_t *chart,*chart_touch,*legend[4],*ranges[3],*stats[4],*stats_mm[4],*ylabel[9],*xlabel[7],*cursor_label;
    lv_chart_series_t *series[4];int32_t plotted[4][1200];bool visible[4];unsigned range;
    int32_t ymin,ymax;tab5_ui_graph_t graph;
    lv_obj_t *set_apply,*set_cancel,*changed,*step_value[7],*theme_btn[2],*clock_text,*panel_text,*fw_text;
    double draft[7];bool dirty,pending,settings_failed,settings_ack;tab5_measure_config_t sent;
    lv_obj_t *net_apply,*reveal,*net_fields[4],*net_switch[2],*net_address[2],*net_chip[2],*net_hint,*tcp_chip,*tcp_address;
    tab5_ui_network_t net_draft,net_sent;bool net_dirty,net_pending,unmasked,network_failed,network_ack;
    bool result_seen;uint32_t result_serial;
    lv_obj_t *veil,*dialog,*modal_title,*modal_hint,*number,*number_frame,*number_error,*num_done,*text_field,*keyboard,*lang,*date_fields[6],*time_error;
    char num[24];unsigned edit_index;bool numeric_valid;int modal_kind,confirm_action,pending_page;unsigned page;
    bool zero_seen,zero_time_known;float zero[4];char zero_time[16];
    lv_timer_t *repeat;int repeat_id;bool repeated;
    uint64_t render_us;bool rendered;
    tab5_ui_state_t state;tab5_ui_action_cb_t cb;void *user;
} u;
static const char *param_name[]={"Частота опроса","Пауза между запросами","Сглаживание EMA","Окно качки","Порог покоя","Частота выборки качки","Гистерезис"};
static const char *units[]={"Гц","мс","","с","°","Гц","°"};
static const char *limits[]={"1–50 Гц · ограничено RS485","15–100 мс","0.01–0.99 · меньше — плавнее","10–60 с","0.1–10° · размах в окне","2–5 Гц","0–2°"};
static const double lo[]={1,15,.01,10,.1,2,0},hi[]={50,100,.99,60,10,5,2},step[]={1,5,.01,5,.05,1,.05};
static void text(lv_obj_t *o,const char *s){if(o && strcmp(lv_label_get_text(o),s))lv_label_set_text(o,s);}
static void hide(lv_obj_t *o,bool h){if(h)lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);}
static void enabled(lv_obj_t *o,bool on){if(on)lv_obj_remove_state(o,LV_STATE_DISABLED);else lv_obj_add_state(o,LV_STATE_DISABLED);lv_obj_set_style_opa(o,on?255:115,0);}
static void role(lv_obj_t *o,int r){for(int i=0;i<STYLE_COUNT;i++)lv_obj_remove_style(o,&u.style[i],LV_PART_MAIN);lv_obj_add_style(o,&u.style[r],LV_PART_MAIN);}
static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,int r){
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);role(o,r);return o;
}
static lv_obj_t *label(lv_obj_t *parent,const char *s,int x,int y,int w,int h,const lv_font_t *f,int r){
    lv_obj_t *o=lv_label_create(parent);lv_obj_remove_style_all(o);role(o,r);lv_obj_set_style_text_font(o,f,0);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);text(o,s);return o;
}
static lv_obj_t *chip(lv_obj_t *parent,const char *s,int x,int y,int w,int r){lv_obj_t *o=label(parent,s,x,y,w,32,F16M,r);lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_pad_top(o,6,0);return o;}
static void event(lv_event_t *e);
static lv_obj_t *button(lv_obj_t *parent,const char *s,int x,int y,int w,int h,int r,int id){
    lv_obj_t *o=lv_button_create(parent);lv_obj_remove_style_all(o);role(o,r);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l=label(o,s,0,0,w,h,F24,TEXT);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_height(l,LV_SIZE_CONTENT);lv_obj_center(l);
    /* Text follows the button's role, including dark actionText. */
    lv_obj_remove_style(l,&u.style[TEXT],0);
    lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,(void*)(intptr_t)id);return o;
}
static void caption(lv_obj_t *b,const char *s){text(lv_obj_get_child(b,0),s);}
static void line(lv_obj_t *p,int x,int y,int w){box(p,x,y,w,1,LINE);}
static void palette(bool light){
    u.light=light;u.bg=light?0xF3F5F9:0x121826;u.card=light?0xFFFFFF:0x1B2233;u.alt=light?0xF6F8FB:0x18202F;
    u.frame=light?0xF6F7FA:0x2B3340;u.border=light?0xD5DBE6:0x2E3850;u.text=light?0x1A1D24:0xE8ECF4;u.dim=light?0x4A5468:0xB3BDD1;u.muted=light?0x6B7489:0x8A95AD;
    u.navc=light?0x103269:0x0B2247;u.navsel=light?0x1D4E9E:0x1D3F7A;u.action=light?0x1A4FA8:0x7FA8F5;u.actiontext=light?0xFFFFFF:0x0B1733;
    u.ok=light?0x1E7B3A:0x5FD07F;u.warn=light?0x8A5A00:0xF2B84B;u.err=light?0xC0262D:0xFF6B6B;u.info=light?0x1F5FBF:0x7FB0FF;
    u.okbg=light?0xDBF2E0:0x21472E;u.warnbg=light?0xFFF0CC:0x524015;u.errbg=light?0xFCE0E0:0x592121;u.infobg=light?0xE0EBFC:0x213660;
    u.sensor[0]=light?0x1F6BCC:0x5C9EFF;u.sensor[1]=light?0xE06B0D:0xFF9E40;
    for(int r=0;r<STYLE_COUNT;r++){
        if(!u.styles_ready)lv_style_init(&u.style[r]);
        lv_style_t *s=&u.style[r];
        uint32_t bg=u.card,fg=u.text,br=u.border;int radius=16,bw=0;bool fill=true;
        if(r==LINE){bg=u.border;radius=0;}else if(r==BG){bg=u.bg;radius=0;}else if(r==CARD)bw=1;
        else if(r==ALT)bg=u.alt;else if(r==FRAME){bg=u.frame;bw=1;radius=12;}
        else if(r==NAV || r==NAVSEL){bg=r==NAV?u.navc:u.navsel;fg=0xFFFFFF;radius=0;}
        else if(r==PRIMARY){bg=u.action;fg=u.actiontext;radius=14;}
        else if(r==ORANGE){bg=0xF87128;fg=0x1A1D24;radius=14;}
        else if(r==RED){bg=u.err;fg=light?0xFFFFFF:0x2A0B0B;radius=14;}
        else if(r==OUTLINE || r==GHOST){bw=2;br=r==OUTLINE?(light?0x7D879C:0x77829C):u.border;radius=14;if(r==GHOST)fill=false;}
        else if(r==OK){bg=u.okbg;fg=u.ok;}else if(r==WARN){bg=u.warnbg;fg=u.warn;}else if(r==ERR){bg=u.errbg;fg=u.err;}
        else if(r==INFO){bg=u.infobg;fg=u.info;}else if(r==NEUTRAL){bg=u.alt;fg=u.dim;bw=1;}
        else {fill=false;fg=r==MUTED?u.muted:r==DIM?u.dim:r==NAVTEXT?(light?0xEDF2FC:0xE6EDFA):r==NAVMUTED?(light?0x9EB5DB:0x8CA1C7):u.text;}
        lv_style_set_bg_color(s,lv_color_hex(bg));lv_style_set_bg_opa(s,fill?255:0);lv_style_set_text_color(s,lv_color_hex(fg));lv_style_set_border_width(s,bw);lv_style_set_border_color(s,lv_color_hex(br));lv_style_set_radius(s,radius);lv_style_set_pad_all(s,0);lv_style_set_text_font(s,F20);
        lv_style_set_shadow_width(s,r==CARD?8:0);lv_style_set_shadow_offset_y(s,2);lv_style_set_shadow_color(s,lv_color_hex(light?0x1A294D:0x000000));lv_style_set_shadow_opa(s,light?22:70);
    }
    u.styles_ready=true;lv_obj_report_style_change(NULL);
}
static void draw_line(lv_layer_t *layer,float x1,float y1,float x2,float y2,uint32_t color,int width,int dash){
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.p1=(lv_point_precise_t){x1,y1};d.p2=(lv_point_precise_t){x2,y2};d.color=lv_color_hex(color);d.width=width;if(dash){d.dash_width=dash;d.dash_gap=dash;}lv_draw_line(layer,&d);
}
static void arc(lv_layer_t *layer,int x,int y,int radius,int a1,int a2,uint32_t color,int width,int opa){
    lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.center=(lv_point_t){x,y};d.radius=radius;d.start_angle=a1;d.end_angle=a2;d.color=lv_color_hex(color);d.width=width;d.opa=opa;d.rounded=1;lv_draw_arc(layer,&d);
}
static void tri(lv_layer_t *layer,float x1,float y1,float x2,float y2,float x3,float y3,uint32_t color){
    lv_draw_triangle_dsc_t d;lv_draw_triangle_dsc_init(&d);d.p[0]=(lv_point_precise_t){x1,y1};d.p[1]=(lv_point_precise_t){x2,y2};d.p[2]=(lv_point_precise_t){x3,y3};d.color=lv_color_hex(color);d.opa=255;lv_draw_triangle(layer,&d);
}
static void circle(lv_layer_t *layer,int x,int y,int radius,uint32_t color,int width,bool fill,int opa){
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.radius=LV_RADIUS_CIRCLE;d.bg_color=lv_color_hex(color);d.bg_opa=fill?opa:0;d.border_color=lv_color_hex(color);d.border_width=width;d.border_opa=opa;lv_area_t a={x-radius,y-radius,x+radius,y+radius};lv_draw_rect(layer,&d,&a);
}
/* Small native vector icons: no bitmap asset, FontAwesome dependency or AI image. */
static void icon_draw(lv_event_t *e){
    lv_obj_t *o=lv_event_get_target(e);int id=(intptr_t)lv_event_get_user_data(e);lv_area_t a;lv_obj_get_coords(o,&a);lv_layer_t *l=lv_event_get_layer(e);
    int x=a.x1,y=a.y1,w=lv_obj_get_width(o);float k=w/32.f;uint32_t c=lv_color_to_u32(lv_obj_get_style_text_color(o,0))&0xffffff;
    #define L(x1,y1,x2,y2) draw_line(l,x+(x1)*k,y+(y1)*k,x+(x2)*k,y+(y2)*k,c,2,0)
    if(id==0){circle(l,x+16*k,y+16*k,11*k,c,2,false,255);L(16,3,16,10);L(16,22,16,29);L(3,16,10,16);L(22,16,29,16);circle(l,x+19*k,y+13*k,2*k,c,1,true,255);}
    else if(id==1){L(3,26,29,26);L(4,20,9,12);L(9,12,16,23);L(16,23,23,11);L(23,11,28,17);}
    else if(id==2){L(6,4,26,4);L(6,4,6,28);L(26,4,26,28);L(6,28,26,28);L(10,4,10,11);L(10,11,22,11);L(22,11,22,4);circle(l,x+16*k,y+20*k,4*k,c,1,true,255);}
    else if(id==6){L(12,3,25,3);L(25,3,25,29);L(25,29,7,29);L(7,29,7,8);L(7,8,12,3);L(13,8,13,13);L(17,8,17,13);L(21,8,21,13);}
    else if(id==11){L(16,4,16,22);L(10,10,16,4);L(16,4,22,10);L(6,17,6,27);L(6,27,26,27);L(26,27,26,17);}
    else if(id==12){arc(l,x+16*k,y+34*k,24*k,236,304,c,2,255);arc(l,x+16*k,y-2*k,24*k,56,124,c,2,255);circle(l,x+16*k,y+16*k,4*k,c,2,false,255);}
    else if(id==10){L(3,25,29,25);arc(l,x+15*k,y+25*k,11*k,180,360,c,2,140);L(15,25,25,9);circle(l,x+15*k,y+25*k,2.5f*k,c,1,true,255);}
    else if(id==3){L(4,9,28,9);L(4,23,28,23);circle(l,x+20*k,y+9*k,3*k,c,2,true,255);circle(l,x+12*k,y+23*k,3*k,c,2,true,255);}
    else if(id==4){for(int r=0;r<3;r++)arc(l,x+16*k,y+27*k,(22-r*7)*k,228,312,c,2,255);circle(l,x+16*k,y+27*k,2*k,c,1,true,255);}
    else if(id==5){circle(l,x+16*k,y+16*k,11*k,c,2,false,255);L(5,16,27,16);}
    else if(id==7){
        /* Молния макета (12,2 5,12 10,12 9,20 16,10 11,10 в сетке 22) — закрашенная. */
        float q=32.f/22*k;
        #define P(a,b) x+(a)*q,y+(b)*q
        tri(l,P(12,2),P(5,12),P(10,12),c);tri(l,P(12,2),P(10,12),P(11,10),c);
        tri(l,P(10,12),P(9,20),P(16,10),c);tri(l,P(10,12),P(16,10),P(11,10),c);
        #undef P
    }
    else if(id==8){L(3,5,29,5);L(3,5,3,23);L(29,5,29,23);L(3,23,29,23);L(16,23,16,29);L(10,29,22,29);}
    else {L(5,9,27,9);L(5,16,23,16);L(5,23,18,23);}
    #undef L
}
static lv_obj_t *icon(lv_obj_t *p,int id,int x,int y,int size,int r){lv_obj_t *o=box(p,x,y,size,size,r);lv_obj_add_event_cb(o,icon_draw,LV_EVENT_DRAW_MAIN,(void*)(intptr_t)id);return o;}
static void heading(lv_obj_t *p,int id,const char *s){icon(p,id,20,20,22,MUTED);lv_point_t size;lv_text_get_size(&size,s,F20M,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);lv_obj_update_layout(p);int w=lv_obj_get_width(p)-72;if(size.x<w)w=size.x+1;label(p,s,52,19,w,30,F20M,TEXT);}
/* Заголовок страницы 32 Bold; возвращает x, с которого можно ставить чип состояния. */
static int page_title(lv_obj_t *p,const char *s){lv_point_t z;lv_text_get_size(&z,s,F32,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);label(p,s,0,15,z.x+2,44,F32,TEXT);return z.x+20;}
static void fit_chip(lv_obj_t *o){lv_obj_set_width(o,LV_SIZE_CONTENT);lv_obj_set_style_pad_hor(o,14,0);}
/* Декор баннера: дуги-«шкала кренометра» справа и тонкие горизонтали, как в макете. */
static void hero_draw(lv_event_t *e){
    lv_obj_t *o=lv_event_get_target(e);lv_area_t a;lv_obj_get_coords(o,&a);lv_layer_t *l=lv_event_get_layer(e);
    for(int i=0;i<3;i++)arc(l,a.x1+1120,a.y1+128,300-i*60,180,270,0xFFFFFF,2,40);
    for(int i=1;i<=3;i++){lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.p1=(lv_point_precise_t){a.x1,a.y1+i*32};d.p2=(lv_point_precise_t){a.x2,a.y1+i*32};d.color=lv_color_white();d.width=1;d.opa=i==3?20:12;lv_draw_line(l,&d);}
}
static void level_draw(lv_event_t *e){
    unsigned i=(intptr_t)lv_event_get_user_data(e);lv_obj_t *o=lv_event_get_target(e);lv_area_t a;lv_obj_get_coords(o,&a);lv_layer_t *l=lv_event_get_layer(e);int x=a.x1+100,y=a.y1+100;
    circle(l,x,y,96,u.alt,1,true,255);circle(l,x,y,96,u.border,1,false,255);
    int radii[]={90,36,18};for(int k=0;k<3;k++)circle(l,x,y,radii[k],u.border,1,false,255);
    draw_line(l,x-94,y,x+94,y,u.border,1,0);draw_line(l,x,y-94,x,y+94,u.border,1,0);
    const tab5_measure_sensor_t *m=&u.state.measurement.sensor[i];if(!m->valid || !isfinite(m->x)||!isfinite(m->y))return;
    float vx=m->x,vy=m->y,r=hypotf(vx,vy);bool out=r>5;if(out){vx*=5/r;vy*=5/r;}
    int px=x+lroundf(vx*18),py=y-lroundf(vy*18);float rx=fminf(90,fmaxf(4,m->roll_x*18)),ry=fminf(90,fmaxf(4,m->roll_y*18));
    /* Ellipse is clipped by the gauge object's bounds. */
    for(int dy=-(int)ry;dy<=(int)ry;dy+=2){float dx=rx*sqrtf(fmaxf(0,1-dy*dy/(ry*ry)));lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.p1=(lv_point_precise_t){px-dx,py+dy};d.p2=(lv_point_precise_t){px+dx,py+dy};d.width=2;d.color=lv_color_hex(u.sensor[i]);d.opa=46;lv_draw_line(l,&d);}
    draw_line(l,x,y,px,py,u.sensor[i],1,4);circle(l,px,py,9,u.sensor[i],3,!out,255);
}
static void config_values(const tab5_measure_config_t *c,double v[7]){v[0]=c->frequency_hz;v[1]=c->gap_ms;v[2]=c->alpha;v[3]=c->roll_window_s;v[4]=c->roll_calm_deg;v[5]=c->roll_rate_hz;v[6]=c->roll_hysteresis_deg;}
static tab5_measure_config_t draft_config(void){return (tab5_measure_config_t){.frequency_hz=(uint16_t)u.draft[0],.gap_ms=(uint16_t)u.draft[1],.alpha=u.draft[2],.roll_window_s=(uint8_t)u.draft[3],.roll_calm_deg=u.draft[4],.roll_rate_hz=(uint8_t)u.draft[5],.roll_hysteresis_deg=u.draft[6]};}
static bool cfg_equal(const tab5_measure_config_t *a,const tab5_measure_config_t *b){double va[7],vb[7];config_values(a,va);config_values(b,vb);for(int i=0;i<7;i++)if(fabs(va[i]-vb[i])>.0001)return false;return true;}
static bool net_equal(const tab5_ui_network_t *a,const tab5_ui_network_t *b){return a->ap_enabled==b->ap_enabled&&a->sta_enabled==b->sta_enabled&&!strcmp(a->ap_ssid,b->ap_ssid)&&!strcmp(a->ap_password,b->ap_password)&&!strcmp(a->sta_ssid,b->sta_ssid)&&!strcmp(a->sta_password,b->sta_password);}
static void dispatch(int kind){tab5_ui_action_t a={.kind=kind,.config=u.state.measurement.config,.light_theme=u.state.light_theme,.network=u.state.network};if(u.cb)u.cb(&a,u.user);}
static void refresh_settings(void){
    double actual[7];config_values(&u.state.measurement.config,actual);unsigned n=0;
    for(unsigned i=0;i<7;i++){bool changed=fabs(u.draft[i]-actual[i])>.0001;n+=changed;char s[40];snprintf(s,sizeof(s),i==2||i==4||i==6?"%.2f%s%s":"%.0f%s%s",u.draft[i],units[i][0]&&strcmp(units[i],"°")?" ":"",units[i]);caption(u.step_value[i],s);role(u.step_value[i],changed?INFO:FRAME);}
    u.dirty=n>0||u.settings_failed;char s[64];snprintf(s,sizeof(s),u.pending?"ожидает подтверждения":u.settings_failed?"не сохранено · повторите":"изменено: %u",n);text(u.changed,s);hide(u.changed,!u.dirty&&!u.pending);enabled(u.set_apply,u.dirty&&!u.pending);enabled(u.set_cancel,u.dirty&&!u.pending);
}
static void close_modal(void){if(u.veil)lv_obj_delete(u.veil);u.veil=u.dialog=u.keyboard=u.text_field=NULL;u.modal_kind=0;}
static void open_modal(int x,int y,int w,int h,int kind){close_modal();u.modal_kind=kind;u.veil=box(u.root,0,0,1280,720,BG);lv_obj_set_style_bg_color(u.veil,lv_color_hex(0x080E1E),0);lv_obj_set_style_bg_opa(u.veil,140,0);lv_obj_add_flag(u.veil,LV_OBJ_FLAG_CLICKABLE);u.dialog=box(u.veil,x,y,w,h,CARD);lv_obj_set_style_radius(u.dialog,20,0);}
static void show_page(unsigned page){u.page=page;for(unsigned i=0;i<5;i++){hide(u.pages[i],page!=i);role(u.nav[i],page==i?NAVSEL:NAV);hide(u.selected[i],page!=i);}lv_obj_invalidate(u.root);}
static bool apply_settings(void){tab5_ui_action_t a={.kind=TAB5_UI_SETTINGS,.config=draft_config(),.light_theme=u.state.light_theme};u.sent=a.config;u.pending=true;u.settings_ack=false;if(u.cb)u.cb(&a,u.user);refresh_settings();return true;}
static bool apply_network(void){
    size_t ap=strlen(u.net_draft.ap_password),sta=strlen(u.net_draft.sta_password);
    if((u.net_draft.ap_enabled&&(!u.net_draft.ap_ssid[0]||ap<8||ap>63)) || (u.net_draft.sta_enabled&&(!u.net_draft.sta_ssid[0]||(sta&&sta<8)||sta>63))){text(u.net_hint,"✗ Проверьте SSID и пароль: AP 8–63, STA пустой или 8–63 байта");role(u.net_hint,ERR);return false;}
    tab5_ui_action_t a={.kind=TAB5_UI_NETWORK,.network=u.net_draft};u.net_sent=u.net_draft;u.net_pending=true;u.network_ack=false;if(u.cb)u.cb(&a,u.user);enabled(u.net_apply,false);text(u.net_hint,"Ожидание подтверждения настроек сети");return true;
}
static void ask_navigation(unsigned page){
    if(page==u.page)return;
    if((u.page==SETTINGS&&u.dirty&&!u.pending)||(u.page==NETWORK&&u.net_dirty&&!u.net_pending)){
        u.pending_page=page;open_modal(300,220,680,280,3);label(u.dialog,"Есть неприменённые изменения",24,24,632,40,F32,TEXT);
        button(u.dialog,"Применить",24,104,200,64,PRIMARY,A_APPLY);button(u.dialog,"Отбросить",240,104,200,64,OUTLINE,A_DISCARD);button(u.dialog,"Остаться",456,104,200,64,GHOST,A_STAY);
    }else show_page(page);
}
static void confirm(int action){u.confirm_action=action;open_modal(300,220,680,280,4);label(u.dialog,action==A_ZERO?"Ноль изменится посреди записи.\nПродолжить?":"Сбросить ноль обоих датчиков?",24,24,632,80,F24,TEXT);button(u.dialog,"Отмена",24,168,304,64,GHOST,A_MODAL_CANCEL);button(u.dialog,"Подтвердить",344,168,312,64,PRIMARY,A_CONFIRM);}
static bool valid_number(double v,unsigned i){return isfinite(v)&&v>=lo[i]-.000001&&v<=hi[i]+.000001&&((i==0||i==1||i==3||i==5)?fabs(v-round(v))<.000001:true);}
static void number_refresh(void){
    char *end;double v=strtod(u.num,&end);u.numeric_valid=*u.num && !*end && valid_number(v,u.edit_index);text(u.number,u.num);
    lv_obj_set_style_border_color(u.number_frame,lv_color_hex(u.numeric_valid?u.border:u.err),0);enabled(u.num_done,u.numeric_valid);
    char s[160];if(u.numeric_valid)s[0]=0;else snprintf(s,sizeof(s),"✗ допустимо %.2f–%.2f %s; проверьте значение",lo[u.edit_index],hi[u.edit_index],units[u.edit_index]);text(u.number_error,s);
}
static void open_number(unsigned i){
    u.edit_index=i;snprintf(u.num,sizeof(u.num),(i==0||i==1||i==3||i==5)?"%.0f":"%.2f",u.draft[i]);open_modal(340,52,600,616,1);
    label(u.dialog,param_name[i],28,24,544,36,F32,TEXT);char s[128];snprintf(s,sizeof(s),"допустимо %.2f–%.2f %s · сейчас %.2f",lo[i],hi[i],units[i],u.draft[i]);label(u.dialog,s,28,66,544,28,F16,MUTED);
    u.number_frame=box(u.dialog,28,98,544,80,FRAME);lv_obj_set_style_border_width(u.number_frame,2,0);u.number=label(u.number_frame,u.num,12,10,444,58,F40,TEXT);lv_obj_set_style_text_align(u.number,LV_TEXT_ALIGN_RIGHT,0);label(u.number_frame,units[i],464,26,72,36,F24,MUTED);
    u.number_error=label(u.dialog,"",28,186,544,28,F16,ERR);lv_obj_set_style_bg_opa(u.number_error,0,0);
    const char *keys[]={"7","8","9","⌫","4","5","6","±","1","2","3","C","0",".","было"};
    for(int k=0;k<15;k++){int row=k/4,col=k%4,w=128,x=28+col*138;if(k>=12){row=3;if(k==12)w=266;else x=k==13?304:442;}lv_obj_t *b=button(u.dialog,keys[k],x,218+row*74,w,64,ALT,400+k);lv_obj_set_style_border_width(b,1,0);lv_obj_set_style_border_color(b,lv_color_hex(u.border),0);lv_obj_set_style_radius(b,12,0);if(k==7 || (k==13&&(i==0||i==1||i==3||i==5)))enabled(b,false);}
    button(u.dialog,"Отмена",28,520,266,64,GHOST,A_MODAL_CANCEL);u.num_done=button(u.dialog,"Готово",306,520,266,64,PRIMARY,A_NUM_OK);number_refresh();
}
static void step_change(int id){unsigned i=(id-200)/2;double v=u.draft[i]+((id-200)%2?step[i]:-step[i]);u.draft[i]=fmin(hi[i],fmax(lo[i],round(v*100)/100));refresh_settings();}
static void repeat_tick(lv_timer_t *t){(void)t;u.repeated=true;step_change(u.repeat_id);lv_timer_set_period(u.repeat,80);}
static void step_press(lv_event_t *e){
    if(lv_event_get_code(e)==LV_EVENT_PRESSED){if(u.repeat)lv_timer_delete(u.repeat);u.repeated=false;u.repeat_id=(intptr_t)lv_event_get_user_data(e);u.repeat=lv_timer_create(repeat_tick,400,NULL);}
    else if(u.repeat){lv_timer_delete(u.repeat);u.repeat=NULL;}
}

static const char *ru_map[]={"1#","й","ц","у","к","е","н","г","ш","щ","з","х",LV_SYMBOL_BACKSPACE,"\n","ф","ы","в","а","п","р","о","л","д","ж","э","\n","я","ч","с","м","и","т","ь","б","ю",".","\n",LV_SYMBOL_CLOSE,LV_SYMBOL_LEFT," ",LV_SYMBOL_RIGHT,LV_SYMBOL_OK,""};
static lv_buttonmatrix_ctrl_t ru_ctrl[41];
static bool russian;
static lv_font_t keyboard_font;
static void keyboard_style(lv_obj_t *k){
    lv_obj_set_style_bg_color(k,lv_color_hex(u.bg),0);lv_obj_set_style_pad_all(k,10,0);lv_obj_set_style_pad_row(k,10,0);lv_obj_set_style_pad_column(k,10,0);
    lv_obj_set_style_bg_color(k,lv_color_hex(u.alt),LV_PART_ITEMS);lv_obj_set_style_border_color(k,lv_color_hex(u.border),LV_PART_ITEMS);lv_obj_set_style_border_width(k,1,LV_PART_ITEMS);lv_obj_set_style_radius(k,12,LV_PART_ITEMS);keyboard_font=font_tab5_24;keyboard_font.fallback=LV_FONT_DEFAULT;lv_obj_set_style_text_font(k,&keyboard_font,LV_PART_ITEMS);lv_obj_set_style_text_font(k,&keyboard_font,LV_PART_ITEMS|LV_STATE_CHECKED);lv_obj_set_style_text_color(k,lv_color_hex(u.text),LV_PART_ITEMS);
    lv_obj_set_style_bg_color(k,lv_color_hex(u.alt),LV_PART_ITEMS|LV_STATE_CHECKED);lv_obj_set_style_text_color(k,lv_color_hex(u.dim),LV_PART_ITEMS|LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(k,lv_color_hex(u.action),LV_PART_ITEMS|LV_STATE_PRESSED);lv_obj_set_style_text_color(k,lv_color_hex(u.actiontext),LV_PART_ITEMS|LV_STATE_PRESSED);
}
static void keyboard_done(lv_event_t *e){if(u.modal_kind==2){if(lv_event_get_code(e)==LV_EVENT_READY)event(e);else close_modal();}}
static void open_text(unsigned i){
    u.edit_index=i;open_modal(120,24,1144,300,2);const char *names[]={"AP: имя сети","AP: пароль","STA: имя сети","STA: пароль"};label(u.dialog,names[i],24,20,780,40,F32,TEXT);
    u.lang=button(u.dialog,russian?"РУС → EN":"EN → РУС",904,16,216,56,OUTLINE,A_LANG);
    u.text_field=lv_textarea_create(u.dialog);lv_obj_set_pos(u.text_field,24,84);lv_obj_set_size(u.text_field,1096,64);lv_textarea_set_one_line(u.text_field,true);lv_textarea_set_max_length(u.text_field,i%2?63:32);lv_obj_set_style_text_font(u.text_field,F24,0);lv_obj_set_style_pad_top(u.text_field,14,0);lv_obj_set_style_pad_bottom(u.text_field,14,0);lv_obj_set_style_bg_color(u.text_field,lv_color_hex(u.frame),0);lv_obj_set_style_text_color(u.text_field,lv_color_hex(u.text),0);
    const char *v[]={u.net_draft.ap_ssid,u.net_draft.ap_password,u.net_draft.sta_ssid,u.net_draft.sta_password};lv_textarea_set_text(u.text_field,v[i]);lv_textarea_set_password_mode(u.text_field,i%2&&!u.unmasked);
    label(u.dialog,i%2?"Пароль: AP 8–63 байта; STA пустой или 8–63 байта":"SSID: до 32 байт UTF-8",24,164,1096,32,F16,MUTED);
    button(u.dialog,"Отмена",24,220,530,56,GHOST,A_MODAL_CANCEL);button(u.dialog,"Готово",570,220,550,56,PRIMARY,A_TEXT_OK);
    u.keyboard=lv_keyboard_create(u.veil);lv_obj_set_size(u.keyboard,1280,360);lv_obj_set_align(u.keyboard,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(u.keyboard,0,360);keyboard_style(u.keyboard);lv_keyboard_set_textarea(u.keyboard,u.text_field);
    if(russian){for(unsigned k=0;k<41;k++)ru_ctrl[k]=1;lv_keyboard_set_map(u.keyboard,LV_KEYBOARD_MODE_USER_1,ru_map,ru_ctrl);lv_keyboard_set_mode(u.keyboard,LV_KEYBOARD_MODE_USER_1);}
    lv_obj_add_event_cb(u.keyboard,keyboard_done,LV_EVENT_READY,(void*)(intptr_t)A_TEXT_OK);lv_obj_add_event_cb(u.keyboard,keyboard_done,LV_EVENT_CANCEL,NULL);
}
static bool date_valid(tab5_ui_datetime_t *out){
    int v[6];for(int i=0;i<6;i++){const char *s=lv_textarea_get_text(u.date_fields[i]);if(!*s)return false;for(const char *c=s;*c;c++)if(*c<'0'||*c>'9')return false;v[i]=atoi(s);}
    if(!tab5_time_fields_valid(v[0],v[1],v[2],v[3],v[4],v[5]))return false;
    *out=(tab5_ui_datetime_t){v[0],v[1],v[2],v[3],v[4],v[5]};return true;
}
static lv_obj_t *time_done;
static void date_edited(lv_event_t *e){(void)e;tab5_ui_datetime_t t;bool valid=date_valid(&t);enabled(time_done,valid);text(u.time_error,valid?"":"✗ Проверьте календарную дату и время (2000–2099)");}
static void date_focus(lv_event_t *e){if(u.keyboard)lv_keyboard_set_textarea(u.keyboard,lv_event_get_target(e));}
static void open_time(void){
    open_modal(140,24,1000,316,5);label(u.dialog,"Дата и время",24,16,952,40,F32,TEXT);
    const char *names[]={"Год","Месяц","День","Час","Минута","Секунда"};tab5_ui_datetime_t t=u.state.datetime;unsigned v[]={t.year,t.month,t.day,t.hour,t.minute,t.second};
    for(int i=0;i<6;i++){int x=24+i*160;label(u.dialog,names[i],x,64,144,24,F16,MUTED);u.date_fields[i]=lv_textarea_create(u.dialog);lv_obj_set_pos(u.date_fields[i],x,94);lv_obj_set_size(u.date_fields[i],144,56);lv_textarea_set_one_line(u.date_fields[i],true);lv_textarea_set_accepted_chars(u.date_fields[i],"0123456789");lv_textarea_set_max_length(u.date_fields[i],i?2:4);lv_obj_set_style_text_font(u.date_fields[i],F24,0);lv_obj_set_style_pad_top(u.date_fields[i],14,0);lv_obj_set_style_pad_bottom(u.date_fields[i],14,0);lv_obj_set_style_bg_color(u.date_fields[i],lv_color_hex(u.frame),0);lv_obj_set_style_text_color(u.date_fields[i],lv_color_hex(u.text),0);char s[16];snprintf(s,sizeof(s),"%u",v[i]);lv_textarea_set_text(u.date_fields[i],u.state.datetime_known?s:"");lv_obj_add_event_cb(u.date_fields[i],date_focus,LV_EVENT_FOCUSED,NULL);}
    u.time_error=label(u.dialog,"",24,166,952,32,F16,ERR);lv_obj_set_style_bg_opa(u.time_error,0,0);
    button(u.dialog,"Отмена",24,224,464,64,GHOST,A_MODAL_CANCEL);time_done=button(u.dialog,"Готово",504,224,472,64,PRIMARY,A_TIME_OK);
    u.keyboard=lv_keyboard_create(u.veil);lv_obj_set_size(u.keyboard,1280,360);lv_obj_set_align(u.keyboard,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(u.keyboard,0,360);keyboard_style(u.keyboard);lv_keyboard_set_mode(u.keyboard,LV_KEYBOARD_MODE_NUMBER);lv_keyboard_set_textarea(u.keyboard,u.date_fields[0]);
    for(int i=0;i<6;i++)lv_obj_add_event_cb(u.date_fields[i],date_edited,LV_EVENT_VALUE_CHANGED,NULL);
    date_edited(NULL);
}
static void refresh_network(void){
    const char *v[]={u.net_draft.ap_ssid,u.net_draft.ap_password,u.net_draft.sta_ssid,u.net_draft.sta_password};
    for(unsigned i=0;i<4;i++){char masked[64];if(i%2&&!u.unmasked){size_t n=strlen(v[i]);if(n>20)n=20;for(size_t q=0;q<n;q++)memcpy(masked+q*3,"\xE2\x80\xA2",3);masked[n*3]=0;text(lv_obj_get_child(u.net_fields[i],1),masked);}else text(lv_obj_get_child(u.net_fields[i],1),v[i]);}
    if(u.net_draft.ap_enabled)lv_obj_add_state(u.net_switch[0],LV_STATE_CHECKED);else lv_obj_remove_state(u.net_switch[0],LV_STATE_CHECKED);
    if(u.net_draft.sta_enabled)lv_obj_add_state(u.net_switch[1],LV_STATE_CHECKED);else lv_obj_remove_state(u.net_switch[1],LV_STATE_CHECKED);
    u.net_dirty=!net_equal(&u.net_draft,&u.state.network)||u.network_failed;enabled(u.net_apply,u.net_dirty&&!u.net_pending);caption(u.reveal,u.unmasked?"Скрыть пароли":"Показать пароли");
}
static void switch_changed(lv_event_t *e){(void)e;u.net_draft.ap_enabled=lv_obj_has_state(u.net_switch[0],LV_STATE_CHECKED);u.net_draft.sta_enabled=lv_obj_has_state(u.net_switch[1],LV_STATE_CHECKED);refresh_network();}
static void chart_refresh(void);
static void event(lv_event_t *e){
    int id=(intptr_t)lv_event_get_user_data(e);
    if(id>=100&&id<105){ask_navigation(id-100);if(u.page==GRAPH)chart_refresh();return;}
    if(id>=200&&id<214){if(!u.repeated)step_change(id);u.repeated=false;return;}
    if(id>=300&&id<307){open_number(id-300);return;}
    if(id>=400&&id<415){int k=id-400;size_t n=strlen(u.num);const char *digits[]={"7","8","9","","4","5","6","","1","2","3","","0",".",""};
        if(k==3){if(n)u.num[n-1]=0;}else if(k==11)u.num[0]=0;else if(k==14)snprintf(u.num,sizeof(u.num),"%.2f",u.draft[u.edit_index]);else if(n+1<sizeof(u.num)){if(k!=13||!strchr(u.num,'.'))strcat(u.num,digits[k]);}number_refresh();return;}
    if(id>=500&&id<504){open_text(id-500);return;}
    if(id>=600&&id<604){unsigned i=id-600;u.visible[i]=!u.visible[i];lv_obj_set_style_opa(u.legend[i],u.visible[i]?255:115,0);chart_refresh();return;}
    if(id>=610&&id<613){u.range=(unsigned)(id-610);chart_refresh();return;}
    switch(id){
    case A_NOTICE_CLOSE:if(u.notice)lv_obj_delete(u.notice);u.notice=u.notice_text=NULL;break;
    case A_START:dispatch(u.state.recording?TAB5_UI_STOP:TAB5_UI_START);break;
    case A_ZERO:if(u.state.recording)confirm(A_ZERO);else dispatch(TAB5_UI_ZERO);break;
    case A_RESET:confirm(A_RESET);break;
    case A_CONFIRM:{int a=u.confirm_action;close_modal();dispatch(a==A_ZERO?TAB5_UI_ZERO:TAB5_UI_ZERO_RESET);break;}
    case A_MODAL_CANCEL:case A_STAY:close_modal();break;
    case A_DISCARD:if(u.page==SETTINGS){config_values(&u.state.measurement.config,u.draft);u.pending=false;u.settings_failed=false;refresh_settings();}else {u.net_draft=u.state.network;u.net_pending=false;u.network_failed=false;refresh_network();}close_modal();show_page(u.pending_page);break;
    case A_APPLY:if(u.modal_kind==3){bool applied=u.page==SETTINGS?apply_settings():apply_network();if(applied){unsigned p=u.pending_page;close_modal();show_page(p);}}else apply_settings();break;
    case A_CANCEL:config_values(&u.state.measurement.config,u.draft);u.settings_failed=false;u.pending=false;refresh_settings();break;
    case A_NET_APPLY:apply_network();break;
    case A_REVEAL:u.unmasked=!u.unmasked;refresh_network();break;
    case A_LIGHT:case A_DARK:{bool light=id==A_LIGHT;tab5_ui_action_t a={.kind=TAB5_UI_SETTINGS,.config=u.pending?u.sent:u.state.measurement.config,.light_theme=light};if(u.cb)u.cb(&a,u.user);tab5_ui_state_t s=u.state;s.light_theme=light;tab5_ui_update(&s);break;}
    case A_NUM_OK:if(u.numeric_valid){u.draft[u.edit_index]=strtod(u.num,NULL);close_modal();refresh_settings();}break;
    case A_TEXT_OK:{const char *s=lv_textarea_get_text(u.text_field);size_t n=strlen(s),max=u.edit_index%2?63:32;if(n>max){lv_obj_set_style_border_color(u.text_field,lv_color_hex(u.err),0);break;}char *v[]={u.net_draft.ap_ssid,u.net_draft.ap_password,u.net_draft.sta_ssid,u.net_draft.sta_password};snprintf(v[u.edit_index],u.edit_index%2?65:33,"%s",s);close_modal();refresh_network();break;}
    case A_LANG:russian=!russian;caption(u.lang,russian?"РУС → EN":"EN → РУС");if(russian){for(unsigned k=0;k<41;k++)ru_ctrl[k]=1;lv_keyboard_set_map(u.keyboard,LV_KEYBOARD_MODE_USER_1,ru_map,ru_ctrl);lv_keyboard_set_mode(u.keyboard,LV_KEYBOARD_MODE_USER_1);}else lv_keyboard_set_mode(u.keyboard,LV_KEYBOARD_MODE_TEXT_LOWER);break;
    case A_TIME:open_time();break;
    case A_TIME_OK:{tab5_ui_datetime_t t;if(date_valid(&t)){tab5_ui_action_t a={.kind=TAB5_UI_TIME,.datetime=t};if(u.cb)u.cb(&a,u.user);close_modal();}break;}
    }
}
static void chart_draw_task(lv_event_t *e){lv_draw_task_t *t=lv_event_get_draw_task(e);lv_draw_line_dsc_t *d=lv_draw_task_get_line_dsc(t);if(d&&d->base.part==LV_PART_ITEMS&&(d->base.id1%2)){d->dash_width=6;d->dash_gap=5;}}
static void chart_touch(lv_event_t *e){
    lv_indev_t *dev=lv_indev_active();if(!dev)return;lv_point_t p;lv_indev_get_point(dev,&p);lv_area_t a;lv_obj_get_coords(u.chart,&a);int x=p.x-a.x1;if(x<0)x=0;if(x>1039)x=1039;unsigned n=u.range==0?300:u.range==1?240:1200,idx=(unsigned)(x*(n-1)/1039);char s[180];size_t used=0;
    used+=snprintf(s+used,sizeof(s)-used,"−%u с",(unsigned)((n-1-idx)*(u.range?500:100)/1000));for(unsigned i=0;i<4;i++){int32_t v=u.plotted[i][idx];if(u.visible[i]&&v!=LV_CHART_POINT_NONE)used+=snprintf(s+used,sizeof(s)-used,"  Д%u %c %+.2f°",i/2+2,i%2?'Y':'X',v/100.0);if(used>=sizeof(s)-1)break;}text(u.cursor_label,s);
    (void)e;
}
static void chart_refresh(void){
    unsigned seconds=u.range==0?30:u.range==1?120:600;int32_t min=0,max=0;bool any=false;unsigned n=0;
    for(unsigned k=0;k<4;k++){n=tab5_ui_graph_read(&u.graph,seconds,k,u.plotted[k]);if(u.visible[k])for(unsigned i=0;i<n;i++)if(u.plotted[k][i]!=LV_CHART_POINT_NONE){int32_t v=u.plotted[k][i];if(!any){min=max=v;any=true;}if(v<min)min=v;if(v>max)max=v;}}
    u.ymin=any?(int32_t)(floor(min/50.0)*50):-100;u.ymax=any?(int32_t)(ceil(max/50.0)*50):100;if(u.ymax-u.ymin<100){u.ymin-=50;u.ymax+=50;}
    int32_t tick=50;unsigned ticks;
    do {u.ymin=(int32_t)floor((any?min:-100)/(double)tick)*tick;u.ymax=(int32_t)ceil((any?max:100)/(double)tick)*tick;if(u.ymax<=u.ymin)u.ymax=u.ymin+tick*2;ticks=(unsigned)((u.ymax-u.ymin)/tick+1);if(ticks>9)tick+=50;}while(ticks>9);
    if(ticks<3){u.ymin-=tick;u.ymax+=tick;ticks+=2;}
    lv_chart_set_point_count(u.chart,n);lv_chart_set_axis_range(u.chart,LV_CHART_AXIS_PRIMARY_Y,u.ymin,u.ymax);lv_chart_set_div_line_count(u.chart,ticks,0);
    for(unsigned k=0;k<4;k++){
        for(uint32_t c=1;c<lv_obj_get_child_count(u.legend[k]);c++)lv_obj_set_style_bg_color(lv_obj_get_child(u.legend[k],c),lv_color_hex(u.sensor[k/2]),0);
        lv_obj_set_style_bg_color(lv_obj_get_child(lv_obj_get_parent(u.stats[k]),0),lv_color_hex(u.sensor[k/2]),0);lv_chart_set_series_ext_y_array(u.chart,u.series[k],u.plotted[k]);lv_chart_set_series_color(u.chart,u.series[k],lv_color_hex(u.sensor[k/2]));lv_chart_hide_series(u.chart,u.series[k],!u.visible[k]);
        int32_t mn=0,mx=0,last=LV_CHART_POINT_NONE;bool found=false;
        for(unsigned i=0;i<n;i++)if(u.plotted[k][i]!=LV_CHART_POINT_NONE){int32_t v=u.plotted[k][i];if(!found)mn=mx=v;found=true;if(v<mn)mn=v;if(v>mx)mx=v;}
        last=u.plotted[k][n-1];char s[120];if(!u.visible[k])snprintf(s,sizeof(s),"скрыт");else if(last!=LV_CHART_POINT_NONE)snprintf(s,sizeof(s),"%s%.2f°",last<0?"−":"+",fabs(last/100.0));else snprintf(s,sizeof(s),"—");text(u.stats[k],s);
        if(found&&u.visible[k])snprintf(s,sizeof(s),"%.2f … %.2f · размах %.2f",mn/100.0,mx/100.0,(mx-mn)/100.0);else snprintf(s,sizeof(s),u.visible[k]?"данных нет":"включается касанием подписи");text(u.stats_mm[k],s);
    }
    for(unsigned i=0;i<9;i++){hide(u.ylabel[i],i>=ticks);if(i>=ticks)continue;double v=(u.ymax-(int)i*tick)/100.;char s[32];snprintf(s,sizeof(s),"%+.1f°",v);text(u.ylabel[i],s);lv_obj_set_y(u.ylabel[i],14+(int)(364*i/(ticks-1)));}
    for(unsigned i=0;i<7;i++){char s[24];snprintf(s,sizeof(s),i==6?"0 с":"−%u с",seconds-seconds*i/6);text(u.xlabel[i],s);}
    for(unsigned i=0;i<3;i++)role(u.ranges[i],i==u.range?PRIMARY:FRAME);
    lv_obj_set_style_line_color(u.chart,lv_color_hex(u.border),LV_PART_MAIN);lv_chart_refresh(u.chart);
}

static void build_measure(void){
    lv_obj_t *p=u.pages[MEASURE];u.hero=box(p,0,0,1144,128,NAV);lv_obj_set_style_radius(u.hero,18,0);
    lv_obj_set_style_bg_grad_dir(u.hero,LV_GRAD_DIR_HOR,0);lv_obj_set_style_bg_grad_color(u.hero,lv_color_hex(u.navsel),0);lv_obj_add_event_cb(u.hero,hero_draw,LV_EVENT_DRAW_MAIN,NULL);
    u.hero_date=label(u.hero,"—",28,16,520,24,F16,NAVTEXT);lv_obj_set_style_text_color(u.hero_date,lv_color_hex(0xC9D6EE),0);
    u.hero_dot=box(u.hero,28,54,18,18,ORANGE);lv_obj_set_style_radius(u.hero_dot,9,0);lv_obj_set_style_outline_width(u.hero_dot,6,0);lv_obj_set_style_outline_opa(u.hero_dot,70,0);lv_obj_set_style_outline_pad(u.hero_dot,0,0);
    u.hero_title=label(u.hero,"Накопление",60,40,494,56,F40,NAVTEXT);
    u.hero_sub=label(u.hero,"",28,96,830,28,F16,NAVTEXT);lv_obj_set_style_text_color(u.hero_sub,lv_color_hex(0xDCE5F5),0);
    for(int i=0;i<2;i++){u.hero_key[i]=label(u.hero,i?"Д2 − Д3 по Y":"Д2 − Д3 по X",560+i*128,24,126,24,F16,NAVTEXT);u.hero_kpi[i]=label(u.hero,"—",560+i*128,48,126,48,F32,NAVTEXT);lv_obj_set_style_text_color(u.hero_key[i],lv_color_hex(0xC9D6EE),0);}
    u.start[0]=button(u.hero,"● Старт записи",880,24,244,80,ORANGE,A_START);
    for(unsigned i=0;i<2;i++){
        lv_obj_t *c=u.sensor_card[i]=box(p,i*580,144,564,376,CARD);
        lv_obj_t *dot=box(c,20,26,14,14,ALT);lv_obj_set_style_radius(dot,7,0);lv_obj_set_style_bg_color(dot,lv_color_hex(u.sensor[i]),0);
        {lv_point_t z;lv_text_get_size(&z,"Датчик 2",F24,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);label(c,i?"Датчик 3":"Датчик 2",44,17,z.x+4,34,F24,TEXT);label(c,i?"адрес 3 · RS485":"адрес 2 · RS485",56+z.x,22,180,26,F16,MUTED);}
        u.health[i]=chip(c,"✓ На связи",364,16,180,OK);
        label(c,"X",20,100,30,36,F24,MUTED);label(c,"Y",20,192,30,36,F24,MUTED);
        for(int a=0;a<2;a++)u.angle[i][a]=label(c,"—",48,66+a*92,294,84,F64,TEXT);
        u.level[i]=box(c,344,58,200,200,TEXT);lv_obj_add_event_cb(u.level[i],level_draw,LV_EVENT_DRAW_MAIN,(void*)(intptr_t)i);
        label(u.level[i],"Y",106,4,20,20,F16,MUTED);label(u.level[i],"X",182,80,18,20,F16,MUTED);
        label(u.level[i],"5°",168,20,28,20,F16,MUTED);label(u.level[i],"2°",128,50,28,20,F16,MUTED);label(u.level[i],"1°",114,68,28,20,F16,MUTED);
        u.level_nodata[i]=label(u.level[i],"нет данных",18,86,164,28,F20,MUTED);lv_obj_set_style_text_align(u.level_nodata[i],LV_TEXT_ALIGN_CENTER,0);
        u.lost[i]=box(c,20,66,308,184,ERR);lv_obj_set_style_radius(u.lost[i],12,0);label(u.lost[i],"Нет ответа",16,14,276,36,F24,TEXT);u.lost_text[i]=label(u.lost[i],"",16,58,276,114,F16,TEXT);lv_obj_set_style_text_color(u.lost_text[i],lv_color_hex(u.err),0);lv_obj_set_style_text_color(lv_obj_get_child(u.lost[i],0),lv_color_hex(u.err),0);
        line(c,20,272,524);
        for(unsigned a=0;a<2;a++){
            label(c,a?"Качка Y":"Качка X",20,290+a*38,84,26,F16,MUTED);
            u.roll_bar[i][a]=lv_bar_create(c);lv_obj_remove_style_all(u.roll_bar[i][a]);lv_obj_set_pos(u.roll_bar[i][a],116,294+a*38);lv_obj_set_size(u.roll_bar[i][a],202,10);lv_bar_set_range(u.roll_bar[i][a],0,1000);role(u.roll_bar[i][a],NEUTRAL);lv_obj_set_style_radius(u.roll_bar[i][a],5,0);lv_obj_set_style_radius(u.roll_bar[i][a],5,LV_PART_INDICATOR);lv_obj_set_style_bg_opa(u.roll_bar[i][a],255,LV_PART_INDICATOR);
            u.roll_mark[i][a]=box(c,217,288+a*38,2,22,LINE);
            u.roll_value[i][a]=label(c,"—",330,286+a*38,84,30,F20,TEXT);lv_obj_set_style_text_align(u.roll_value[i][a],LV_TEXT_ALIGN_RIGHT,0);
            u.roll_chip[i][a]=chip(c,"нет данных",428,284+a*38,116,NEUTRAL);
        }
    }
    lv_obj_t *c=box(p,0,536,336,152,CARD);heading(c,5,"Ноль датчиков");u.zero_chip=chip(c,"не задан",224,16,90,NEUTRAL);lv_obj_set_style_text_letter_space(u.zero_chip,-1,0);
    button(c,"Обнулить",20,70,152,64,PRIMARY,A_ZERO);button(c,"Сбросить",184,70,132,64,OUTLINE,A_RESET);
    c=box(p,352,536,272,152,CARD);heading(c,6,"Носители");
    for(int i=0;i<3;i++)u.storage_lines[i]=label(c,"—",i<2?38:20,62+i*26,i<2?80:232,26,F16,i==2?MUTED:TEXT);
    for(int i=0;i<2;i++){u.storage_dot[i]=box(c,20,70+i*26,10,10,MUTED);lv_obj_set_style_radius(u.storage_dot[i],5,0);lv_obj_set_style_bg_opa(u.storage_dot[i],255,0);u.storage_detail[i]=label(c,"—",112,63+i*26,140,26,F16,MUTED);lv_obj_set_style_text_align(u.storage_detail[i],LV_TEXT_ALIGN_RIGHT,0);}
    c=box(p,640,536,256,152,CARD);heading(c,4,"Сеть");u.pc_chip=chip(c,"ПК",188,18,48,OK);
    for(int i=0;i<3;i++)u.network_lines[i]=label(c,"—",20,62+i*26,216,26,F16,i==2?MUTED:TEXT);
    c=box(p,912,536,232,152,CARD);heading(c,7,"Питание");for(int i=0;i<3;i++)u.power_lines[i]=label(c,"—",20,62+i*26,192,26,F16,i?MUTED:TEXT);
}
static void build_graph(void){
    lv_obj_t *p=u.pages[GRAPH];page_title(p,"График");
    const char *names[]={"Д2 · X","Д2 · Y","Д3 · X","Д3 · Y"};for(int i=0;i<4;i++){u.legend[i]=button(p,names[i],154+i*114,8,104,56,NEUTRAL,600+i);lv_obj_set_style_text_font(lv_obj_get_child(u.legend[i],0),F16M,0);lv_obj_set_style_radius(u.legend[i],28,0);u.visible[i]=true;for(int k=0;k<(i%2?2:1);k++){lv_obj_t *swatch=box(u.legend[i],12+k*12,27,i%2?8:20,3,ORANGE);lv_obj_set_style_bg_color(swatch,lv_color_hex(u.sensor[i/2]),0);}lv_obj_t *ll=lv_obj_get_child(u.legend[i],0);lv_obj_set_align(ll,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(ll,38,18);lv_obj_set_width(ll,62);lv_obj_set_style_text_align(ll,LV_TEXT_ALIGN_LEFT,0);}
    const char *ranges[]={"30 с","2 мин","10 мин"};for(int i=0;i<3;i++){u.ranges[i]=button(p,ranges[i],878+i*88,8,88,56,FRAME,610+i);lv_obj_set_style_text_font(lv_obj_get_child(u.ranges[i],0),F20,0);}
    u.range=1;lv_obj_t *card=box(p,0,88,1144,440,CARD);
    u.chart=lv_chart_create(card);lv_obj_remove_style_all(u.chart);lv_obj_set_pos(u.chart,76,24);lv_obj_set_size(u.chart,1040,364);lv_obj_set_style_line_width(u.chart,1,LV_PART_MAIN);lv_obj_set_style_line_color(u.chart,lv_color_hex(u.border),LV_PART_MAIN);lv_obj_set_style_line_width(u.chart,2,LV_PART_ITEMS);lv_obj_set_style_size(u.chart,0,0,LV_PART_INDICATOR);lv_obj_set_style_pad_all(u.chart,0,0);lv_obj_remove_flag(u.chart,LV_OBJ_FLAG_SCROLLABLE);
    lv_chart_set_type(u.chart,LV_CHART_TYPE_LINE);lv_chart_set_point_count(u.chart,240);
    for(unsigned i=0;i<4;i++){u.series[i]=lv_chart_add_series(u.chart,lv_color_hex(u.sensor[i/2]),LV_CHART_AXIS_PRIMARY_Y);for(unsigned j=0;j<1200;j++)u.plotted[i][j]=LV_CHART_POINT_NONE;}
    lv_obj_add_flag(u.chart,LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);lv_obj_add_event_cb(u.chart,chart_draw_task,LV_EVENT_DRAW_TASK_ADDED,NULL);
    lv_obj_add_flag(u.chart,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(u.chart,chart_touch,LV_EVENT_PRESSED,NULL);lv_obj_add_event_cb(u.chart,chart_touch,LV_EVENT_PRESSING,NULL);
    for(int i=0;i<9;i++)u.ylabel[i]=label(card,"",8,14+i*45,58,22,F16,MUTED);
    for(int i=0;i<7;i++){u.xlabel[i]=label(card,"",36+i*173,400,80,24,F16,MUTED);lv_obj_set_style_text_align(u.xlabel[i],LV_TEXT_ALIGN_CENTER,0);}
    u.cursor_label=label(card,"Коснитесь графика для отсчёта",90,2,1000,22,F16,MUTED);
    for(int i=0;i<4;i++){card=box(p,i*290,544,274,144,CARD);lv_obj_t *dot=box(card,20,27,12,12,ORANGE);lv_obj_set_style_radius(dot,6,0);lv_obj_set_style_bg_color(dot,lv_color_hex(u.sensor[i/2]),0);label(card,names[i],42,20,212,28,F20,TEXT);u.stats[i]=label(card,"—",20,58,234,48,F32,TEXT);u.stats_mm[i]=label(card,"данных нет",20,108,234,28,F16,MUTED);}
}
static void build_record(void){
    lv_obj_t *p=u.pages[RECORD];int tx=page_title(p,"Запись");u.rec_chip=chip(p,"остановлена",tx,22,270,NEUTRAL);fit_chip(u.rec_chip);u.start[1]=button(p,"● Старт записи",884,4,260,64,ORANGE,A_START);
    for(int i=0;i<2;i++){lv_obj_t *c=box(p,i*580,88,564,236,CARD);heading(c,i?11:6,i?"Флешка USB":"Карта microSD");u.media_chip[i]=chip(c,"—",364,16,180,NEUTRAL);
        u.capacity_text[i]=label(c,"свободное место: нет данных",20,68,524,28,F20,TEXT);u.capacity_bar[i]=lv_bar_create(c);lv_obj_set_pos(u.capacity_bar[i],20,98);lv_obj_set_size(u.capacity_bar[i],524,12);lv_bar_set_range(u.capacity_bar[i],0,1000);lv_obj_set_style_bg_color(u.capacity_bar[i],lv_color_hex(u.alt),0);lv_obj_set_style_bg_color(u.capacity_bar[i],lv_color_hex(u.action),LV_PART_INDICATOR);
        u.media_detail[i]=label(c,"данных нет",20,130,524,82,F16,MUTED);
    }
    u.history=box(p,0,340,1144,348,CARD);heading(u.history,9,"Последние записи");u.history_note=label(u.history,"файлы: программа ПК → «Файлы прибора»",566,22,558,28,F16,MUTED);
    const char *cols[]={"Файл","Начало","Длительность","Строк","Носители","Состояние"};int x[]={20,280,480,630,760,910},w[]={250,190,140,120,140,214};
    for(int j=0;j<6;j++)label(u.history,cols[j],x[j],70,w[j],24,F16,MUTED);
    for(int i=0;i<4;i++){line(u.history,0,96+i*56,1144);for(int j=0;j<6;j++)u.history_rows[i][j]=label(u.history,"",x[j],112+i*56,w[j],32,F16,TEXT);}
    u.history_rows[4][0]=label(u.history,"данных нет",20,222,1104,30,F20,MUTED);lv_obj_set_style_text_align(u.history_rows[4][0],LV_TEXT_ALIGN_CENTER,0);
}
static void stepper(lv_obj_t *c,unsigned i,int y){
    label(c,param_name[i],20,y+7,258,30,F20M,TEXT);label(c,limits[i],20,y+38,258,24,F16,MUTED);
    lv_obj_t *b=button(c,"−",286,y,56,56,OUTLINE,200+i*2);lv_obj_set_style_radius(b,12,0);lv_obj_add_event_cb(b,step_press,LV_EVENT_PRESSED,(void*)(intptr_t)(200+i*2));lv_obj_add_event_cb(b,step_press,LV_EVENT_RELEASED,NULL);lv_obj_add_event_cb(b,step_press,LV_EVENT_PRESS_LOST,NULL);
    u.step_value[i]=button(c,"",350,y,128,56,FRAME,300+i);lv_obj_set_style_radius(u.step_value[i],12,0);
    b=button(c,"+",486,y,56,56,OUTLINE,201+i*2);lv_obj_set_style_radius(b,12,0);lv_obj_add_event_cb(b,step_press,LV_EVENT_PRESSED,(void*)(intptr_t)(201+i*2));lv_obj_add_event_cb(b,step_press,LV_EVENT_RELEASED,NULL);lv_obj_add_event_cb(b,step_press,LV_EVENT_PRESS_LOST,NULL);
}
static void build_settings(void){
    lv_obj_t *p=u.pages[SETTINGS];int tx=page_title(p,"Настройки");u.changed=chip(p,"изменено: 0",tx,22,278,INFO);fit_chip(u.changed);
    u.set_cancel=button(p,"Отменить",732,8,180,56,GHOST,A_CANCEL);u.set_apply=button(p,"Применить",924,8,220,56,PRIMARY,A_APPLY);
    lv_obj_t *c=box(p,0,88,564,600,CARD);heading(c,7,"Опрос датчиков");stepper(c,0,68);line(c,20,132,524);stepper(c,1,144);line(c,20,208,524);stepper(c,2,220);
    icon(c,1,20,310,22,MUTED);label(c,"Качка и покой",52,309,460,32,F20M,TEXT);stepper(c,3,356);line(c,20,420,524);stepper(c,4,432);
    c=box(p,580,88,564,268,CARD);heading(c,1,"Качка — дополнительно");stepper(c,5,68);line(c,20,132,524);stepper(c,6,144);
    c=box(p,580,372,564,316,CARD);heading(c,8,"Прибор");label(c,"Тема",20,79,200,32,F20M,TEXT);
    lv_obj_t *seg=box(c,316,62,228,64,FRAME);lv_obj_set_style_radius(seg,12,0);u.theme_btn[0]=button(seg,"Светлая",4,4,108,56,FRAME,A_LIGHT);u.theme_btn[1]=button(seg,"Тёмная",114,4,108,56,FRAME,A_DARK);for(int i=0;i<2;i++){lv_obj_set_style_text_font(lv_obj_get_child(u.theme_btn[i],0),F20M,0);lv_obj_set_style_radius(u.theme_btn[i],9,0);}
    line(c,20,132,524);label(c,"Дата и время",20,150,320,28,F20M,TEXT);u.clock_text=label(c,"—",20,178,350,24,F16,MUTED);lv_obj_t *b=button(c,"Изменить",392,144,150,56,OUTLINE,A_TIME);lv_obj_set_style_text_font(lv_obj_get_child(b,0),F20M,0);
    line(c,20,208,524);label(c,"Экран и прошивка",20,226,524,28,F20M,TEXT);u.panel_text=label(c,"—",20,254,524,24,F16,MUTED);u.fw_text=label(c,"—",20,278,524,24,F16,MUTED);
}
static void build_network(void){
    lv_obj_t *p=u.pages[NETWORK];page_title(p,"Сеть");u.reveal=button(p,"Показать пароли",692,8,220,56,GHOST,A_REVEAL);lv_obj_set_style_text_font(lv_obj_get_child(u.reveal,0),F20M,0);u.net_apply=button(p,"Применить",924,8,220,56,PRIMARY,A_NET_APPLY);
    for(int i=0;i<2;i++){
        lv_obj_t *c=box(p,i*580,88,564,380,CARD);heading(c,4,i?"Подключение к Wi-Fi":"Точка доступа прибора");
        u.net_switch[i]=lv_switch_create(c);lv_obj_set_pos(u.net_switch[i],470,12);lv_obj_set_size(u.net_switch[i],72,40);lv_obj_set_ext_click_area(u.net_switch[i],8);lv_obj_add_event_cb(u.net_switch[i],switch_changed,LV_EVENT_VALUE_CHANGED,NULL);
        for(int j=0;j<2;j++){unsigned k=i*2+j;lv_obj_t *b=u.net_fields[k]=button(c,"",20,70+j*68,524,56,FRAME,500+k);lv_obj_delete(lv_obj_get_child(b,0));label(b,j?"Пароль":i?"Сеть":"Имя сети",16,17,96,24,F16,MUTED);lv_obj_t *v=label(b,"",122,14,j?340:382,30,F20,TEXT);lv_label_set_long_mode(v,LV_LABEL_LONG_SCROLL_CIRCULAR);if(j)icon(b,12,478,15,26,MUTED);}
        lv_obj_t *row=box(c,20,206,524,56,FRAME);label(row,"Адрес",16,17,96,24,F16,MUTED);u.net_address[i]=label(row,"—",122,14,254,30,F20,TEXT);u.net_chip[i]=chip(row,"—",382,12,126,NEUTRAL);
        label(c,i?"пароль пустой или 8–63 символа · без Wi-Fi прибор работает через точку доступа":"пароль точки доступа служит ключом для программы ПК · 8–63 символа",20,326,524,44,F16,MUTED);
    }
    lv_obj_t *c=box(p,0,484,1144,204,CARD);heading(c,8,"Подключение программы для ПК");u.tcp_chip=chip(c,"клиент —",894,16,230,NEUTRAL);
    label(c,"в программе: порт →",20,72,350,26,F16,MUTED);label(c,"Tab5 по сети (TCP)",20,100,350,34,F20,TEXT);
    label(c,"адрес и порт",394,72,350,26,F16,MUTED);u.tcp_address=label(c,"—",394,100,350,34,F20,TEXT);
    label(c,"ключ доступа",768,72,356,26,F16,MUTED);label(c,"пароль точки доступа",768,100,356,34,F20,TEXT);
    u.net_hint=label(c,"по USB-C прибор виден как COM-порт; ключ не нужен",20,150,1104,40,F16,MUTED);
}
void tab5_ui_init(tab5_ui_action_cb_t cb,void *user){
    if(u.root)tab5_ui_destroy();
    memset(&u,0,sizeof(u));u.cb=cb;u.user=user;palette(true);
    u.root=box(lv_screen_active(),0,0,1280,720,BG);u.rail=box(u.root,0,0,104,720,NAV);
    icon(u.rail,10,35,18,34,NAVTEXT);{lv_obj_t *t=label(u.rail,"КРЕНОМЕР",0,58,104,20,F16M,NAVMUTED);lv_obj_set_style_text_align(t,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_letter_space(t,1,0);}box(u.rail,0,87,104,1,NAVSEL);
    const char *names[]={"Измерения","График","Запись","Настройки","Сеть"};
    for(unsigned i=0;i<5;i++){u.nav[i]=button(u.rail,"",0,88+i*88,104,88,NAV,100+i);lv_obj_delete(lv_obj_get_child(u.nav[i],0));icon(u.nav[i],i,36,16,32,NAVTEXT);lv_obj_t *l=label(u.nav[i],names[i],0,56,104,24,F16,NAVTEXT);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);u.selected[i]=box(u.nav[i],0,14,5,60,ORANGE);lv_obj_set_style_radius(u.selected[i],3,0);}
    u.badge=box(u.nav[0],68,14,12,12,RED);lv_obj_set_style_radius(u.badge,6,0);
    u.rail_time=label(u.rail,"—",0,654,104,34,F24,NAVTEXT);lv_obj_set_style_text_align(u.rail_time,LV_TEXT_ALIGN_CENTER,0);u.rail_power=label(u.rail,"—",32,690,70,24,F16,NAVMUTED);icon(u.rail,7,13,692,18,NAVMUTED);box(u.rail,0,640,104,1,NAVSEL);
    for(unsigned i=0;i<5;i++)u.pages[i]=box(u.root,120,16,1144,688,TEXT);
    build_measure();build_graph();build_record();build_settings();build_network();show_page(MEASURE);
    tab5_ui_state_t s={.light_theme=true};tab5_measure_config_default(&s.measurement.config);tab5_ui_update(&s);
}

static void elapsed(char *dest,size_t n,uint64_t seconds){snprintf(dest,n,"%02u:%02u:%02u",(unsigned)(seconds/3600),(unsigned)(seconds/60%60),(unsigned)(seconds%60));}
static uint64_t duration(void){if(u.state.rec_duration_known)return u.state.rec_duration_s;if(u.state.rec_known&&u.state.now_known&&u.state.now_us>=u.state.rec_started_us)return (u.state.now_us-u.state.rec_started_us)/1000000;return 0;}
static const char *medium_status(const tab5_ui_medium_t *m){return !m->known?"данных нет":m->failed?"✗ ошибка":m->recording?"● запись":m->mounted?"✓ готов":"не подключён";}
static void update_record(void){
    char s[240],d[40];elapsed(d,sizeof(d),duration());snprintf(s,sizeof(s),u.state.recording?"● идёт · %s":"остановлена",u.state.rec_known?d:"—");text(u.rec_chip,s);role(u.rec_chip,u.state.recording?ERR:NEUTRAL);
    const tab5_ui_medium_t *m[]={&u.state.sd,&u.state.usb};
    for(int i=0;i<2;i++){
        const tab5_ui_capacity_t *c=&u.state.capacity[i];text(u.media_chip[i],medium_status(m[i]));role(u.media_chip[i],!m[i]->known?NEUTRAL:m[i]->failed?ERR:m[i]->recording?ERR:m[i]->mounted?OK:WARN);
        if(c->known&&c->total_bytes){snprintf(s,sizeof(s),"свободно %.2f ГБ из %.2f ГБ",c->free_bytes/1073741824.,c->total_bytes/1073741824.);lv_bar_set_value(u.capacity_bar[i],(int32_t)(1000.0*(c->total_bytes-fmin((double)c->free_bytes,(double)c->total_bytes))/c->total_bytes),LV_ANIM_OFF);}else snprintf(s,sizeof(s),"свободное место: нет данных");text(u.capacity_text[i],s);hide(u.capacity_bar[i],!c->known||!c->total_bytes);
        char estimate[64];if(c->estimate_known)snprintf(estimate,sizeof(estimate),"примерно %u ч записи",(unsigned)(c->remaining_s/3600));else snprintf(estimate,sizeof(estimate),"оценка времени: данных нет");
        if(m[i]->known&&!m[i]->mounted)snprintf(s,sizeof(s),"Подключите носитель FAT32.\nДостаточно одного исправного носителя.\n%s",estimate);
        else snprintf(s,sizeof(s),"файловая система: %s\nпапка: %s\n%s",c->details_known?c->filesystem:"—",c->details_known?c->folder:"—",estimate);
        text(u.media_detail[i],s);
        lv_obj_set_style_bg_color(u.capacity_bar[i],lv_color_hex(u.alt),0);lv_obj_set_style_bg_color(u.capacity_bar[i],lv_color_hex(u.action),LV_PART_INDICATOR);
    }
    unsigned count=u.state.recent_count;if(count>4)count=4;
    text(u.history_note,u.state.recent_known&&u.state.recent_partial?"История частичная: текущий сеанс":"файлы: программа ПК → «Файлы прибора»");
    for(unsigned i=0;i<4;i++){
        bool known=u.state.recent_known&&i<count&&u.state.recent[i].known;for(int j=0;j<6;j++)hide(u.history_rows[i][j],!known);if(!known)continue;
        const tab5_ui_recent_t *r=&u.state.recent[i];text(u.history_rows[i][0],r->name);
        if(r->start_known){time_t t=(time_t)r->start_unix;struct tm *tm=localtime(&t);if(tm)strftime(s,sizeof(s),"%d.%m %H:%M:%S",tm);else snprintf(s,sizeof(s),"—");}else snprintf(s,sizeof(s),"—");text(u.history_rows[i][1],s);
        elapsed(s,sizeof(s),r->duration_s);text(u.history_rows[i][2],s);snprintf(s,sizeof(s),"%" PRIu64,r->rows);text(u.history_rows[i][3],s);text(u.history_rows[i][4],r->media_mask==3?"SD · USB":r->media_mask==1?"SD":r->media_mask==2?"USB":"—");text(u.history_rows[i][5],r->closed_ok?"✓ закрыт":"⚠ не закрыт");role(u.history_rows[i][5],r->closed_ok?OK:WARN);
    }
    hide(u.history_rows[4][0],u.state.recent_known&&count>0);text(u.history_rows[4][0],u.state.recent_known?"Записей в текущем сеансе пока нет":"История записей появится после подключения носителя");
}
static void update_measure(void){
    const tab5_ui_state_t *s=&u.state;char b[360],d[40];bool any=s->measurement.sensor[0].valid||s->measurement.sensor[1].valid,both=s->measurement.sensor[0].valid&&s->measurement.sensor[1].valid;
    unsigned fill=s->measurement.sensor[0].roll_fill_s;if(s->measurement.sensor[1].roll_fill_s<fill)fill=s->measurement.sensor[1].roll_fill_s;
    float roll=0;for(int i=0;i<2;i++)if(s->measurement.sensor[i].valid){roll=fmaxf(roll,s->measurement.sensor[i].roll_x);roll=fmaxf(roll,s->measurement.sensor[i].roll_y);}
    text(u.hero_date,s->clock.known&&s->clock.time_valid?s->clock.datetime:"Установите часы");
    if(!any){text(u.hero_title,"Нет датчиков");text(u.hero_sub,"проверьте питание и RS485");}
    else if(s->recording){elapsed(d,sizeof(d),duration());snprintf(b,sizeof(b),"Запись · %s",s->rec_known?d:"—");text(u.hero_title,b);snprintf(b,sizeof(b),"%s · %" PRIu64 " строк · %s",s->rec_known?s->rec_file:"файл —",s->sd.recording?s->sd.written:s->usb.written,s->sd.recording&&s->usb.recording?"SD и USB":s->sd.recording?"SD":s->usb.recording?"USB":"носители —");text(u.hero_sub,b);}
    else {text(u.hero_title,s->measurement.stable?"Покой":fill<s->measurement.config.roll_window_s?"Накопление":"Качка");snprintf(b,sizeof(b),"%s · размах %.2f° · порог %.2f° · окно %u/%u с",s->measurement.stable?"можно снимать отсчёт":"дождитесь покоя",(double)roll,(double)s->measurement.config.roll_calm_deg,fill,s->measurement.config.roll_window_s);text(u.hero_sub,b);}
    {uint32_t dc=!any?u.err:s->recording?0xFF5A5F:s->measurement.stable?0xB8E039:0xF2B84B;lv_obj_set_style_bg_color(u.hero_dot,lv_color_hex(dc),0);lv_obj_set_style_outline_color(u.hero_dot,lv_color_hex(dc),0);}
    for(unsigned i=0;i<2;i++){
        hide(u.hero_kpi[i],s->recording&&i==1);hide(u.hero_key[i],s->recording&&i==1);
        text(u.hero_key[i],s->recording?"Состояние":i?"Д2 − Д3 по Y":"Д2 − Д3 по X");
        if(s->recording)snprintf(b,sizeof(b),s->measurement.stable?"ПОКОЙ":"КАЧКА %.2f°",(double)roll);
        else if(both){float v=i?s->measurement.sensor[0].y-s->measurement.sensor[1].y:s->measurement.sensor[0].x-s->measurement.sensor[1].x;snprintf(b,sizeof(b),"%s%.2f°",v<0?"−":"+",fabsf(v));}else snprintf(b,sizeof(b),"—");
        lv_point_t kp;lv_text_get_size(&kp,b,F32,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);lv_obj_set_style_text_font(u.hero_kpi[i],!s->recording&&kp.x>126?F20:F32,0);text(u.hero_kpi[i],b);lv_obj_set_width(u.hero_kpi[i],s->recording?292:126);
    }
    const char *errors[]={"нет","таймаут","CRC","кадр","исключение","ввод-вывод"};
    for(unsigned i=0;i<2;i++){
        const tab5_measure_sensor_t *m=&s->measurement.sensor[i];bool valid=m->valid&&isfinite(m->x)&&isfinite(m->y);
        text(u.health[i],!valid?"✗ Отключён":m->fail_streak?"⚠ Пропуск ответа":"✓ На связи");role(u.health[i],!valid?ERR:m->fail_streak?WARN:OK);
        hide(u.lost[i],valid);hide(u.level_nodata[i],valid);lv_obj_set_style_text_color(u.lost_text[i],lv_color_hex(u.err),0);lv_obj_set_style_text_color(lv_obj_get_child(u.lost[i],0),lv_color_hex(u.err),0);lv_obj_set_style_bg_color(lv_obj_get_child(u.sensor_card[i],0),lv_color_hex(u.sensor[i]),0);
        for(int a=0;a<2;a++){hide(u.angle[i][a],!valid);float v=a?m->y:m->x;snprintf(b,sizeof(b),"%s%.2f°",v<0?"−":"+",fabsf(v));text(u.angle[i][a],b);}
        if(!valid){char last[40]="—";if(m->good&&s->now_known&&s->datetime_known&&m->timestamp_us<=s->now_us){unsigned daysec=s->datetime.hour*3600+s->datetime.minute*60+s->datetime.second;uint64_t age=(s->now_us-m->timestamp_us)/1000000;unsigned t=(daysec+86400-(unsigned)(age%86400))%86400;elapsed(last,sizeof(last),t);}snprintf(b,sizeof(b),"%u запросов подряд без ответа\nпоследнее значение: %s\nошибок: %" PRIu64 " · %s\nпроверьте питание и RS485",m->fail_streak,last,m->errors,errors[m->last_error<=TAB5_MEASURE_IO?m->last_error:0]);text(u.lost_text[i],b);}
        for(int a=0;a<2;a++){float v=a?m->roll_y:m->roll_x;bool calm=a?m->calm_y:m->calm_x;unsigned value=valid&&s->measurement.config.roll_calm_deg>0?(unsigned)fminf(1000,fmaxf(0,1000*v/(2*s->measurement.config.roll_calm_deg))):0;lv_bar_set_value(u.roll_bar[i][a],value,LV_ANIM_OFF);lv_obj_set_style_bg_color(u.roll_mark[i][a],lv_color_hex(u.dim),0);lv_obj_set_style_bg_color(u.roll_bar[i][a],lv_color_hex(calm?u.ok:u.warn),LV_PART_INDICATOR);if(valid)snprintf(b,sizeof(b),"%.2f°",(double)v);else snprintf(b,sizeof(b),"—");text(u.roll_value[i][a],b);text(u.roll_chip[i][a],valid?(calm?"● покой":"▲ качка"):"нет данных");role(u.roll_chip[i][a],valid?(calm?OK:WARN):NEUTRAL);}
        lv_obj_invalidate(u.level[i]);
    }
    bool nonzero=false;float zero[4]={s->measurement.sensor[0].zero_x,s->measurement.sensor[0].zero_y,s->measurement.sensor[1].zero_x,s->measurement.sensor[1].zero_y};bool changed=false;
    for(int i=0;i<4;i++){nonzero|=fabsf(zero[i])>.000001;changed|=fabsf(zero[i]-u.zero[i])>.000001;u.zero[i]=zero[i];}
    if(u.zero_seen&&changed){u.zero_time_known=s->datetime_known;if(u.zero_time_known)snprintf(u.zero_time,sizeof(u.zero_time),"%02u:%02u",s->datetime.hour,s->datetime.minute);}u.zero_seen=true;
    if(nonzero)snprintf(b,sizeof(b),"задан %s",u.zero_time_known?u.zero_time:"—");else snprintf(b,sizeof(b),"не задан");text(u.zero_chip,b);role(u.zero_chip,nonzero?INFO:NEUTRAL);
    const tab5_ui_medium_t *media[]={&s->sd,&s->usb};for(unsigned i=0;i<2;i++){const tab5_ui_medium_t *m=media[i];text(u.storage_lines[i],i?"USB":"microSD");
        if(m->known&&m->recording)snprintf(b,sizeof(b),"%" PRIu64 " строк",m->written);else if(m->known&&m->mounted&&!m->failed&&s->capacity[i].known)snprintf(b,sizeof(b),"готов · %.1f ГБ",s->capacity[i].free_bytes/1073741824.);
        else snprintf(b,sizeof(b),"%s",!m->known?"данных нет":m->failed?"ошибка":m->mounted?"готов":"не подключён");
        text(u.storage_detail[i],b);
        lv_obj_set_style_bg_color(u.storage_dot[i],lv_color_hex(!m->known?u.muted:m->failed?u.err:m->recording?0xFF5A5F:m->mounted?u.ok:u.muted),0);}
    text(u.storage_lines[2],s->sd.recording&&s->usb.recording?"пишутся оба носителя":s->sd.recording?"пишется только SD":s->usb.recording?"пишется только USB":"запись выключена");
    snprintf(b,sizeof(b),"AP %s",s->link.known&&s->link.ap_started?s->link.ap_ip:"—");text(u.network_lines[0],b);text(u.network_lines[1],!s->link.known?"STA —":s->link.sta_connected?"STA подключено":"STA отключено");if(s->tcp_clients_known)snprintf(b,sizeof(b),"TCP %u · клиентов %u",s->link.tcp_port,s->tcp_clients);else snprintf(b,sizeof(b),"TCP %u · клиентов —",s->link.tcp_port);text(u.network_lines[2],b);hide(u.pc_chip,!s->tcp_clients_known||!s->tcp_clients);
    if(!s->clock.known)snprintf(b,sizeof(b),"данных нет");else if(s->clock.battery_present&&isfinite(s->clock.battery_v))snprintf(b,sizeof(b),"батарея %.2f В",(double)s->clock.battery_v);else snprintf(b,sizeof(b),"внешнее питание");text(u.power_lines[0],b);lv_obj_set_style_text_letter_space(u.power_lines[1],-1,0);text(u.power_lines[1],!s->clock.known?"—":s->clock.battery_present?"":"аккумулятора нет");text(u.power_lines[2],!s->clock.known?"часы: —":s->clock.time_valid?"часы установлены":"установите часы");
    hide(u.badge,both&&!s->sd.failed&&!s->usb.failed);
}
/* Only typed action results acknowledge/reject persistence; status is display-only. */
static bool backend_failure(void){return u.state.action_result_known&&!u.state.action_result_success;}
static void update_result(void){
    const tab5_ui_state_t *s=&u.state;
    if(!s->action_result_known||(u.result_seen&&u.result_serial==s->action_result_serial))return;
    u.result_seen=true;u.result_serial=s->action_result_serial;
    /* Another completed action may repeat exactly the same notice after dismissal. */
    u.last_notice[0]=0;
    if(s->action_result_kind==TAB5_UI_SETTINGS){
        u.settings_failed=!s->action_result_success;u.settings_ack=s->action_result_success;
        if(u.settings_failed){u.pending=false;u.dirty=true;}
    }else if(s->action_result_kind==TAB5_UI_NETWORK){
        u.network_failed=!s->action_result_success;u.network_ack=s->action_result_success;
        if(u.network_failed){u.net_pending=false;u.net_dirty=true;text(u.net_hint,s->status);role(u.net_hint,ERR);}
    }
}
static void update_notice(const char *s){
    if(!strcmp(u.last_notice,s))return;
    snprintf(u.last_notice,sizeof(u.last_notice),"%s",s);
    if(!*s){if(u.notice)lv_obj_delete(u.notice);u.notice=u.notice_text=NULL;return;}
    bool error=backend_failure();
    if(!u.notice){u.notice=box(u.root,240,616,1024,88,INFO);lv_obj_add_flag(u.notice,LV_OBJ_FLAG_CLICKABLE);u.notice_text=label(u.notice,"",20,14,912,64,F20,TEXT);button(u.notice,"✗",948,16,56,56,GHOST,A_NOTICE_CLOSE);}
    role(u.notice,error?ERR:INFO);lv_obj_set_style_text_color(u.notice_text,lv_color_hex(error?u.err:u.info),0);text(u.notice_text,s);lv_obj_move_foreground(u.notice);
}
void tab5_ui_update(const tab5_ui_state_t *s){
    if(!u.root||!s)return;
    tab5_ui_state_t state=*s;u.state=state;
    tab5_ui_graph_sample(&u.graph,&state);
    if(u.light!=state.light_theme){palette(state.light_theme);lv_obj_set_style_bg_grad_color(u.hero,lv_color_hex(u.navsel),0);}
    update_result();update_notice(state.status);if(u.notice_text)lv_obj_set_style_text_color(u.notice_text,lv_color_hex(backend_failure()?u.err:u.info),0);
    if(u.pending&&!u.settings_failed&&(!state.action_result_known||u.settings_ack)&&cfg_equal(&state.measurement.config,&u.sent)){tab5_measure_config_t current=draft_config();u.pending=false;u.dirty=!cfg_equal(&current,&u.sent);}
    if(!u.dirty&&!u.pending)config_values(&state.measurement.config,u.draft);
    if(u.net_pending&&!u.network_failed&&(!state.action_result_known||u.network_ack)&&net_equal(&state.network,&u.net_sent)){u.net_pending=false;u.net_dirty=!net_equal(&u.net_draft,&u.net_sent);text(u.net_hint,"Сеть: настройки применены");role(u.net_hint,MUTED);}
    if(!u.net_dirty&&!u.net_pending&&u.modal_kind!=2)u.net_draft=state.network;
    refresh_settings();refresh_network();
    for(unsigned i=0;i<2;i++){
        role(u.theme_btn[i],state.light_theme==(i==0)?PRIMARY:DIM);
        lv_obj_set_style_bg_color(u.net_switch[i],lv_color_hex(u.action),LV_PART_INDICATOR|LV_STATE_CHECKED);lv_obj_set_style_bg_color(u.net_switch[i],lv_color_hex(u.light?0xFFFFFF:0x0B1733),LV_PART_KNOB);
        text(u.net_address[i],!state.link.known?"—":i?(state.link.sta_connected?state.link.sta_ip:"—"):(state.link.ap_started?state.link.ap_ip:"—"));bool on=state.link.known&&(i?state.link.sta_connected:state.link.ap_started);text(u.net_chip[i],on?(i?"✓ подключено":"✓ работает"):"—");role(u.net_chip[i],on?OK:NEUTRAL);
    }
    char b[240];if(state.tcp_peer_known)snprintf(b,sizeof(b),"✓ клиент %s",state.tcp_peer);else if(state.tcp_clients_known)snprintf(b,sizeof(b),"клиентов %u · адрес —",state.tcp_clients);else snprintf(b,sizeof(b),"клиент: данных нет");text(u.tcp_chip,b);role(u.tcp_chip,state.tcp_clients_known&&state.tcp_clients?OK:NEUTRAL);
    if(state.link.known)snprintf(b,sizeof(b),"%s : %u",state.link.sta_connected?state.link.sta_ip:state.link.ap_started?state.link.ap_ip:"—",state.link.tcp_port);else snprintf(b,sizeof(b),"—");text(u.tcp_address,b);
    text(u.clock_text,state.clock.known&&state.clock.time_valid?state.clock.datetime:"Установите часы");text(u.panel_text,state.panel[0]?state.panel:"Экран: автоматический выбор");snprintf(b,sizeof(b),"Прошивка: %s",state.firmware[0]?state.firmware:"—");text(u.fw_text,b);
    if(state.datetime_known)snprintf(b,sizeof(b),"%02u:%02u",state.datetime.hour,state.datetime.minute);else snprintf(b,sizeof(b),"—");text(u.rail_time,b);text(u.rail_power,!state.clock.known?"—":state.clock.battery_present?"батарея":"внешнее");
    for(unsigned i=0;i<2;i++){caption(u.start[i],state.recording?"■ Стоп записи":"● Старт записи");role(u.start[i],state.recording?RED:ORANGE);}
    uint64_t now=state.now_known?state.now_us:(uint64_t)lv_tick_get()*1000;if(!u.rendered||now<u.render_us||now-u.render_us>=100000){update_measure();update_record();if(u.page==GRAPH)chart_refresh();u.rendered=true;u.render_us=now;}
    if(u.keyboard)keyboard_style(u.keyboard);
}
void tab5_ui_destroy(void){if(u.repeat)lv_timer_delete(u.repeat);if(u.root)lv_obj_delete(u.root);if(u.styles_ready)for(int i=0;i<STYLE_COUNT;i++)lv_style_reset(&u.style[i]);memset(&u,0,sizeof(u));}
