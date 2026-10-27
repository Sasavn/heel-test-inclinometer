/* Native RGB565 UI for tab5-feedback/2026-10-22-ui-design/SPEC.md.
 * Fixed 1280x720 geometry. Main owns hardware, validation and persistence. */
#include "tab5_ui.h"
#include "native/native_ui.h"
#include "tab5_ui_graph.h"
#include "tab5_plot.h"
#include "tab5_level.h"
#include "tab5_time.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
NUI_FONT_DECLARE(font_tab5_16);NUI_FONT_DECLARE(font_tab5_16m);NUI_FONT_DECLARE(font_tab5_20);NUI_FONT_DECLARE(font_tab5_20m);
NUI_FONT_DECLARE(font_tab5_24);NUI_FONT_DECLARE(font_tab5_32);
NUI_FONT_DECLARE(font_tab5_40);NUI_FONT_DECLARE(font_tab5_64);
NUI_FONT_DECLARE(font_tab5_28);NUI_FONT_DECLARE(font_tab5_36);NUI_FONT_DECLARE(font_tab5_48);NUI_FONT_DECLARE(font_tab5_56);NUI_FONT_DECLARE(font_tab5_108);
#define F16 (&font_tab5_16)
#define F16M (&font_tab5_16m)
#define F20 (&font_tab5_20)
#define F20M (&font_tab5_20m)
#define F24 (&font_tab5_24)
#define F32 (&font_tab5_32)
#define F40 (&font_tab5_40)
#define F64 (&font_tab5_64)
#define F28 (&font_tab5_28)
#define F36 (&font_tab5_36)
#define F48 (&font_tab5_48)
#define F56 (&font_tab5_56)
#define F108 (&font_tab5_108)
enum {BG,CARD,ALT,FRAME,TEXT,DIM,MUTED,NAV,NAVSEL,NAVTEXT,NAVMUTED,PRIMARY,ORANGE,RED,OUTLINE,GHOST,OK,WARN,ERR,INFO,NEUTRAL,LINE,STYLE_COUNT};
enum {MEASURE,ROLL,GRAPH,RECORD,SETTINGS,NETWORK};
enum {A_START=1,A_ZERO,A_RESET,A_APPLY,A_CANCEL,A_REVEAL,A_NET_APPLY,A_LIGHT,A_DARK,A_TIME,A_NUM_OK,A_MODAL_CANCEL,A_CONFIRM,A_DISCARD,A_STAY,A_LANG,A_TEXT_OK,A_TIME_OK,A_NOTICE_CLOSE,A_TIME_SYNC,A_TZ_SELECT,A_TZ_MINUS,A_TZ_PLUS};
static struct {
    nui_obj_t *notice,*notice_text;char last_notice[160];
    nui_obj_t *root,*rail,*pages[6],*nav[6],*selected[6],*badge,*rail_time,*rail_power,*rail_resources[3];
    nui_obj_t *status_bar,*status_rec,*status_media,*measure_roll[2],*roll_window,*roll_calm,*roll_status[2];
    nui_obj_t *settings_view,*settings_content,*setting_rows[11],*brightness_value;
    nui_obj_t *network_view,*network_content,*net_rows[8],*rec_file_label,*rec_detail_label;
    nui_style_t style[STYLE_COUNT];bool styles_ready,light;
    uint32_t bg,card,alt,frame,border,text,dim,muted,navc,navsel,action,actiontext,ok,warn,err,info,okbg,warnbg,errbg,infobg,sensor[2];
    nui_obj_t *hero,*hero_date,*hero_title,*hero_sub,*hero_dot,*hero_kpi[2],*hero_key[2],*start[2];
    nui_obj_t *sensor_card[2],*angle[2][2],*health[2],*lost[2],*lost_text[2],*level[2],*level_nodata[2],*roll_bar[2][2],*roll_mark[2][2],*roll_value[2][2],*roll_chip[2][2];
    nui_obj_t *zero_chip,*storage_lines[3],*storage_dot[2],*storage_detail[2],*network_lines[3],*power_lines[3],*pc_chip;
    nui_obj_t *rec_chip,*capacity_text[2],*capacity_bar[2],*media_chip[2],*media_detail[2],*history,*history_note,*history_rows[5][6];
    nui_obj_t *chart,*chart_touch,*legend[4],*ranges[3],*stats[4],*stats_mm[4],*ylabel[9],*xlabel[7],*cursor_label;
    nui_chart_series_t *series[4];int32_t plotted[4][1200];bool visible[4];unsigned range;
    nui_obj_t *plot_image_obj;uint16_t *plot_pixels;nui_image_dsc_t plot_image;
    nui_obj_t *level_image_obj[2];uint16_t *level_pixels[2];nui_image_dsc_t level_image[2];
    int32_t ymin,ymax;tab5_ui_graph_t graph;
    int32_t chart_scratch[1200],chart_latest[4];unsigned chart_count,chart_range;
    bool chart_cached,chart_light,chart_visible[4],chart_live_unfiltered;
    nui_obj_t *set_apply,*set_cancel,*changed,*step_value[8],*theme_btn[2],*clock_text,*panel_text,*fw_text;
    double draft[8];bool dirty,pending,settings_failed,settings_ack;tab5_measure_config_t sent;
    nui_obj_t *net_apply,*reveal,*net_fields[4],*net_switch[2],*net_address[2],*net_chip[2],*net_hint,*tcp_chip,*tcp_address;
    tab5_ui_network_t net_draft,net_sent;bool net_dirty,net_pending,unmasked,network_failed,network_ack;
    bool result_seen;uint32_t result_serial;
    nui_obj_t *veil,*dialog,*modal_title,*modal_hint,*number,*number_frame,*number_error,*num_done,*text_field,*keyboard,*lang,*date_fields[6],*time_error;
    char num[24];unsigned edit_index;bool numeric_valid;int modal_kind,confirm_action,pending_page;unsigned page;
    bool zero_seen,zero_time_known;float zero[4];char zero_time[16];
    nui_timer_t *repeat;int repeat_id;bool repeated;
    uint64_t render_us,chart_plot_us;bool rendered;
    tab5_measure_sensor_t level_state[2];bool level_seen[2],level_light[2];
    tab5_ui_state_t state;tab5_ui_action_cb_t cb;void *user;
} u;
/* Separate input state: layout fields remain owned by the page renderer. */
enum { ENC_MENU, ENC_SELECT, ENC_EDIT, ENC_MODAL };
static struct {
    struct { nui_obj_t *obj; int id; } buttons[256];
    unsigned buttons_n, level, index, saved_index, saved_level, modal_index;
    unsigned streak, edit, cursor_s, date_index;
    uint32_t last_ms, rot_ms;
    int direction;
    bool ring, injecting, rotating, cursor;
    double original;
    bool original_light;
    nui_obj_t *frame, *mode, *hint,*cursor_line,*edit_row,*edit_marks[2];
    int64_t paint_key[16];bool paint_valid,paint_dirty;
    int settings_offset, network_offset;
    nui_obj_t *scroll_thumb[2];
    int16_t timezone_draft;
    nui_obj_t *timezone_value,*sync_button,*sync_status;
    bool sync_requested,sync_seen_busy;
    uint32_t sync_request_serial;
    char sync_previous_status[128];
    uint8_t sent_brightness;bool brightness_pending,brightness_seen;
    uint32_t hint_ms;
    bool touch_down,dragging;int touch_x,touch_y,touch_offset;
    nui_obj_t *touch_view,*touch_content;int *touch_scroll;
} e;
static void encoder_touch(nui_obj_t *obj, int id);
static void encoder_paint(void);
static void encoder_modal_forget(void);
static void encoder_reset(void);
static void encoder_sync(void);
static void enc_cursor(int delta);
static void time_feedback(void);
/* One non-inherited marker per shared role style, registered once across UI
 * destroy/init cycles. No object user_data or per-object cache allocation. */
static nui_style_prop_t role_prop;
static const char *param_name[]={"Частота опроса","Пауза между запросами","Сглаживание EMA","Окно качки","Порог покоя","Частота выборки качки","Гистерезис","Яркость"};
static const char *units[]={"Гц","мс","","с","°","Гц","°","%"};
static const char *limits[]={"1–50 Гц · ограничено RS485","15–100 мс","0.01–0.99 · меньше — плавнее","10–60 с","0.1–10° · размах в окне","2–5 Гц","0–2°","10–100 %"};
static const double lo[]={1,15,.01,10,.1,2,0,10},hi[]={50,100,.99,60,10,5,2,100},step[]={1,5,.01,5,.05,1,.05,10};
static void text(nui_obj_t *o,const char *s){if(o && strcmp(nui_label_get_text(o),s))nui_label_set_text(o,s);}
static void hide(nui_obj_t *o,bool h){if(h)nui_obj_add_flag(o,NUI_OBJ_FLAG_HIDDEN);else nui_obj_remove_flag(o,NUI_OBJ_FLAG_HIDDEN);}
/* Compare exact local selectors, including inactive states; effective getters
 * would miss overrides needed after a theme or state change. */
static void style_num(nui_obj_t *o,nui_style_prop_t prop,int32_t n,nui_style_selector_t sel){nui_style_value_t old,v={.num=n};if(nui_obj_get_local_style_prop(o,prop,&old,sel)!=NUI_STYLE_RES_FOUND||old.num!=n)nui_obj_set_local_style_prop(o,prop,v,sel);}
static void style_color(nui_obj_t *o,nui_style_prop_t prop,nui_color_t c,nui_style_selector_t sel){nui_style_value_t old,v={.color=c};if(nui_obj_get_local_style_prop(o,prop,&old,sel)!=NUI_STYLE_RES_FOUND||!nui_color_eq(old.color,c))nui_obj_set_local_style_prop(o,prop,v,sel);}
static void style_font(nui_obj_t *o,const nui_font_t *f,nui_style_selector_t sel){nui_style_value_t old,v={.ptr=f};if(nui_obj_get_local_style_prop(o,NUI_STYLE_TEXT_FONT,&old,sel)!=NUI_STYLE_RES_FOUND||old.ptr!=f)nui_obj_set_local_style_prop(o,NUI_STYLE_TEXT_FONT,v,sel);}
static void enabled(nui_obj_t *o,bool on){if(on)nui_obj_remove_state(o,NUI_STATE_DISABLED);else nui_obj_add_state(o,NUI_STATE_DISABLED);style_num(o,NUI_STYLE_OPA,on?255:115,0);}
static void role(nui_obj_t *o,int r){
    int previous=nui_obj_get_style_prop(o,NUI_PART_MAIN,role_prop).num-1;
    if(previous==r)return;
    /* Known role pair only. Ambiguous/stacked references keep the old fallback. */
    if(previous>=0&&previous<STYLE_COUNT&&r>=0&&r<STYLE_COUNT&&
       nui_obj_replace_style_atomic(o,&u.style[previous],&u.style[r],NUI_PART_MAIN))return;
    for(int i=0;i<STYLE_COUNT;i++)nui_obj_remove_style(o,&u.style[i],NUI_PART_MAIN);
    nui_obj_add_style(o,&u.style[r],NUI_PART_MAIN);
}
static nui_obj_t *box(nui_obj_t *parent,int x,int y,int w,int h,int r){
    nui_obj_t *o=nui_obj_create(parent);nui_obj_remove_style_all(o);nui_obj_set_pos(o,x,y);nui_obj_set_size(o,w,h);nui_obj_remove_flag(o,NUI_OBJ_FLAG_SCROLLABLE);nui_obj_remove_flag(o,NUI_OBJ_FLAG_CLICKABLE);role(o,r);return o;
}
static nui_obj_t *label(nui_obj_t *parent,const char *s,int x,int y,int w,int h,const nui_font_t *f,int r){
    nui_obj_t *o=nui_label_create(parent);nui_obj_remove_style_all(o);role(o,r);style_font(o,f,0);nui_obj_set_pos(o,x,y);nui_obj_set_size(o,w,h);nui_label_set_long_mode(o,NUI_LABEL_LONG_DOT);text(o,s);return o;
}
static nui_obj_t *chip(nui_obj_t *parent,const char *s,int x,int y,int w,int r){nui_obj_t *o=label(parent,s,x,y,w,32,F16M,r);nui_obj_set_style_text_align(o,NUI_TEXT_ALIGN_CENTER,0);nui_obj_set_style_pad_top(o,6,0);return o;}
static void event(nui_event_t *e);
static nui_obj_t *button(nui_obj_t *parent,const char *s,int x,int y,int w,int h,int r,int id){
    nui_obj_t *o=nui_button_create(parent);nui_obj_remove_style_all(o);role(o,r);nui_obj_set_pos(o,x,y);nui_obj_set_size(o,w,h);nui_obj_remove_flag(o,NUI_OBJ_FLAG_SCROLLABLE);
    nui_obj_t *l=label(o,s,0,0,w,h,w<240&&h<80?F28:F36,TEXT);nui_obj_set_style_text_align(l,NUI_TEXT_ALIGN_CENTER,0);nui_obj_set_height(l,NUI_SIZE_CONTENT);nui_obj_center(l);
    /* Text follows the button's role, including dark actionText. */
    nui_obj_remove_style(l,&u.style[TEXT],0);
    nui_obj_add_event_cb(o,event,NUI_EVENT_CLICKED,(void*)(intptr_t)id);
    if(id!=A_NOTICE_CLOSE&&e.buttons_n<256){e.buttons[e.buttons_n].obj=o;e.buttons[e.buttons_n++].id=id;}return o;
}
static void caption(nui_obj_t *b,const char *s){text(nui_obj_get_child(b,0),s);}
static void line(nui_obj_t *p,int x,int y,int w){box(p,x,y,w,1,LINE);}
static void palette(bool light){
    u.light=light;u.bg=light?0xF3F5F9:0x121826;u.card=light?0xFFFFFF:0x1B2233;u.alt=light?0xF6F8FB:0x18202F;
    u.frame=light?0xF6F7FA:0x2B3340;u.border=light?0xD5DBE6:0x2E3850;u.text=light?0x1A1D24:0xE8ECF4;u.dim=light?0x4A5468:0xB3BDD1;u.muted=light?0x6B7489:0x8A95AD;
    u.navc=light?0x103269:0x0B2247;u.navsel=light?0x1D4E9E:0x1D3F7A;u.action=light?0x1A4FA8:0x7FA8F5;u.actiontext=light?0xFFFFFF:0x0B1733;
    u.ok=light?0x1E7B3A:0x5FD07F;u.warn=light?0x8A5A00:0xF2B84B;u.err=light?0xC0262D:0xFF6B6B;u.info=light?0x1F5FBF:0x7FB0FF;
    u.okbg=light?0xDBF2E0:0x21472E;u.warnbg=light?0xFFF0CC:0x524015;u.errbg=light?0xFCE0E0:0x592121;u.infobg=light?0xE0EBFC:0x213660;
    u.sensor[0]=light?0x1F6BCC:0x5C9EFF;u.sensor[1]=light?0xE06B0D:0xFF9E40;
    for(int r=0;r<STYLE_COUNT;r++){
        if(!u.styles_ready)nui_style_init(&u.style[r]);
        nui_style_value_t marker={.num=r+1};nui_style_set_prop(&u.style[r],role_prop,marker);
        nui_style_t *s=&u.style[r];
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
        nui_style_set_bg_color(s,nui_color_hex(bg));nui_style_set_bg_opa(s,fill?255:0);nui_style_set_text_color(s,nui_color_hex(fg));nui_style_set_border_width(s,bw);nui_style_set_border_color(s,nui_color_hex(br));nui_style_set_radius(s,radius);nui_style_set_pad_all(s,0);nui_style_set_text_font(s,F20);
        nui_style_set_shadow_width(s,r==CARD?8:0);nui_style_set_shadow_offset_y(s,2);nui_style_set_shadow_color(s,nui_color_hex(light?0x1A294D:0x000000));nui_style_set_shadow_opa(s,light?22:70);
    }
    u.styles_ready=true;nui_obj_report_style_change(NULL);
}
static void draw_line(nui_layer_t *layer,float x1,float y1,float x2,float y2,uint32_t color,int width,int dash){
    nui_draw_line_dsc_t d;nui_draw_line_dsc_init(&d);d.p1=(nui_point_precise_t){x1,y1};d.p2=(nui_point_precise_t){x2,y2};d.color=nui_color_hex(color);d.width=width;if(dash){d.dash_width=dash;d.dash_gap=dash;}nui_draw_line(layer,&d);
}
static void arc(nui_layer_t *layer,int x,int y,int radius,int a1,int a2,uint32_t color,int width,int opa){
    nui_draw_arc_dsc_t d;nui_draw_arc_dsc_init(&d);d.center=(nui_point_t){x,y};d.radius=radius;d.start_angle=a1;d.end_angle=a2;d.color=nui_color_hex(color);d.width=width;d.opa=opa;d.rounded=1;nui_draw_arc(layer,&d);
}
static void tri(nui_layer_t *layer,float x1,float y1,float x2,float y2,float x3,float y3,uint32_t color){
    nui_draw_triangle_dsc_t d;nui_draw_triangle_dsc_init(&d);d.p[0]=(nui_point_precise_t){x1,y1};d.p[1]=(nui_point_precise_t){x2,y2};d.p[2]=(nui_point_precise_t){x3,y3};d.color=nui_color_hex(color);d.opa=255;nui_draw_triangle(layer,&d);
}
static void circle(nui_layer_t *layer,int x,int y,int radius,uint32_t color,int width,bool fill,int opa){
    nui_draw_rect_dsc_t d;nui_draw_rect_dsc_init(&d);d.radius=NUI_RADIUS_CIRCLE;d.bg_color=nui_color_hex(color);d.bg_opa=fill?opa:0;d.border_color=nui_color_hex(color);d.border_width=width;d.border_opa=opa;nui_area_t a={x-radius,y-radius,x+radius,y+radius};nui_draw_rect(layer,&d,&a);
}
/* Small native vector icons: no bitmap asset, FontAwesome dependency or AI image. */
static void icon_draw(nui_event_t *e){
    nui_obj_t *o=nui_event_get_target(e);int id=(intptr_t)nui_event_get_user_data(e);nui_area_t a;nui_obj_get_coords(o,&a);nui_layer_t *l=nui_event_get_layer(e);
    int x=a.x1,y=a.y1,w=nui_obj_get_width(o);float k=w/32.f;uint32_t c=nui_color_to_u32(nui_obj_get_style_text_color(o,0))&0xffffff;
    #define L(x1,y1,x2,y2) draw_line(l,x+(x1)*k,y+(y1)*k,x+(x2)*k,y+(y2)*k,c,2,0)
    if(id==0){circle(l,x+16*k,y+16*k,11*k,c,2,false,255);L(16,3,16,10);L(16,22,16,29);L(3,16,10,16);L(22,16,29,16);circle(l,x+19*k,y+13*k,2*k,c,1,true,255);}
    else if(id==1){L(3,28,29,28);for(int j=0;j<24;j++){float t=j/24.f,t2=(j+1)/24.f;draw_line(l,x+(3+26*t)*k,y+(17-6*sinf(t*12.56637f))*k,x+(3+26*t2)*k,y+(17-6*sinf(t2*12.56637f))*k,c,2,0);}}
    else if(id==13){L(4,4,4,28);L(4,28,29,28);L(8,22,14,14);L(14,14,20,19);L(20,19,28,8);}
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
static nui_obj_t *icon(nui_obj_t *p,int id,int x,int y,int size,int r){
    nui_obj_t *o=box(p,x,y,size,size,r);
    nui_obj_add_event_cb(o,icon_draw,NUI_EVENT_DRAW_MAIN,(void*)(intptr_t)id);
    /* Audited leaf: icon_draw only reads coordinates/color and paints primitives;
     * no actions, state mutation or event side effects. Only this pure callback
     * may be omitted under a proven opaque cover; other callbacks keep the guard. */
    nui_obj_add_flag(o,NUI_OBJ_FLAG_OCCLUSION_PAINT_INERT);
    return o;
}
/* Заголовок страницы 32 Bold; возвращает x, с которого можно ставить чип состояния. */
static int page_title(nui_obj_t *p,const char *s){
    nui_point_t z;nui_text_get_size(&z,s,F48,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);
    label(p,s,0,10,z.x+4,66,F48,TEXT);return z.x+24;
}
/* Декор баннера: дуги-«шкала кренометра» справа и тонкие горизонтали, как в макете. */
static void hero_draw(nui_event_t *e){
    nui_obj_t *o=nui_event_get_target(e);nui_area_t a;nui_obj_get_coords(o,&a);nui_layer_t *l=nui_event_get_layer(e);
    for(int i=0;i<3;i++)arc(l,a.x1+1120,a.y1+128,300-i*60,180,270,0xFFFFFF,2,40);
    for(int i=1;i<=3;i++){nui_draw_line_dsc_t d;nui_draw_line_dsc_init(&d);d.p1=(nui_point_precise_t){a.x1,a.y1+i*32};d.p2=(nui_point_precise_t){a.x2,a.y1+i*32};d.color=nui_color_white();d.width=1;d.opa=i==3?20:12;nui_draw_line(l,&d);}
}
static void config_values(const tab5_measure_config_t *c,double v[7]){v[0]=c->frequency_hz;v[1]=c->gap_ms;v[2]=c->alpha;v[3]=c->roll_window_s;v[4]=c->roll_calm_deg;v[5]=c->roll_rate_hz;v[6]=c->roll_hysteresis_deg;}
static tab5_measure_config_t draft_config(void){return (tab5_measure_config_t){.frequency_hz=(uint16_t)u.draft[0],.gap_ms=(uint16_t)u.draft[1],.alpha=u.draft[2],.roll_window_s=(uint8_t)u.draft[3],.roll_calm_deg=u.draft[4],.roll_rate_hz=(uint8_t)u.draft[5],.roll_hysteresis_deg=u.draft[6]};}
static bool cfg_equal(const tab5_measure_config_t *a,const tab5_measure_config_t *b){double va[7],vb[7];config_values(a,va);config_values(b,vb);for(int i=0;i<7;i++)if(fabs(va[i]-vb[i])>.0001)return false;return true;}
static bool net_equal(const tab5_ui_network_t *a,const tab5_ui_network_t *b){return a->ap_enabled==b->ap_enabled&&a->sta_enabled==b->sta_enabled&&!strcmp(a->ap_ssid,b->ap_ssid)&&!strcmp(a->ap_password,b->ap_password)&&!strcmp(a->sta_ssid,b->sta_ssid)&&!strcmp(a->sta_password,b->sta_password);}
static void dispatch(int kind){tab5_ui_action_t a={.kind=kind,.config=u.state.measurement.config,.light_theme=u.state.light_theme,.network=u.state.network};if(u.cb)u.cb(&a,u.user);}
static void refresh_settings(void){
    if(u.draft[7]<10)u.draft[7]=u.state.brightness_pct?u.state.brightness_pct:100;
    if(!e.brightness_seen&&u.state.brightness_pct){u.draft[7]=u.state.brightness_pct;e.brightness_seen=true;}
    if(e.brightness_pending){
        if(u.settings_failed||u.state.brightness_pct==e.sent_brightness)e.brightness_pending=false;
        else u.pending=true;
    }
    double actual[8];config_values(&u.state.measurement.config,actual);actual[7]=u.state.brightness_pct?u.state.brightness_pct:100;unsigned n=0;
    for(unsigned i=0;i<8;i++){bool changed=fabs(u.draft[i]-actual[i])>.0001;n+=changed;char s[40];snprintf(s,sizeof(s),i==2||i==4||i==6?"%.2f%s%s":"%.0f%s%s",u.draft[i],units[i][0]&&strcmp(units[i],"°")?" ":"",units[i]);caption(u.step_value[i],s);role(u.step_value[i],changed?INFO:FRAME);}
    u.dirty=n>0||u.settings_failed;char s[64];snprintf(s,sizeof(s),u.pending?"ожидает подтверждения":u.settings_failed?"не сохранено · повторите":"изменено: %u",n);text(u.changed,s);hide(u.changed,!u.dirty&&!u.pending);enabled(u.set_apply,u.dirty&&!u.pending);enabled(u.set_cancel,u.dirty&&!u.pending);
}
static void close_modal(void){encoder_modal_forget();if(u.veil)nui_obj_delete(u.veil);u.veil=u.dialog=u.keyboard=u.text_field=NULL;u.modal_kind=0;}
static void open_modal(int x,int y,int w,int h,int kind){close_modal();e.paint_dirty=true;e.saved_level=e.level;e.saved_index=e.index;e.level=ENC_MODAL;e.modal_index=kind==3?2:0;e.date_index=0;u.modal_kind=kind;u.veil=box(u.root,0,0,1280,720,BG);style_color(u.veil,NUI_STYLE_BG_COLOR,nui_color_hex(0x080E1E),0);nui_obj_set_style_bg_opa(u.veil,140,0);nui_obj_add_flag(u.veil,NUI_OBJ_FLAG_CLICKABLE);u.dialog=box(u.veil,x,y,w,h,CARD);nui_obj_set_style_radius(u.dialog,20,0);}
static void update_measure(void);
static void update_record(void);
static void show_page(unsigned page){
    /* Page drawing is clipped to its container. Preserve the old extent before
     * hiding it; nav style/marker changes invalidate their own old/new extents. */
    bool bounded=page<sizeof(u.pages)/sizeof(u.pages[0])&&u.page<sizeof(u.pages)/sizeof(u.pages[0])&&u.root&&u.pages[page]&&u.pages[u.page]&&
        nui_obj_get_parent(u.pages[page])==u.root&&nui_obj_get_parent(u.pages[u.page])==u.root;
    if(bounded)nui_obj_invalidate(u.pages[u.page]);
    u.page=page;e.index=0;e.cursor=false;if(e.cursor_line)hide(e.cursor_line,true);for(unsigned i=0;i<sizeof(u.pages)/sizeof(u.pages[0]);i++){hide(u.pages[i],page!=i);role(u.nav[i],page==i?NAVSEL:NAV);hide(u.selected[i],page!=i);}
    if(page==MEASURE||page==ROLL)update_measure();else if(page==RECORD)update_record();
    if(bounded)nui_obj_invalidate(u.pages[page]);else nui_obj_invalidate(u.root);
}
static bool apply_settings(void){tab5_ui_action_t a={.kind=TAB5_UI_SETTINGS,.config=draft_config(),.light_theme=u.state.light_theme,.brightness_pct=(uint8_t)u.draft[7]};u.sent=a.config;e.sent_brightness=a.brightness_pct;e.brightness_pending=true;u.pending=true;u.settings_ack=false;if(u.cb)u.cb(&a,u.user);refresh_settings();return true;}
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
static bool valid_number(double v,unsigned i){return isfinite(v)&&v>=lo[i]-.000001&&v<=hi[i]+.000001&&((i==0||i==1||i==3||i==5||i==7)?fabs(v-round(v))<.000001:true);}
static void number_refresh(void){
    char *end;double v=strtod(u.num,&end);u.numeric_valid=*u.num && !*end && valid_number(v,u.edit_index);text(u.number,u.num);
    style_color(u.number_frame,NUI_STYLE_BORDER_COLOR,nui_color_hex(u.numeric_valid?u.border:u.err),0);enabled(u.num_done,u.numeric_valid);
    char s[160];if(u.numeric_valid)s[0]=0;else snprintf(s,sizeof(s),"✗ допустимо %.2f–%.2f %s; проверьте значение",lo[u.edit_index],hi[u.edit_index],units[u.edit_index]);text(u.number_error,s);
}
static void open_number(unsigned i){
    u.edit_index=i;snprintf(u.num,sizeof(u.num),(i==0||i==1||i==3||i==5||i==7)?"%.0f":"%.2f",u.draft[i]);open_modal(340,52,600,616,1);
    label(u.dialog,param_name[i],28,24,544,36,F32,TEXT);char s[128];snprintf(s,sizeof(s),"допустимо %.2f–%.2f %s · сейчас %.2f",lo[i],hi[i],units[i],u.draft[i]);label(u.dialog,s,28,66,544,28,F16,MUTED);
    u.number_frame=box(u.dialog,28,98,544,80,FRAME);nui_obj_set_style_border_width(u.number_frame,2,0);u.number=label(u.number_frame,u.num,12,10,444,58,F40,TEXT);nui_obj_set_style_text_align(u.number,NUI_TEXT_ALIGN_RIGHT,0);label(u.number_frame,units[i],464,26,72,36,F24,MUTED);
    u.number_error=label(u.dialog,"",28,186,544,28,F16,ERR);nui_obj_set_style_bg_opa(u.number_error,0,0);
    const char *keys[]={"7","8","9","⌫","4","5","6","±","1","2","3","C","0",".","было"};
    for(int k=0;k<15;k++){int row=k/4,col=k%4,w=128,x=28+col*138;if(k>=12){row=3;if(k==12)w=266;else x=k==13?304:442;}nui_obj_t *b=button(u.dialog,keys[k],x,218+row*74,w,64,ALT,400+k);nui_obj_set_style_border_width(b,1,0);style_color(b,NUI_STYLE_BORDER_COLOR,nui_color_hex(u.border),0);nui_obj_set_style_radius(b,12,0);if(k==7 || (k==13&&(i==0||i==1||i==3||i==5||i==7)))enabled(b,false);}
    button(u.dialog,"Отмена",28,520,266,64,GHOST,A_MODAL_CANCEL);u.num_done=button(u.dialog,"Готово",306,520,266,64,PRIMARY,A_NUM_OK);number_refresh();
}
static void step_change(int id){unsigned i=(id-200)/2;double v=u.draft[i]+((id-200)%2?step[i]:-step[i]);u.draft[i]=fmin(hi[i],fmax(lo[i],round(v*100)/100));refresh_settings();}
static void repeat_tick(nui_timer_t *t){(void)t;u.repeated=true;step_change(u.repeat_id);nui_timer_set_period(u.repeat,80);}
static void step_press(nui_event_t *e){
    if(nui_event_get_code(e)==NUI_EVENT_PRESSED){if(u.repeat)nui_timer_delete(u.repeat);u.repeated=false;u.repeat_id=(intptr_t)nui_event_get_user_data(e);u.repeat=nui_timer_create(repeat_tick,400,NULL);}
    else if(u.repeat){nui_timer_delete(u.repeat);u.repeat=NULL;}
}

static const char *ru_map[]={"1#","й","ц","у","к","е","н","г","ш","щ","з","х",NUI_SYMBOL_BACKSPACE,"\n","ф","ы","в","а","п","р","о","л","д","ж","э","\n","я","ч","с","м","и","т","ь","б","ю",".","\n",NUI_SYMBOL_CLOSE,NUI_SYMBOL_LEFT," ",NUI_SYMBOL_RIGHT,NUI_SYMBOL_OK,""};
static nui_buttonmatrix_ctrl_t ru_ctrl[41];
static bool russian;
static nui_font_t keyboard_font;
static void keyboard_style(nui_obj_t *k){
    style_color(k,NUI_STYLE_BG_COLOR,nui_color_hex(u.bg),0);style_num(k,NUI_STYLE_PAD_TOP,10,0);style_num(k,NUI_STYLE_PAD_BOTTOM,10,0);style_num(k,NUI_STYLE_PAD_LEFT,10,0);style_num(k,NUI_STYLE_PAD_RIGHT,10,0);style_num(k,NUI_STYLE_PAD_ROW,10,0);style_num(k,NUI_STYLE_PAD_COLUMN,10,0);
    style_color(k,NUI_STYLE_BG_COLOR,nui_color_hex(u.alt),NUI_PART_ITEMS);style_color(k,NUI_STYLE_BORDER_COLOR,nui_color_hex(u.border),NUI_PART_ITEMS);style_num(k,NUI_STYLE_BORDER_WIDTH,1,NUI_PART_ITEMS);style_num(k,NUI_STYLE_RADIUS,12,NUI_PART_ITEMS);keyboard_font=font_tab5_36;keyboard_font.fallback=NUI_FONT_DEFAULT;style_font(k,&keyboard_font,NUI_PART_ITEMS);style_font(k,&keyboard_font,NUI_PART_ITEMS|NUI_STATE_CHECKED);style_color(k,NUI_STYLE_TEXT_COLOR,nui_color_hex(u.text),NUI_PART_ITEMS);
    style_color(k,NUI_STYLE_BG_COLOR,nui_color_hex(u.alt),NUI_PART_ITEMS|NUI_STATE_CHECKED);style_color(k,NUI_STYLE_TEXT_COLOR,nui_color_hex(u.dim),NUI_PART_ITEMS|NUI_STATE_CHECKED);
    style_color(k,NUI_STYLE_BG_COLOR,nui_color_hex(u.action),NUI_PART_ITEMS|NUI_STATE_PRESSED);style_color(k,NUI_STYLE_TEXT_COLOR,nui_color_hex(u.actiontext),NUI_PART_ITEMS|NUI_STATE_PRESSED);
}
static void keyboard_done(nui_event_t *e){if(u.modal_kind==2){if(nui_event_get_code(e)==NUI_EVENT_READY)event(e);else close_modal();}}
static void open_text(unsigned i){
    u.edit_index=i;open_modal(120,24,1144,300,2);const char *names[]={"AP: имя сети","AP: пароль","STA: имя сети","STA: пароль"};label(u.dialog,names[i],24,20,780,40,F32,TEXT);
    u.lang=button(u.dialog,russian?"РУС → EN":"EN → РУС",904,16,216,56,OUTLINE,A_LANG);
    u.text_field=nui_textarea_create(u.dialog);nui_obj_set_pos(u.text_field,24,84);nui_obj_set_size(u.text_field,1096,64);nui_textarea_set_one_line(u.text_field,true);nui_textarea_set_max_length(u.text_field,i%2?63:32);style_font(u.text_field,F24,0);nui_obj_set_style_pad_top(u.text_field,14,0);nui_obj_set_style_pad_bottom(u.text_field,14,0);style_color(u.text_field,NUI_STYLE_BG_COLOR,nui_color_hex(u.frame),0);style_color(u.text_field,NUI_STYLE_TEXT_COLOR,nui_color_hex(u.text),0);
    const char *v[]={u.net_draft.ap_ssid,u.net_draft.ap_password,u.net_draft.sta_ssid,u.net_draft.sta_password};nui_textarea_set_text(u.text_field,v[i]);nui_textarea_set_password_mode(u.text_field,i%2&&!u.unmasked);
    label(u.dialog,i%2?"Пароль: AP 8–63 байта; STA пустой или 8–63 байта":"SSID: до 32 байт UTF-8",24,164,1096,32,F16,MUTED);
    button(u.dialog,"Отмена",24,220,530,56,GHOST,A_MODAL_CANCEL);button(u.dialog,"Готово",570,220,550,56,PRIMARY,A_TEXT_OK);
    u.keyboard=nui_keyboard_create(u.veil);nui_obj_set_size(u.keyboard,1280,360);nui_obj_set_align(u.keyboard,NUI_ALIGN_TOP_LEFT);nui_obj_set_pos(u.keyboard,0,360);keyboard_style(u.keyboard);nui_keyboard_set_textarea(u.keyboard,u.text_field);
    if(russian){for(unsigned k=0;k<41;k++)ru_ctrl[k]=1;nui_keyboard_set_map(u.keyboard,NUI_KEYBOARD_MODE_USER_1,ru_map,ru_ctrl);nui_keyboard_set_mode(u.keyboard,NUI_KEYBOARD_MODE_USER_1);}
    nui_obj_add_event_cb(u.keyboard,keyboard_done,NUI_EVENT_READY,(void*)(intptr_t)A_TEXT_OK);nui_obj_add_event_cb(u.keyboard,keyboard_done,NUI_EVENT_CANCEL,NULL);
}
static bool date_valid(tab5_ui_datetime_t *out){
    int v[6];for(int i=0;i<6;i++){const char *s=nui_textarea_get_text(u.date_fields[i]);if(!*s)return false;for(const char *c=s;*c;c++)if(*c<'0'||*c>'9')return false;v[i]=atoi(s);}
    if(!tab5_time_fields_valid(v[0],v[1],v[2],v[3],v[4],v[5]))return false;
    *out=(tab5_ui_datetime_t){v[0],v[1],v[2],v[3],v[4],v[5]};return true;
}
static nui_obj_t *time_done;
static void date_edited(nui_event_t *ev){(void)ev;tab5_ui_datetime_t t;bool valid=date_valid(&t);enabled(time_done,valid);text(u.time_error,valid?"":"Проверьте календарную дату и время (2000–2099)");}
static bool timezone_valid(int minutes){return minutes>=-720&&minutes<=840&&minutes%15==0;}
static void timezone_refresh(void){
    int minutes=e.timezone_draft,absolute=minutes<0?-minutes:minutes;char b[16];
    snprintf(b,sizeof(b),"%c%02d:%02d",minutes<0?'-':'+',absolute/60,absolute%60);caption(e.timezone_value,b);
}
static void timezone_nudge(int32_t ticks){
    int64_t minutes=(int64_t)e.timezone_draft+(int64_t)ticks*15;
    if(minutes<-720)minutes=-720;
    if(minutes>840)minutes=840;
    e.timezone_draft=(int16_t)minutes;timezone_refresh();
}
static void time_feedback(void){
    bool completed=e.sync_requested&&!u.state.time_sync_busy&&u.state.action_result_known&&
        u.state.action_result_kind==TAB5_UI_TIME_SYNC&&u.state.action_result_serial!=e.sync_request_serial;
    if(completed&&u.state.action_result_success&&u.state.datetime_known&&u.modal_kind==5){
        tab5_ui_datetime_t t=u.state.datetime;unsigned fields[]={t.year,t.month,t.day,t.hour,t.minute,t.second};
        for(unsigned i=0;i<6;i++){char b[12];snprintf(b,sizeof(b),"%u",fields[i]);nui_textarea_set_text(u.date_fields[i],b);}
        if(u.state.timezone_known&&timezone_valid(u.state.timezone_minutes)){e.timezone_draft=u.state.timezone_minutes;timezone_refresh();}
        date_edited(NULL);
    }
    if(e.sync_requested){
        if(u.state.time_sync_busy)e.sync_seen_busy=true;
        if(!u.state.time_sync_busy&&(e.sync_seen_busy||strcmp(e.sync_previous_status,u.state.time_sync_status)||
           (u.state.action_result_known&&u.state.action_result_kind==TAB5_UI_TIME_SYNC&&u.state.action_result_serial!=e.sync_request_serial)))e.sync_requested=false;
    }
    if(u.modal_kind!=5)return;
    bool busy=u.state.time_sync_busy||e.sync_requested;
    enabled(e.sync_button,!busy);
    caption(e.sync_button,busy?"Ожидание…":"По WiFi");
    const char *message=e.sync_requested&&!u.state.time_sync_busy?"Запрос синхронизации отправлен":u.state.time_sync_status[0]?u.state.time_sync_status:u.state.time_sync_busy?"Синхронизация по WiFi…":"По WiFi: требуется подключение к сети с интернетом";
    text(e.sync_status,message);
}
static void date_focus(nui_event_t *ev){encoder_touch(nui_event_get_target(ev),0);if(u.keyboard)nui_keyboard_set_textarea(u.keyboard,nui_event_get_target(ev));}
static void open_time(void){
    open_modal(68,12,1144,336,5);label(u.dialog,"Дата и время",24,12,1096,44,F32,TEXT);
    e.timezone_draft=u.state.timezone_known&&timezone_valid(u.state.timezone_minutes)?u.state.timezone_minutes:180;
    const char *names[]={"Год","Месяц","День","Час","Минута","Секунда"};tab5_ui_datetime_t t=u.state.datetime;unsigned v[]={t.year,t.month,t.day,t.hour,t.minute,t.second};
    for(int i=0;i<6;i++){
        int x=24+i*130;label(u.dialog,names[i],x,64,118,28,F20,MUTED);
        u.date_fields[i]=nui_textarea_create(u.dialog);nui_obj_set_pos(u.date_fields[i],x,96);nui_obj_set_size(u.date_fields[i],118,64);nui_textarea_set_one_line(u.date_fields[i],true);nui_textarea_set_accepted_chars(u.date_fields[i],"0123456789");nui_textarea_set_max_length(u.date_fields[i],i?2:4);style_font(u.date_fields[i],F28,0);nui_obj_set_style_pad_top(u.date_fields[i],14,0);nui_obj_set_style_pad_bottom(u.date_fields[i],14,0);style_color(u.date_fields[i],NUI_STYLE_BG_COLOR,nui_color_hex(u.frame),0);style_color(u.date_fields[i],NUI_STYLE_TEXT_COLOR,nui_color_hex(u.text),0);
        char s[16];snprintf(s,sizeof(s),"%u",v[i]);nui_textarea_set_text(u.date_fields[i],u.state.datetime_known?s:"");nui_obj_add_event_cb(u.date_fields[i],date_focus,NUI_EVENT_FOCUSED,NULL);
    }
    label(u.dialog,"Часовой пояс UTC",814,64,306,28,F20,MUTED);
    button(u.dialog,"−",814,96,56,64,OUTLINE,A_TZ_MINUS);
    e.timezone_value=button(u.dialog,"",878,96,178,64,FRAME,A_TZ_SELECT);style_font(nui_obj_get_child(e.timezone_value,0),F28,0);
    button(u.dialog,"+",1064,96,56,64,OUTLINE,A_TZ_PLUS);timezone_refresh();
    u.time_error=label(u.dialog,"",24,166,1096,28,F20,ERR);style_num(u.time_error,NUI_STYLE_BG_OPA,0,0);
    e.sync_status=label(u.dialog,"",24,196,1096,48,F20,MUTED);nui_label_set_long_mode(e.sync_status,NUI_LABEL_LONG_WRAP);
    button(u.dialog,"Отмена",24,256,348,64,GHOST,A_MODAL_CANCEL);
    e.sync_button=button(u.dialog,"По WiFi",398,256,348,64,OUTLINE,A_TIME_SYNC);
    time_done=button(u.dialog,"Готово",772,256,348,64,PRIMARY,A_TIME_OK);
    u.keyboard=nui_keyboard_create(u.veil);nui_obj_set_size(u.keyboard,1280,360);nui_obj_set_align(u.keyboard,NUI_ALIGN_TOP_LEFT);nui_obj_set_pos(u.keyboard,0,360);keyboard_style(u.keyboard);nui_keyboard_set_mode(u.keyboard,NUI_KEYBOARD_MODE_NUMBER);nui_keyboard_set_textarea(u.keyboard,u.date_fields[0]);
    for(int i=0;i<6;i++)nui_obj_add_event_cb(u.date_fields[i],date_edited,NUI_EVENT_VALUE_CHANGED,NULL);
    date_edited(NULL);time_feedback();
}
static void refresh_network(void){
    const char *v[]={u.net_draft.ap_ssid,u.net_draft.ap_password,u.net_draft.sta_ssid,u.net_draft.sta_password};
    for(unsigned i=0;i<4;i++){char masked[64];if(i%2&&!u.unmasked){size_t n=strlen(v[i]);if(n>20)n=20;for(size_t q=0;q<n;q++)memcpy(masked+q*3,"\xE2\x80\xA2",3);masked[n*3]=0;text(nui_obj_get_child(u.net_fields[i],1),masked);}else text(nui_obj_get_child(u.net_fields[i],1),v[i]);}
    if(u.net_draft.ap_enabled)nui_obj_add_state(u.net_switch[0],NUI_STATE_CHECKED);else nui_obj_remove_state(u.net_switch[0],NUI_STATE_CHECKED);
    if(u.net_draft.sta_enabled)nui_obj_add_state(u.net_switch[1],NUI_STATE_CHECKED);else nui_obj_remove_state(u.net_switch[1],NUI_STATE_CHECKED);
    u.net_dirty=!net_equal(&u.net_draft,&u.state.network)||u.network_failed;enabled(u.net_apply,u.net_dirty&&!u.net_pending);caption(u.reveal,u.unmasked?"Скрыть пароли":"Показать пароли");
}
static void switch_changed(nui_event_t *ev){encoder_touch(nui_event_get_target(ev),0);u.net_draft.ap_enabled=nui_obj_has_state(u.net_switch[0],NUI_STATE_CHECKED);u.net_draft.sta_enabled=nui_obj_has_state(u.net_switch[1],NUI_STATE_CHECKED);refresh_network();}
static void chart_refresh(bool force);
static void action_event(nui_event_t *ev){
    int id=(intptr_t)nui_event_get_user_data(ev);
    encoder_touch(nui_event_get_target(ev),id);
    if(id>=100&&id<100+(int)(sizeof(u.pages)/sizeof(u.pages[0]))){ask_navigation(id-100);if(u.page==GRAPH)chart_refresh(true);return;}
    if(id>=200&&id<216){if(!u.repeated)step_change(id);u.repeated=false;return;}
    if(id>=300&&id<308){open_number(id-300);return;}
    if(id>=400&&id<415){int k=id-400;size_t n=strlen(u.num);const char *digits[]={"7","8","9","","4","5","6","","1","2","3","","0",".",""};
        if(k==3){if(n)u.num[n-1]=0;}else if(k==11)u.num[0]=0;else if(k==14)snprintf(u.num,sizeof(u.num),"%.2f",u.draft[u.edit_index]);else if(n+1<sizeof(u.num)){if(k!=13||!strchr(u.num,'.'))strcat(u.num,digits[k]);}number_refresh();return;}
    if(id>=500&&id<504){open_text(id-500);return;}
    if(id>=600&&id<604){unsigned i=id-600;u.visible[i]=!u.visible[i];style_num(u.legend[i],NUI_STYLE_OPA,u.visible[i]?255:115,0);chart_refresh(true);return;}
    if(id>=610&&id<613){u.range=(unsigned)(id-610);chart_refresh(true);return;}
    switch(id){
    case A_NOTICE_CLOSE:if(u.notice)nui_obj_delete(u.notice);u.notice=u.notice_text=NULL;break;
    case A_START:dispatch(u.state.recording?TAB5_UI_STOP:TAB5_UI_START);break;
    case A_ZERO:confirm(A_ZERO);break;
    case A_RESET:confirm(A_RESET);break;
    case A_CONFIRM:{int a=u.confirm_action;close_modal();dispatch(a==A_ZERO?TAB5_UI_ZERO:TAB5_UI_ZERO_RESET);break;}
    case A_MODAL_CANCEL:case A_STAY:close_modal();break;
    case A_DISCARD:if(u.page==SETTINGS){config_values(&u.state.measurement.config,u.draft);u.draft[7]=u.state.brightness_pct?u.state.brightness_pct:100;u.pending=false;u.settings_failed=false;refresh_settings();}else {u.net_draft=u.state.network;u.net_pending=false;u.network_failed=false;refresh_network();}close_modal();show_page(u.pending_page);break;
    case A_APPLY:if(u.modal_kind==3){bool applied=u.page==SETTINGS?apply_settings():apply_network();if(applied){unsigned p=u.pending_page;close_modal();show_page(p);}}else apply_settings();break;
    case A_CANCEL:config_values(&u.state.measurement.config,u.draft);u.draft[7]=u.state.brightness_pct?u.state.brightness_pct:100;u.settings_failed=false;u.pending=false;refresh_settings();break;
    case A_NET_APPLY:apply_network();break;
    case A_REVEAL:u.unmasked=!u.unmasked;refresh_network();break;
    case A_LIGHT:case A_DARK:{
        /* Queue the backend-owned change, just like a PC theme command.
         * Do not synchronously call tab5_ui_update from the touch/encoder
         * event stack: its snapshot plus the touch/keyboard scratch frames
         * exceed the native_ui stack. The next main snapshot applies the
         * palette under the normal UI lock, including backend failures. */
        bool light=id==A_LIGHT;
        tab5_ui_action_t a={.kind=TAB5_UI_SETTINGS,.config=u.pending?u.sent:u.state.measurement.config,.light_theme=light};
        if(u.cb)u.cb(&a,u.user);
        break;
    }
    case A_NUM_OK:if(u.numeric_valid){u.draft[u.edit_index]=strtod(u.num,NULL);close_modal();refresh_settings();}break;
    case A_TEXT_OK:{const char *s=nui_textarea_get_text(u.text_field);size_t n=strlen(s),max=u.edit_index%2?63:32;if(n>max){style_color(u.text_field,NUI_STYLE_BORDER_COLOR,nui_color_hex(u.err),0);break;}char *v[]={u.net_draft.ap_ssid,u.net_draft.ap_password,u.net_draft.sta_ssid,u.net_draft.sta_password};snprintf(v[u.edit_index],u.edit_index%2?65:33,"%s",s);close_modal();refresh_network();break;}
    case A_LANG:russian=!russian;caption(u.lang,russian?"РУС → EN":"EN → РУС");if(russian){for(unsigned k=0;k<41;k++)ru_ctrl[k]=1;nui_keyboard_set_map(u.keyboard,NUI_KEYBOARD_MODE_USER_1,ru_map,ru_ctrl);nui_keyboard_set_mode(u.keyboard,NUI_KEYBOARD_MODE_USER_1);}else nui_keyboard_set_mode(u.keyboard,NUI_KEYBOARD_MODE_TEXT_LOWER);break;
    case A_TIME:open_time();break;
    case A_TZ_SELECT:if(u.keyboard)nui_keyboard_set_textarea(u.keyboard,NULL);break;
    case A_TZ_MINUS:case A_TZ_PLUS:timezone_nudge(id==A_TZ_MINUS?-1:1);break;
    case A_TIME_SYNC:
        if(!u.state.time_sync_busy&&!e.sync_requested&&timezone_valid(e.timezone_draft)){
            e.sync_requested=true;e.sync_seen_busy=false;e.sync_request_serial=u.state.action_result_serial;
            snprintf(e.sync_previous_status,sizeof(e.sync_previous_status),"%s",u.state.time_sync_status);
            tab5_ui_action_t a={.kind=TAB5_UI_TIME_SYNC,.timezone_minutes=e.timezone_draft,.timezone_set=true};
            if(u.cb)u.cb(&a,u.user);
            time_feedback();
        }
        break;
    case A_TIME_OK:{tab5_ui_datetime_t t;if(date_valid(&t)&&timezone_valid(e.timezone_draft)){tab5_ui_action_t a={.kind=TAB5_UI_TIME,.datetime=t,.timezone_minutes=e.timezone_draft,.timezone_set=true};if(u.cb)u.cb(&a,u.user);close_modal();}break;}
    }
}

/* IDs are registered by button(), never inferred from captions or geometry. */
static bool enc_descendant(nui_obj_t *o,nui_obj_t *parent){
    for(;o;o=nui_obj_get_parent(o))if(o==parent)return true;
    return false;
}
static nui_obj_t *enc_button(int id){
    for(unsigned i=e.buttons_n;i;i--)if(e.buttons[i-1].id==id)return e.buttons[i-1].obj;
    return NULL;
}
static void encoder_modal_forget(void){
    if(!u.veil)return;
    unsigned n=0;
    for(unsigned i=0;i<e.buttons_n;i++)if(!enc_descendant(e.buttons[i].obj,u.veil))e.buttons[n++]=e.buttons[i];
    e.paint_dirty=true;e.buttons_n=n;e.level=e.saved_level;e.index=e.saved_index;
    if(e.frame)hide(e.frame,true);
}
static unsigned enc_page_count(void){return (unsigned)(sizeof(u.pages)/sizeof(u.pages[0]));}
static unsigned enc_count(void){
    if(u.page==MEASURE)return 3;
    if(u.page==GRAPH)return 8;
    if(u.page==RECORD)return 1;
    if(u.page==SETTINGS)return 11;
    if(u.page==NETWORK)return 8; /* includes STA password and Apply */
    return 0;
}
static nui_obj_t *enc_object(unsigned index){
    if(u.page==MEASURE)return index==0?u.start[0]:enc_button(index==1?A_ZERO:A_RESET);
    if(u.page==RECORD)return u.start[1];
    if(u.page==GRAPH)return index<3?u.ranges[index]:index==3?u.chart_touch:u.legend[index-4];
    if(u.page==SETTINGS)return index<10?u.setting_rows[index]:u.set_apply;
    if(u.page==NETWORK){nui_obj_t *items[]={u.net_switch[0],u.net_fields[0],u.net_fields[1],u.net_switch[1],u.net_fields[2],u.net_fields[3],u.reveal,u.net_apply};return index<8?items[index]:NULL;}
    return NULL;
}
static unsigned enc_clamp(unsigned value,int32_t delta,unsigned count){
    int64_t next=(int64_t)value+delta;
    return !count||next<0?0:next>=(int64_t)count?count-1:(unsigned)next;
}
static unsigned enc_modal_buttons(nui_obj_t **out){
    unsigned n=0;
    for(unsigned i=0;i<e.buttons_n;i++)if(enc_descendant(e.buttons[i].obj,u.dialog))out[n++]=e.buttons[i].obj;
    return n;
}
static unsigned enc_modal_count(void){
    if(u.modal_kind==5)return 10; /* six date fields, UTC offset, cancel, sync, done */
    nui_obj_t *items[256];unsigned n=enc_modal_buttons(items);
    return n+(u.keyboard?(unsigned)nui_native_keyboard_get_keys(u.keyboard,NULL,NULL,0):0);
}
static nui_obj_t *enc_modal_object(nui_area_t *key_area){
    if(u.modal_kind==5)return e.modal_index<6?u.date_fields[e.modal_index]:enc_button(e.modal_index==6?A_TZ_SELECT:e.modal_index==7?A_MODAL_CANCEL:e.modal_index==8?A_TIME_SYNC:A_TIME_OK);
    unsigned keys=u.keyboard?(unsigned)nui_native_keyboard_get_keys(u.keyboard,NULL,NULL,0):0;
    if(e.modal_index<keys){nui_area_t areas[128];nui_native_keyboard_get_keys(u.keyboard,areas,NULL,128);if(key_area)*key_area=areas[e.modal_index];return u.keyboard;}
    nui_obj_t *items[256];unsigned n=enc_modal_buttons(items),j=e.modal_index-keys;
    return j<n?items[j]:NULL;
}
/* View-owned, noninteractive thumbs: appended once after the content, never
 * reordered on idle. Geometry setters and style_color are change-only. */
static void enc_scroll_thumb(nui_obj_t *view,nui_obj_t *content,int offset,unsigned index){
    if(!view||!content)return;
    int vh=nui_obj_get_height(view),ch=nui_obj_get_height(content),track=vh-16;
    if(!e.scroll_thumb[index]){
        e.scroll_thumb[index]=box(view,0,8,6,32,MUTED);
        style_num(e.scroll_thumb[index],NUI_STYLE_BG_OPA,160,0);
        style_num(e.scroll_thumb[index],NUI_STYLE_RADIUS,3,0);
    }
    nui_obj_t *thumb=e.scroll_thumb[index];
    hide(thumb,ch<=vh||track<=0);
    if(ch<=vh||track<=0)return;
    int height=(int)((int64_t)track*vh/ch);
    if(height<32)height=32;
    if(height>track)height=track;
    int maximum=ch-vh;
    if(offset<0)offset=0;
    if(offset>maximum)offset=maximum;
    int border=nui_obj_get_style_border_width(view,0);
    nui_obj_set_pos(thumb,nui_obj_get_width(view)-12-border,8-border+(int)((int64_t)offset*(track-height)/maximum));
    nui_obj_set_size(thumb,6,height);
    style_color(thumb,NUI_STYLE_BG_COLOR,nui_color_hex(u.muted),0);
}
bool tab5_ui_scroll_cache_enabled(void){
    return u.settings_content&&nui_obj_get_layer_cache_enabled(u.settings_content);
}
void tab5_ui_set_scroll_cache_enabled(bool enabled){
    if(u.settings_content)nui_obj_set_layer_cache_parent_matte(u.settings_content,enabled);
    if(u.notice)nui_obj_set_layer_cache(u.notice,enabled);
}
void tab5_ui_clear_scroll_cache(void){
    if(u.settings_content)nui_obj_clear_layer_cache(u.settings_content);
    if(u.notice)nui_obj_clear_layer_cache(u.notice);
}
bool tab5_ui_scroll_benchmark_begin(tab5_ui_scroll_benchmark_state_t *saved,int *maximum){
    if(!saved||!maximum||!u.root||u.modal_kind||e.touch_down||u.state.recording||u.dirty||u.pending)return false;
    *saved=(tab5_ui_scroll_benchmark_state_t){.page=u.page,.index=e.index,.level=e.level,.settings_offset=e.settings_offset,.ring=e.ring,.valid=true};
    e.ring=false;show_page(SETTINGS);encoder_paint();
    *maximum=nui_obj_get_height(u.settings_content)-nui_obj_get_height(u.settings_view);
    if(*maximum<0)*maximum=0;
    tab5_ui_scroll_benchmark_position(0);return true;
}
void tab5_ui_scroll_benchmark_position(int offset){
    if(!u.root)return;
    int maximum=nui_obj_get_height(u.settings_content)-nui_obj_get_height(u.settings_view);
    if(maximum<0)maximum=0;
    if(offset<0)offset=0;
    if(offset>maximum)offset=maximum;
    e.settings_offset=offset;nui_obj_set_y(u.settings_content,-offset);
    enc_scroll_thumb(u.settings_view,u.settings_content,offset,0);
}
void tab5_ui_scroll_benchmark_end(tab5_ui_scroll_benchmark_state_t *saved){
    if(!saved||!saved->valid||!u.root)return;
    tab5_ui_scroll_benchmark_position(saved->settings_offset);show_page(saved->page);
    e.index=saved->index;e.level=saved->level;e.ring=saved->ring;saved->valid=false;
}
static void enc_scroll(nui_obj_t *obj,nui_obj_t *view,nui_obj_t *content,int *offset){
    if(!obj||!view||!content||!enc_descendant(obj,content))return;
    nui_area_t a,v;nui_obj_get_coords(obj,&a);nui_obj_get_coords(view,&v);
    if(a.y1<v.y1+4)*offset-=v.y1+4-a.y1;
    if(a.y2>v.y2-4)*offset+=a.y2-v.y2+4;
    int max=nui_obj_get_height(content)-nui_obj_get_height(view);if(max<0)max=0;
    if(*offset<0)*offset=0;
    if(*offset>max)*offset=max;
    nui_obj_set_y(content,-*offset);
    enc_scroll_thumb(view,content,*offset,view==u.settings_view?0:1);
}
static void encoder_reset(void){memset(&e,0,sizeof(e));}
static void encoder_sync(void){
    time_feedback();
    enc_scroll_thumb(u.settings_view,u.settings_content,e.settings_offset,0);
    enc_scroll_thumb(u.network_view,u.network_content,e.network_offset,1);
    if(!u.dirty&&!u.pending&&!e.brightness_pending)u.draft[7]=u.state.brightness_pct?u.state.brightness_pct:100;
    if(e.brightness_pending){
        if(u.state.brightness_pct==e.sent_brightness){e.brightness_pending=false;}
        else if(!u.settings_failed){u.pending=true;u.draft[7]=e.sent_brightness;}
    }
    if(e.hint&&(uint32_t)(nui_tick_get()-e.hint_ms)>=2600)hide(e.hint,true);
    if(e.frame)encoder_paint();
    if(e.cursor)enc_cursor(0);
}
static void encoder_paint(void){
    if(!u.root||(!e.frame&&!e.ring))return;
    nui_area_t a={0};
    nui_obj_t *obj=!e.ring?NULL:u.modal_kind?enc_modal_object(&a):e.level==ENC_MENU?u.nav[u.page]:enc_object(e.index);
    bool editing=e.ring&&!u.modal_kind&&e.level==ENC_EDIT&&u.page==SETTINGS;
    if(editing)obj=e.edit<8?u.step_value[e.edit]:u.setting_rows[8];
    if(obj){
        enc_scroll(obj,u.settings_view,u.settings_content,&e.settings_offset);
        enc_scroll(obj,u.network_view,u.network_content,&e.network_offset);
        if(obj!=u.keyboard)nui_obj_get_coords(obj,&a);
    }
    const int64_t key[16]={(intptr_t)obj,a.x1,a.y1,a.x2,a.y2,e.ring,e.level,u.page,
        u.modal_kind,e.modal_index,e.index,e.edit,u.light,(intptr_t)u.veil,editing,0};
    if(e.paint_valid&&!e.paint_dirty&&!memcmp(key,e.paint_key,sizeof(key)))return;
    memcpy(e.paint_key,key,sizeof(key));e.paint_valid=true;e.paint_dirty=false;
    if(!e.frame){e.frame=box(u.root,0,0,10,10,GHOST);style_num(e.frame,NUI_STYLE_BG_OPA,0,0);style_color(e.frame,NUI_STYLE_BORDER_COLOR,nui_color_hex(0xF87128),0);style_num(e.frame,NUI_STYLE_BORDER_WIDTH,5,0);e.mode=chip(u.root,"",730,10,100,ORANGE);}
    hide(e.frame,!e.ring||!obj);hide(e.mode,!e.ring);
    if(e.edit_row){style_num(e.edit_row,NUI_STYLE_BG_OPA,0,0);e.edit_row=NULL;}
    for(unsigned i=0;i<2;i++)if(e.edit_marks[i])hide(e.edit_marks[i],true);
    if(!e.ring)return;
    const char *modes[]={"меню","выбор","правка","окно"};text(e.mode,modes[u.modal_kind?ENC_MODAL:e.level]);
    if(!obj)return;
    if(editing){e.edit_row=u.setting_rows[e.edit];style_color(e.edit_row,NUI_STYLE_BG_COLOR,nui_color_hex(0xF87128),0);style_num(e.edit_row,NUI_STYLE_BG_OPA,28,0);}
    bool inside=e.level==ENC_MENU||(!u.modal_kind&&(u.page==SETTINGS||u.page==NETWORK));int pad=inside?-2:4;
    nui_obj_set_pos(e.frame,a.x1-pad,a.y1-pad);nui_obj_set_size(e.frame,a.x2-a.x1+1+pad*2,a.y2-a.y1+1+pad*2);
    style_num(e.frame,NUI_STYLE_BORDER_WIDTH,e.level==ENC_EDIT?4:5,0);
    nui_obj_move_foreground(e.frame);nui_obj_move_foreground(e.mode);
    if(editing&&e.edit<8){
        for(unsigned i=0;i<2;i++){
            if(!e.edit_marks[i])e.edit_marks[i]=label(u.root,i?">":"<",0,0,26,40,F28,ORANGE);
            nui_obj_set_pos(e.edit_marks[i],i?a.x2+8:a.x1-32,a.y1+(a.y2-a.y1-40)/2);hide(e.edit_marks[i],false);nui_obj_move_foreground(e.edit_marks[i]);
        }
    }
}
static void encoder_touch(nui_obj_t *obj,int id){
    if(e.injecting)return;
    e.paint_dirty=true;e.ring=false;e.streak=0;e.rotating=false;
    if(u.modal_kind){
        if(u.modal_kind==5){for(unsigned i=0;i<6;i++)if(obj==u.date_fields[i])e.modal_index=i;
            if(id==A_TZ_SELECT||id==A_TZ_MINUS||id==A_TZ_PLUS)e.modal_index=6;
            else if(id==A_MODAL_CANCEL)e.modal_index=7;else if(id==A_TIME_SYNC)e.modal_index=8;else if(id==A_TIME_OK)e.modal_index=9;}
        else {nui_obj_t *items[256];unsigned n=enc_modal_buttons(items);unsigned keys=u.keyboard?(unsigned)nui_native_keyboard_get_keys(u.keyboard,NULL,NULL,0):0;for(unsigned i=0;i<n;i++)if(items[i]==obj)e.modal_index=keys+i;}
    }else if(id>=100&&id<100+(int)enc_page_count()){e.level=ENC_MENU;e.index=0;}
    else {e.level=ENC_SELECT;for(unsigned i=0;i<enc_count();i++){nui_obj_t *item=enc_object(i);if(item&&enc_descendant(obj,item)){e.index=i;break;}}if(u.page==SETTINGS&&id>=200&&id<216)e.index=(id-200)/2;}
    encoder_paint();
}
static void event(nui_event_t *ev){action_event(ev);encoder_paint();}
static void enc_action(int id){
    nui_event_t ev={.target=enc_button(id),.user_data=(void*)(intptr_t)id,.code=NUI_EVENT_CLICKED};
    action_event(&ev);
}
static void enc_hint(const char *message){
    if(!e.hint)e.hint=chip(u.root,"",272,640,920,INFO);
    e.hint_ms=nui_tick_get();text(e.hint,message);hide(e.hint,false);nui_obj_move_foreground(e.hint);
}
static unsigned enc_acceleration(int direction,uint32_t now){
    e.streak=e.rotating&&e.direction==direction&&(uint32_t)(now-e.rot_ms)<90?e.streak+1:1;
    e.rotating=true;e.rot_ms=now;e.direction=direction;
    return e.streak>=6?5:1;
}
static void enc_cursor(int delta){
    unsigned seconds=u.range==0?30:u.range==1?120:600;
    e.cursor_s=enc_clamp(e.cursor_s,delta,seconds+1);
    unsigned count=u.range==0?300:u.range==1?240:1200;
    unsigned idx=(unsigned)((uint64_t)e.cursor_s*(count-1)/seconds);
    char b[220];size_t used=(size_t)snprintf(b,sizeof(b),"−%u с",seconds-e.cursor_s);
    for(unsigned i=0;i<4&&used<sizeof(b)-1;i++)if(u.visible[i]&&u.plotted[i][idx]!=NUI_CHART_POINT_NONE)used+=(size_t)snprintf(b+used,sizeof(b)-used,"  Д%u %c %+.2f°",i/2+2,i%2?'Y':'X',u.plotted[i][idx]/100.0);
    text(u.cursor_label,b);
    if(!e.cursor_line){e.cursor_line=box(u.chart,0,0,3,TAB5_PLOT_HEIGHT,ORANGE);nui_obj_set_style_radius(e.cursor_line,0,0);}
    nui_obj_set_x(e.cursor_line,(int)((uint64_t)e.cursor_s*(TAB5_PLOT_WIDTH-3)/seconds));hide(e.cursor_line,false);
}
static void enc_edit_nudge(int direction,unsigned multiplier){
    if(e.cursor){enc_cursor(direction*(int)multiplier);return;}
    if(e.edit==8){bool light=direction<0; if(light!=u.state.light_theme)enc_action(light?A_LIGHT:A_DARK);return;}
    unsigned i=e.edit;
    u.draft[i]=fmin(hi[i],fmax(lo[i],round((u.draft[i]+direction*step[i]*multiplier)*100)/100));refresh_settings();
}
static void enc_activate(void){
    if(u.page==SETTINGS){
        if(e.index<9){e.edit=e.index;e.original=e.index<8?u.draft[e.index]:0;e.original_light=u.state.light_theme;e.level=ENC_EDIT;e.streak=0;e.rotating=false;enc_hint("< > вращайте — значение · нажмите — готово · удерживайте — отмена");}
        else enc_action(e.index==9?A_TIME:A_APPLY);
    }else if(u.page==GRAPH){if(e.index<3)enc_action(610+e.index);else if(e.index==3){e.cursor=true;e.level=ENC_EDIT;e.cursor_s=u.range==0?30:u.range==1?120:600;e.rotating=false;enc_cursor(0);enc_hint("вращайте — время · нажмите — выйти");}else enc_action(600+e.index-4);}
    else if(u.page==MEASURE)enc_action(e.index==0?A_START:e.index==1?A_ZERO:A_RESET);
    else if(u.page==RECORD)enc_action(A_START);
    else if(u.page==NETWORK){if(e.index==0||e.index==3){unsigned i=e.index==3;nui_obj_t *sw=u.net_switch[i];if(nui_obj_has_state(sw,NUI_STATE_CHECKED))nui_obj_remove_state(sw,NUI_STATE_CHECKED);else nui_obj_add_state(sw,NUI_STATE_CHECKED);nui_obj_send_event(sw,NUI_EVENT_VALUE_CHANGED,NULL);}else if(e.index==6)enc_action(A_REVEAL);else if(e.index==7)enc_action(A_NET_APPLY);else enc_action(500+(e.index<3?e.index-1:e.index-2));}
}
static void enc_modal_press(void){
    if(u.modal_kind==5){if(e.modal_index<7){e.modal_index++;return;}enc_action(e.modal_index==7?A_MODAL_CANCEL:e.modal_index==8?A_TIME_SYNC:A_TIME_OK);return;}
    unsigned keys=u.keyboard?(unsigned)nui_native_keyboard_get_keys(u.keyboard,NULL,NULL,0):0;
    if(e.modal_index<keys){nui_area_t a;enc_modal_object(&a);nui_native_touch((a.x1+a.x2)/2,(a.y1+a.y2)/2,true);nui_native_touch((a.x1+a.x2)/2,(a.y1+a.y2)/2,false);e.modal_index=enc_clamp(e.modal_index,0,enc_modal_count());return;}
    nui_obj_t *obj=enc_modal_object(NULL);if(obj&&!nui_obj_has_state(obj,NUI_STATE_DISABLED))nui_obj_send_event(obj,NUI_EVENT_CLICKED,NULL);
}
static void enc_time_rotate(int32_t delta){
    if(e.modal_index==6){timezone_nudge(delta);return;}
    if(e.modal_index>=7){e.modal_index=enc_clamp(e.modal_index,delta,10);return;}
    static const unsigned minimum[]={2000,1,1,0,0,0},maximum[]={2099,12,31,23,59,59};
    unsigned i=e.modal_index;const char *old=nui_textarea_get_text(u.date_fields[i]);int value=*old?atoi(old):(int)minimum[i];
    int64_t next=(int64_t)value+delta;if(next<minimum[i])next=minimum[i];if(next>maximum[i])next=maximum[i];
    char b[12];snprintf(b,sizeof(b),"%u",(unsigned)next);nui_textarea_set_text(u.date_fields[i],b);date_edited(NULL);
}
void tab5_ui_input(tab5_ui_input_event_t input,int32_t delta,uint32_t time_ms){
    if(!u.root)return;
    if(input==TAB5_UI_INPUT_TOUCH){e.ring=false;e.rotating=false;e.streak=0;encoder_paint();return;}
    if(input==TAB5_UI_INPUT_IDLE){if(e.ring&&(uint32_t)(time_ms-e.last_ms)>=15000){e.ring=false;e.rotating=false;if(e.hint)hide(e.hint,true);encoder_paint();}return;}
    if(input>TAB5_UI_INPUT_TOUCH||(input==TAB5_UI_INPUT_ROT&&!delta))return;
    bool asleep=!e.ring||(uint32_t)(time_ms-e.last_ms)>=15000;e.last_ms=time_ms;
    if(asleep){e.ring=true;e.rotating=false;e.streak=0;encoder_paint();return;}
    e.injecting=true;
    if(input!=TAB5_UI_INPUT_ROT){e.rotating=false;e.streak=0;}
    if(u.modal_kind){
        if(input==TAB5_UI_INPUT_LONG)close_modal();
        else if(input==TAB5_UI_INPUT_PRESS)enc_modal_press();
        else if(u.modal_kind==5)enc_time_rotate(delta);
        else e.modal_index=enc_clamp(e.modal_index,delta,enc_modal_count());
    }else if(e.level==ENC_EDIT){
        if(input==TAB5_UI_INPUT_ROT){int direction=delta>0?1:-1;uint64_t ticks=delta>0?(uint64_t)delta:(uint64_t)(-(int64_t)delta);/* same timestamp for a batched group */
            if(ticks>4096)ticks=4096;
            for(uint64_t i=0;i<ticks;i++)enc_edit_nudge(direction,enc_acceleration(direction,time_ms));}
        else {if(input==TAB5_UI_INPUT_LONG&&!e.cursor){if(e.edit<8){u.draft[e.edit]=e.original;refresh_settings();}else if(e.original_light!=u.state.light_theme)enc_action(e.original_light?A_LIGHT:A_DARK);}e.level=ENC_SELECT;e.cursor=false;if(e.cursor_line)hide(e.cursor_line,true);if(e.hint)hide(e.hint,true);}
    }else if(e.level==ENC_MENU){
        if(input==TAB5_UI_INPUT_ROT){ask_navigation(enc_clamp(u.page,delta,enc_page_count()));if(u.page==GRAPH)chart_refresh(true);}
        else if(input==TAB5_UI_INPUT_LONG)ask_navigation(MEASURE);
        else if(enc_count()){e.level=ENC_SELECT;e.index=0;}else enc_hint("на этой странице нет кнопок — только показания");
    }else {
        if(input==TAB5_UI_INPUT_LONG)e.level=ENC_MENU;
        else if(input==TAB5_UI_INPUT_ROT)e.index=enc_clamp(e.index,delta,enc_count());
        else {nui_obj_t *obj=enc_object(e.index);if(obj&&!nui_obj_has_state(obj,NUI_STATE_DISABLED))enc_activate();}
    }
    e.injecting=false;encoder_paint();
}

void tab5_ui_touch(int32_t x,int32_t y,bool pressed,uint32_t time_ms){
    if(!u.root)return;
    if(pressed&&!e.touch_down){
        tab5_ui_input(TAB5_UI_INPUT_TOUCH,0,time_ms);e.touch_down=true;e.dragging=false;e.touch_x=x;e.touch_y=y;e.touch_view=NULL;e.touch_content=NULL;e.touch_scroll=NULL;
        if(!u.modal_kind){
            if(u.page==SETTINGS){e.touch_view=u.settings_view;e.touch_content=u.settings_content;e.touch_scroll=&e.settings_offset;}
            else if(u.page==NETWORK){e.touch_view=u.network_view;e.touch_content=u.network_content;e.touch_scroll=&e.network_offset;}
            if(e.touch_view){nui_area_t a;nui_obj_get_coords(e.touch_view,&a);if(x<a.x1||x>a.x2||y<a.y1||y>a.y2)e.touch_view=NULL;}
            if(e.touch_view)e.touch_offset=*e.touch_scroll;
        }else if(u.keyboard){
            nui_area_t areas[128];size_t count=nui_native_keyboard_get_keys(u.keyboard,areas,NULL,128);
            for(size_t i=0;i<count&&i<128;i++)if(x>=areas[i].x1&&x<=areas[i].x2&&y>=areas[i].y1&&y<=areas[i].y2){e.modal_index=(unsigned)i;break;}
        }
    }
    if(pressed&&e.touch_down&&e.touch_view){
        int64_t dx=(int64_t)x-e.touch_x,dy=(int64_t)y-e.touch_y;
        if(!e.dragging&&(dx>12||dx<-12||dy>12||dy<-12)){
            /* Release outside every object: native drops capture without CLICKED
             * and notifies RELEASED, stopping the stepper's repeat timer. */
            nui_native_touch(-100,-100,false);e.dragging=true;
        }
        if(e.dragging){
            int64_t offset=(int64_t)e.touch_offset-dy;int max=nui_obj_get_height(e.touch_content)-nui_obj_get_height(e.touch_view);if(max<0)max=0;
            if(offset<0)offset=0;
            if(offset>max)offset=max;
            *e.touch_scroll=(int)offset;nui_obj_set_y(e.touch_content,-(int)offset);
            enc_scroll_thumb(e.touch_view,e.touch_content,(int)offset,e.touch_view==u.settings_view?0:1);return;
        }
    }
    if(!pressed){bool dragged=e.dragging;e.touch_down=false;e.dragging=false;e.touch_view=NULL;if(dragged)return;}
    nui_native_touch(x,y,pressed);
}
static void chart_touch(nui_event_t *ev){
    encoder_touch(u.chart_touch,0);
    nui_indev_t *dev=nui_indev_active();if(!dev)return;nui_point_t p;nui_indev_get_point(dev,&p);nui_area_t a;nui_obj_get_coords(u.chart,&a);int x=p.x-a.x1;if(x<0)x=0;if(x>TAB5_PLOT_WIDTH-1)x=TAB5_PLOT_WIDTH-1;unsigned n=u.range==0?300:u.range==1?240:1200,idx=(unsigned)(x*(n-1)/(TAB5_PLOT_WIDTH-1));char s[180];size_t used=0;
    if(u.state.live_unfiltered)used+=snprintf(s+used,sizeof(s)-used,"Угол без сглаживания · ");
    used+=snprintf(s+used,sizeof(s)-used,"−%u с",(unsigned)((n-1-idx)*(u.range?500:100)/1000));for(unsigned i=0;i<4;i++){int32_t v=u.plotted[i][idx];if(u.visible[i]&&v!=NUI_CHART_POINT_NONE)used+=snprintf(s+used,sizeof(s)-used,"  Д%u %c %+.2f°",i/2+2,i%2?'Y':'X',v/100.0);if(used>=sizeof(s)-1)break;}text(u.cursor_label,s);
    (void)ev;
}
static void chart_refresh(bool force){
    if(!u.chart_cached||u.chart_live_unfiltered!=u.state.live_unfiltered){
        text(u.cursor_label,u.state.live_unfiltered?"Угол без сглаживания · Коснитесь графика для отсчёта":"Коснитесь графика для отсчёта");
        u.chart_live_unfiltered=u.state.live_unfiltered;
    }
    /* Current numeric cards remain 10Hz even when a plot pass is deferred. */
    for(unsigned k=0;k<4;k++){
        const tab5_measure_sensor_t *m=&u.state.measurement.sensor[k/2];
        float value=k%2?m->y:m->x;
        int32_t latest=m->valid&&m->good&&m->timestamp_us&&isfinite(value)&&fabsf(value)<=327.66f?(int32_t)lroundf(value*100):NUI_CHART_POINT_NONE;
        u.chart_latest[k]=latest;
        char b[48];if(!u.visible[k])snprintf(b,sizeof(b),"скрыт");else if(latest==NUI_CHART_POINT_NONE)snprintf(b,sizeof(b),"—");else snprintf(b,sizeof(b),"%s%.2f°",latest<0?"−":"+",fabs(latest/100.0));text(u.stats[k],b);
    }
    uint64_t now=u.state.now_known?u.state.now_us:(uint64_t)nui_tick_get()*1000;
    bool controls=!u.chart_cached||u.chart_light!=u.light||u.chart_range!=u.range||memcmp(u.chart_visible,u.visible,sizeof(u.visible));
    if(!force&&!controls&&now>=u.chart_plot_us&&now-u.chart_plot_us<200000)return;
    u.chart_plot_us=now;
    unsigned seconds=u.range==0?30:u.range==1?120:600;int32_t min=0,max=0;bool any=false;unsigned n=0;
    bool changed=!u.chart_cached||u.chart_light!=u.light||u.chart_range!=u.range||memcmp(u.chart_visible,u.visible,sizeof(u.visible));
    /* Read one series at a time into persistent storage: the UI task stack is small. */
    for(unsigned k=0;k<4;k++){
        n=tab5_ui_graph_read_render(&u.graph,seconds,k,u.chart_scratch);
        size_t bytes=n*sizeof(u.chart_scratch[0]);
        if(!u.chart_cached||u.chart_count!=n||memcmp(u.plotted[k],u.chart_scratch,bytes)){
            memcpy(u.plotted[k],u.chart_scratch,bytes);changed=true;
        }
    }
    if(!changed)return;
    u.chart_cached=true;u.chart_count=n;u.chart_range=u.range;u.chart_light=u.light;
    memcpy(u.chart_visible,u.visible,sizeof(u.visible));
    for(unsigned k=0;k<4;k++){if(u.visible[k])for(unsigned i=0;i<n;i++)if(u.plotted[k][i]!=NUI_CHART_POINT_NONE){int32_t v=u.plotted[k][i];if(!any){min=max=v;any=true;}if(v<min)min=v;if(v>max)max=v;}}
    u.ymin=any?(int32_t)(floor(min/50.0)*50):-100;u.ymax=any?(int32_t)(ceil(max/50.0)*50):100;if(u.ymax-u.ymin<100){u.ymin-=50;u.ymax+=50;}
    int32_t tick=50;unsigned ticks;
    do {u.ymin=(int32_t)floor((any?min:-100)/(double)tick)*tick;u.ymax=(int32_t)ceil((any?max:100)/(double)tick)*tick;if(u.ymax<=u.ymin)u.ymax=u.ymin+tick*2;ticks=(unsigned)((u.ymax-u.ymin)/tick+1);if(ticks>9)tick+=50;}while(ticks>9);
    if(ticks<3){u.ymin-=tick;u.ymax+=tick;ticks+=2;}
    nui_chart_set_point_count(u.chart,n);nui_chart_set_axis_range(u.chart,NUI_CHART_AXIS_PRIMARY_Y,u.ymin,u.ymax);nui_chart_set_div_line_count(u.chart,ticks,0);
    for(unsigned k=0;k<4;k++){
        nui_color_t series_color=nui_color_hex(u.sensor[k/2]);
        if(k%2)series_color=nui_color_mix(nui_color_hex(u.light?0x000000:0xFFFFFF),series_color,64);
        for(uint32_t c=1;c<nui_obj_get_child_count(u.legend[k]);c++)style_color(nui_obj_get_child(u.legend[k],c),NUI_STYLE_BG_COLOR,series_color,0);
        style_color(nui_obj_get_child(nui_obj_get_parent(u.stats[k]),0),NUI_STYLE_BG_COLOR,series_color,0);nui_chart_set_series_ext_y_array(u.chart,u.series[k],u.plotted[k]);nui_chart_set_series_color(u.chart,u.series[k],series_color);nui_chart_hide_series(u.chart,u.series[k],!u.visible[k]);
        int32_t mn=0,mx=0,last=NUI_CHART_POINT_NONE;bool found=false;
        for(unsigned i=0;i<n;i++)if(u.plotted[k][i]!=NUI_CHART_POINT_NONE){int32_t v=u.plotted[k][i];if(!found)mn=mx=v;found=true;if(v<mn)mn=v;if(v>mx)mx=v;}
        /* The last 100ms cell can be empty between healthy 5Hz samples.
         * Show the current real sensor value; do not extrapolate the plot tail. */
        last=u.chart_latest[k];
        char s[120];if(!u.visible[k])snprintf(s,sizeof(s),"скрыт");else if(last!=NUI_CHART_POINT_NONE)snprintf(s,sizeof(s),"%s%.2f°",last<0?"−":"+",fabs(last/100.0));else snprintf(s,sizeof(s),"—");text(u.stats[k],s);
        if(found&&u.visible[k])snprintf(s,sizeof(s),"%.2f … %.2f · размах %.2f",mn/100.0,mx/100.0,(mx-mn)/100.0);else snprintf(s,sizeof(s),u.visible[k]?"данных нет":"включается касанием подписи");text(u.stats_mm[k],s);
    }
    for(unsigned i=0;i<9;i++){hide(u.ylabel[i],i>=ticks);if(i>=ticks)continue;double v=(u.ymax-(int)i*tick)/100.;char s[32];snprintf(s,sizeof(s),"%+.1f°",v);text(u.ylabel[i],s);nui_obj_set_y(u.ylabel[i],30+(int)(364*i/(ticks-1)));}
    for(unsigned i=0;i<7;i++){hide(u.xlabel[i],i>=5);if(i>=5)continue;nui_obj_set_x(u.xlabel[i],i==4?972:52+(int)i*240);char s[24];snprintf(s,sizeof(s),i==4?"0 с":"−%u с",seconds-seconds*i/4);text(u.xlabel[i],s);}
    for(unsigned i=0;i<3;i++)role(u.ranges[i],i==u.range?PRIMARY:FRAME);
    style_color(u.chart,NUI_STYLE_LINE_COLOR,nui_color_hex(u.border),NUI_PART_MAIN);
    if(u.plot_pixels){
        tab5_plot_t plot={.y_min=u.ymin,.y_max=u.ymax,.none=NUI_CHART_POINT_NONE,.ticks=ticks,.n=n,
            .background=nui_color_to_u16(nui_color_hex(u.card)),.grid=nui_color_to_u16(nui_color_hex(u.border))};
        for(unsigned k=0;k<4;k++){
            nui_color_t color=nui_color_hex(u.sensor[k/2]);
            if(k%2)color=nui_color_mix(nui_color_hex(u.light?0x000000:0xFFFFFF),color,64);
            plot.colors[k]=nui_color_to_u16(color);plot.series[k]=u.plotted[k];plot.visible[k]=u.visible[k];
        }
        tab5_plot_draw(u.plot_pixels,TAB5_PLOT_WIDTH,&plot);
        nui_image_cache_drop(&u.plot_image);nui_obj_invalidate(u.plot_image_obj);
    }else nui_chart_refresh(u.chart);
}

static void build_measure(void){
    nui_obj_t *p=u.pages[MEASURE];
    u.hero=box(p,0,0,1096,88,NAV);nui_obj_set_style_radius(u.hero,22,0);
    nui_obj_set_style_bg_grad_dir(u.hero,NUI_GRAD_DIR_HOR,0);style_color(u.hero,NUI_STYLE_BG_GRAD_COLOR,nui_color_hex(u.navsel),0);nui_obj_add_event_cb(u.hero,hero_draw,NUI_EVENT_DRAW_MAIN,NULL);
    u.hero_dot=box(u.hero,30,30,30,30,ORANGE);nui_obj_set_style_radius(u.hero_dot,15,0);nui_obj_set_style_outline_width(u.hero_dot,8,0);nui_obj_set_style_outline_opa(u.hero_dot,70,0);
    u.hero_title=label(u.hero,"Покой",78,10,470,72,F56,NAVTEXT);
    u.hero_sub=label(u.hero,"",560,30,510,42,F28,NAVTEXT);style_color(u.hero_sub,NUI_STYLE_TEXT_COLOR,nui_color_hex(0xDCE5F5),0);
    for(unsigned i=0;i<2;i++){
        nui_obj_t *c=u.sensor_card[i]=box(p,0,100+i*216,1096,204,CARD);nui_obj_set_style_radius(c,20,0);
        nui_obj_t *dot=box(c,28,44,22,22,ALT);nui_obj_set_style_radius(dot,11,0);style_color(dot,NUI_STYLE_BG_COLOR,nui_color_hex(u.sensor[i]),0);
        label(c,i?"Д3":"Д2",70,22,150,68,F56,TEXT);
        u.health[i]=chip(c,"✓ связь",28,104,166,OK);nui_obj_set_height(u.health[i],48);style_font(u.health[i],F28,0);style_num(u.health[i],NUI_STYLE_PAD_TOP,7,0);
        for(int a=0;a<2;a++){
            label(c,a?"Y":"X",244+a*428,76,36,58,F48,MUTED);
            u.angle[i][a]=label(c,"—",280+a*428,44,380,138,F108,TEXT);
        }
        u.measure_roll[i]=label(c,"",250,154,818,38,F28,MUTED);
        u.lost[i]=box(c,250,24,818,148,ERR);nui_obj_set_style_radius(u.lost[i],16,0);
        label(u.lost[i],"Нет ответа",22,12,760,56,F48,ERR);
        u.lost_text[i]=label(u.lost[i],"Проверьте питание и RS485",22,76,760,60,F28,ERR);
    }
    u.start[0]=button(p,"● Старт записи",0,532,500,104,ORANGE,A_START);
    button(p,"Ноль",516,532,282,104,PRIMARY,A_ZERO);
    button(p,"Сброс нуля",814,532,282,104,OUTLINE,A_RESET);
    u.zero_chip=label(p,"",0,0,1,1,F16,MUTED);hide(u.zero_chip,true);
}
static void build_roll(void){
    nui_obj_t *p=u.pages[ROLL];int x=page_title(p,"Качка");
    u.roll_window=chip(p,"окно —",x,16,226,WARN);nui_obj_set_height(u.roll_window,48);style_font(u.roll_window,F28,0);
    u.roll_calm=label(p,"порог покоя —",x+248,24,550,40,F28,MUTED);
    for(unsigned i=0;i<2;i++){
        nui_obj_t *c=box(p,0,92+i*282,1096,262,CARD);nui_obj_set_style_radius(c,20,0);
        nui_obj_t *dot=box(c,28,42,22,22,ALT);nui_obj_set_style_radius(dot,11,0);style_color(dot,NUI_STYLE_BG_COLOR,nui_color_hex(u.sensor[i]),0);
        label(c,i?"Датчик 3":"Датчик 2",70,18,600,66,F48,TEXT);
        u.roll_status[i]=chip(c,"нет данных",880,22,188,NEUTRAL);nui_obj_set_height(u.roll_status[i],48);style_font(u.roll_status[i],F28,0);
        for(unsigned a=0;a<2;a++){
            label(c,a?"Y":"X",28,100+a*80,50,60,F40,MUTED);
            nui_obj_t *bar=u.roll_bar[i][a]=nui_bar_create(c);nui_obj_remove_style_all(bar);nui_obj_set_pos(bar,94,112+a*80);nui_obj_set_size(bar,564,30);nui_bar_set_range(bar,0,1000);role(bar,FRAME);nui_obj_set_style_radius(bar,15,0);nui_obj_set_style_radius(bar,15,NUI_PART_INDICATOR);nui_obj_set_style_bg_opa(bar,255,NUI_PART_INDICATOR);
            u.roll_mark[i][a]=box(c,375,100+a*80,4,52,LINE);
            u.roll_value[i][a]=label(c,"—",676,94+a*80,180,74,F56,TEXT);nui_obj_set_style_text_align(u.roll_value[i][a],NUI_TEXT_ALIGN_RIGHT,0);
            u.roll_chip[i][a]=chip(c,"нет данных",878,102+a*80,190,NEUTRAL);nui_obj_set_height(u.roll_chip[i][a],48);style_font(u.roll_chip[i][a],F28,0);
        }
    }
}
static void build_graph(void){
    nui_obj_t *p=u.pages[GRAPH];page_title(p,"График");
    const char *names[]={"Д2 · X","Д2 · Y","Д3 · X","Д3 · Y"};for(int i=0;i<4;i++){u.legend[i]=button(p,names[i],i*176,556,160,80,NEUTRAL,600+i);style_font(nui_obj_get_child(u.legend[i],0),F28,0);nui_obj_set_style_radius(u.legend[i],28,0);u.visible[i]=true;for(int k=0;k<1;k++){nui_obj_t *swatch=box(u.legend[i],16+k*12,39,24,4,ORANGE);style_color(swatch,NUI_STYLE_BG_COLOR,nui_color_hex(u.sensor[i/2]),0);}nui_obj_t *ll=nui_obj_get_child(u.legend[i],0);nui_obj_set_align(ll,NUI_ALIGN_TOP_LEFT);nui_obj_set_pos(ll,50,24);nui_obj_set_width(ll,108);nui_obj_set_style_text_align(ll,NUI_TEXT_ALIGN_LEFT,0);}
    const char *ranges[]={"30 с","2 мин","10 мин"};for(int i=0;i<3;i++){u.ranges[i]=button(p,ranges[i],620+i*158,0,158,80,FRAME,610+i);style_font(nui_obj_get_child(u.ranges[i],0),F28,0);}
    u.range=1;nui_obj_t *card=box(p,0,92,1096,446,CARD);
    u.chart=nui_chart_create(card);u.chart_touch=u.chart;nui_obj_remove_style_all(u.chart);nui_obj_set_pos(u.chart,112,44);nui_obj_set_size(u.chart,TAB5_PLOT_WIDTH,364);nui_obj_set_style_line_width(u.chart,1,NUI_PART_MAIN);style_color(u.chart,NUI_STYLE_LINE_COLOR,nui_color_hex(u.border),NUI_PART_MAIN);nui_obj_set_style_line_width(u.chart,2,NUI_PART_ITEMS);nui_obj_set_style_size(u.chart,0,0,NUI_PART_INDICATOR);nui_obj_set_style_pad_all(u.chart,0,0);nui_obj_remove_flag(u.chart,NUI_OBJ_FLAG_SCROLLABLE);
    nui_chart_set_type(u.chart,NUI_CHART_TYPE_LINE);nui_chart_set_point_count(u.chart,240);
    for(unsigned i=0;i<4;i++){u.series[i]=nui_chart_add_series(u.chart,nui_color_hex(u.sensor[i/2]),NUI_CHART_AXIS_PRIMARY_Y);for(unsigned j=0;j<1200;j++)u.plotted[i][j]=NUI_CHART_POINT_NONE;}
    /* One opaque RGB565 blit replaces thousands of antialiased LVGL line
     * tasks. Keep chart series as the cursor/test data model and fallback.
     * The buffer belongs in PSRAM, never in scarce internal SRAM. */
    size_t plot_bytes=TAB5_PLOT_WIDTH*TAB5_PLOT_HEIGHT*sizeof(uint16_t);
#ifdef ESP_PLATFORM
    u.plot_pixels=heap_caps_aligned_alloc(64,plot_bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    u.plot_pixels=malloc(plot_bytes);
#endif
    if(u.plot_pixels){
        tab5_plot_t blank={.y_min=-100,.y_max=100,.none=NUI_CHART_POINT_NONE,.background=nui_color_to_u16(nui_color_hex(u.card))};
        tab5_plot_draw(u.plot_pixels,TAB5_PLOT_WIDTH,&blank);
        u.plot_image=(nui_image_dsc_t){.header={.magic=NUI_IMAGE_HEADER_MAGIC,.cf=NUI_COLOR_FORMAT_RGB565,
            .w=TAB5_PLOT_WIDTH,.h=TAB5_PLOT_HEIGHT,.stride=TAB5_PLOT_WIDTH*2},
            .data_size=plot_bytes,.data=(const uint8_t*)u.plot_pixels};
        u.plot_image_obj=nui_image_create(u.chart);nui_obj_remove_style_all(u.plot_image_obj);
        nui_image_set_src(u.plot_image_obj,&u.plot_image);nui_obj_set_pos(u.plot_image_obj,0,0);
        nui_obj_remove_flag(u.plot_image_obj,NUI_OBJ_FLAG_CLICKABLE|NUI_OBJ_FLAG_SCROLLABLE);
        nui_obj_set_style_line_opa(u.chart,0,NUI_PART_MAIN);nui_obj_set_style_line_opa(u.chart,0,NUI_PART_ITEMS);
    }
    nui_obj_add_flag(u.chart,NUI_OBJ_FLAG_CLICKABLE);nui_obj_add_event_cb(u.chart,chart_touch,NUI_EVENT_PRESSED,NULL);nui_obj_add_event_cb(u.chart,chart_touch,NUI_EVENT_PRESSING,NULL);
    for(int i=0;i<9;i++)u.ylabel[i]=label(card,"",4,30+i*45,96,36,F28,MUTED);
    for(int i=0;i<7;i++){u.xlabel[i]=label(card,"",44+i*160,408,120,36,F28,MUTED);nui_obj_set_style_text_align(u.xlabel[i],NUI_TEXT_ALIGN_CENTER,0);}
    u.cursor_label=label(card,"Коснитесь графика для отсчёта",112,2,960,36,F24,MUTED);
    for(int i=0;i<4;i++){card=box(p,0,0,1,1,CARD);hide(card,true);nui_obj_t *dot=box(card,20,27,12,12,ORANGE);nui_obj_set_style_radius(dot,6,0);style_color(dot,NUI_STYLE_BG_COLOR,nui_color_hex(u.sensor[i/2]),0);label(card,names[i],42,20,212,28,F20,TEXT);u.stats[i]=label(card,"—",20,58,234,48,F32,TEXT);u.stats_mm[i]=label(card,"данных нет",20,108,234,28,F16,MUTED);}
}
static void build_record(void){
    nui_obj_t *p=u.pages[RECORD];int tx=page_title(p,"Запись");
    u.rec_chip=chip(p,"остановлена",tx,16,292,NEUTRAL);nui_obj_set_height(u.rec_chip,48);style_font(u.rec_chip,F28,0);
    nui_obj_t *c=box(p,0,92,1096,220,CARD);nui_obj_set_style_radius(c,20,0);
    label(c,"последний файл",28,22,570,42,F28,MUTED);
    u.rec_file_label=label(c,"—",28,62,550,68,F40,TEXT);
    u.rec_detail_label=label(c,"",28,126,550,72,F28,DIM);
    u.start[1]=button(c,"● Старт записи",628,58,440,104,ORANGE,A_START);
    for(int i=0;i<2;i++){
        c=box(p,i*556,328,540,308,CARD);nui_obj_set_style_radius(c,20,0);
        label(c,i?"USB":"microSD",28,24,260,64,F48,TEXT);
        u.media_chip[i]=chip(c,"—",352,24,160,NEUTRAL);nui_obj_set_height(u.media_chip[i],48);style_font(u.media_chip[i],F28,0);
        u.capacity_text[i]=label(c,"свободное место: —",28,112,484,48,F36,TEXT);
        u.capacity_bar[i]=nui_bar_create(c);nui_obj_set_pos(u.capacity_bar[i],28,170);nui_obj_set_size(u.capacity_bar[i],484,20);nui_bar_set_range(u.capacity_bar[i],0,1000);
        u.media_detail[i]=label(c,"",28,208,484,92,F28,MUTED);nui_label_set_long_mode(u.media_detail[i],NUI_LABEL_LONG_WRAP);
    }
}
static void stepper(nui_obj_t *c,unsigned i,int y){
    nui_obj_t *r=u.setting_rows[i]=box(c,0,y,1094,112,TEXT);
    label(r,param_name[i],28,16,590,48,F36,TEXT);label(r,limits[i],28,64,590,34,F24,MUTED);
    nui_obj_t *b=button(r,"−",642,12,94,86,OUTLINE,200+i*2);nui_obj_add_event_cb(b,step_press,NUI_EVENT_PRESSED,(void*)(intptr_t)(200+i*2));nui_obj_add_event_cb(b,step_press,NUI_EVENT_RELEASED,NULL);nui_obj_add_event_cb(b,step_press,NUI_EVENT_PRESS_LOST,NULL);
    u.step_value[i]=button(r,"",750,12,208,86,FRAME,300+i);style_font(nui_obj_get_child(u.step_value[i],0),F48,0);
    b=button(r,"+",972,12,94,86,OUTLINE,201+i*2);nui_obj_add_event_cb(b,step_press,NUI_EVENT_PRESSED,(void*)(intptr_t)(201+i*2));nui_obj_add_event_cb(b,step_press,NUI_EVENT_RELEASED,NULL);nui_obj_add_event_cb(b,step_press,NUI_EVENT_PRESS_LOST,NULL);
    line(r,0,111,1094);
}
static void build_settings(void){
    nui_obj_t *p=u.pages[SETTINGS];int tx=page_title(p,"Настройки");
    u.changed=chip(p,"изменено: 0",tx,16,280,INFO);nui_obj_set_height(u.changed,48);style_font(u.changed,F28,0);
    u.set_apply=button(p,"Применить",836,0,260,80,PRIMARY,A_APPLY);
    u.set_cancel=button(p,"Отменить",636,8,188,64,GHOST,A_CANCEL);style_font(nui_obj_get_child(u.set_cancel,0),F28,0);
    u.settings_view=box(p,0,96,1096,540,CARD);nui_obj_set_style_radius(u.settings_view,20,0);
    nui_obj_t *c=u.settings_content=box(u.settings_view,1,0,1094,1490,TEXT);
    nui_obj_t *g=box(c,0,0,1094,64,ALT);label(g,"ОПРОС ДАТЧИКОВ",28,14,1030,44,F28,MUTED);
    for(unsigned i=0;i<3;i++)stepper(c,i,64+i*112);
    g=box(c,0,400,1094,64,ALT);label(g,"КАЧКА И ПОКОЙ",28,14,1030,44,F28,MUTED);
    for(unsigned i=3;i<7;i++)stepper(c,i,464+(i-3)*112);
    g=box(c,0,912,1094,64,ALT);label(g,"ПРИБОР",28,14,1030,44,F28,MUTED);
    stepper(c,7,976);u.brightness_value=u.step_value[7];
    nui_obj_t *r=u.setting_rows[8]=box(c,0,1088,1094,112,TEXT);label(r,"Тема",28,28,580,56,F36,TEXT);
    u.theme_btn[0]=button(r,"Светлая",642,12,208,86,FRAME,A_LIGHT);u.theme_btn[1]=button(r,"Тёмная",864,12,202,86,FRAME,A_DARK);line(r,0,111,1094);
    r=u.setting_rows[9]=box(c,0,1200,1094,112,TEXT);label(r,"Дата и время",28,12,500,48,F36,TEXT);u.clock_text=label(r,"—",28,64,600,34,F24,MUTED);button(r,"Изменить",750,12,316,86,OUTLINE,A_TIME);line(r,0,111,1094);
    r=u.setting_rows[10]=box(c,0,1312,1094,176,TEXT);label(r,"Экран и прошивка",28,8,1030,48,F36,TEXT);u.panel_text=label(r,"—",28,64,1030,34,F24,MUTED);u.fw_text=label(r,"—",28,108,1030,34,F24,MUTED);
}
static void build_network(void){
    nui_obj_t *p=u.pages[NETWORK];page_title(p,"Сеть");
    u.reveal=button(p,"Показать пароли",716,0,380,80,OUTLINE,A_REVEAL);style_font(nui_obj_get_child(u.reveal,0),F28,0);
    u.network_view=box(p,0,92,1096,456,CARD);nui_obj_set_style_radius(u.network_view,20,0);
    nui_obj_t *c=u.network_content=box(u.network_view,1,0,1094,704,TEXT);
    for(int i=0;i<2;i++){
        int y=i*352;
        nui_obj_t *r=u.net_rows[i*4]=box(c,0,y,1094,88,ALT);label(r,i?"Wi-Fi сеть":"Точка доступа прибора",28,20,700,54,F36,TEXT);
        u.net_switch[i]=nui_switch_create(r);nui_obj_set_pos(u.net_switch[i],954,16);nui_obj_set_size(u.net_switch[i],112,56);nui_obj_set_ext_click_area(u.net_switch[i],8);nui_obj_add_event_cb(u.net_switch[i],switch_changed,NUI_EVENT_VALUE_CHANGED,NULL);
        u.net_chip[i]=chip(r,"—",736,20,204,NEUTRAL);nui_obj_set_height(u.net_chip[i],48);style_font(u.net_chip[i],F28,0);
        for(int j=0;j<2;j++){
            unsigned k=i*2+j;r=u.net_rows[i*4+1+j]=box(c,0,y+88+j*88,1094,88,TEXT);
            nui_obj_t *b=u.net_fields[k]=button(r,"",0,0,1094,88,TEXT,500+k);nui_obj_delete(nui_obj_get_child(b,0));
            label(b,j?"пароль":i?"сеть":"имя сети",28,24,210,42,F28,MUTED);
            nui_obj_t *v=label(b,"",248,20,816,54,F36,TEXT);nui_label_set_long_mode(v,NUI_LABEL_LONG_DOT);line(r,0,87,1094);
        }
        r=u.net_rows[i*4+3]=box(c,0,y+264,1094,88,TEXT);label(r,"адрес",28,24,210,42,F28,MUTED);u.net_address[i]=label(r,"—",248,20,816,54,F36,TEXT);line(r,0,87,1094);
    }
    u.net_apply=button(p,"Применить",836,564,260,72,PRIMARY,A_NET_APPLY);
    u.net_hint=label(p,"",0,558,814,78,F24,MUTED);nui_label_set_long_mode(u.net_hint,NUI_LABEL_LONG_WRAP);
    u.tcp_chip=label(p,"",0,0,1,1,F16,MUTED);hide(u.tcp_chip,true);u.tcp_address=label(p,"",0,0,1,1,F16,MUTED);hide(u.tcp_address,true);
}
void tab5_ui_init(tab5_ui_action_cb_t cb,void *user){
    if(u.root){tab5_ui_destroy();}
    memset(&u,0,sizeof(u));encoder_reset();if(!role_prop)role_prop=nui_style_register_prop(0);u.cb=cb;u.user=user;palette(true);
    u.root=box(nui_screen_active(),0,0,1280,720,BG);u.rail=box(u.root,0,0,152,720,NAV);
    const char *names[]={"Углы","Качка","График","Запись","Настройки","Сеть"};int icons[]={0,1,13,2,3,4};
    for(unsigned i=0;i<6;i++){
        u.nav[i]=button(u.rail,"",0,i*120,152,120,NAV,100+i);nui_obj_add_flag(u.nav[i],NUI_OBJ_FLAG_PRESSED_PAINT_INERT);nui_obj_delete(nui_obj_get_child(u.nav[i],0));icon(u.nav[i],icons[i],52,18,48,NAVTEXT);
        nui_obj_t *l=label(u.nav[i],names[i],0,76,152,36,F24,NAVTEXT);nui_obj_set_style_text_align(l,NUI_TEXT_ALIGN_CENTER,0);
        u.selected[i]=box(u.nav[i],0,18,7,84,ORANGE);nui_obj_set_style_radius(u.selected[i],4,0);
    }
    u.badge=box(u.nav[0],116,16,14,14,RED);nui_obj_set_style_radius(u.badge,7,0);
    u.status_bar=box(u.root,152,0,1128,56,NAV);
    u.rail_time=label(u.status_bar,"—",16,12,300,40,F28,NAVTEXT);u.status_rec=chip(u.status_bar,"СТОП",324,8,146,NAVSEL);nui_obj_set_height(u.status_rec,40);style_font(u.status_rec,F28,0);
    u.rail_resources[0]=label(u.status_bar,"ЦП —",682,12,144,40,F28,NAVTEXT);
    u.rail_resources[1]=label(u.status_bar,"ОЗУ —",486,4,108,26,F16,NAVMUTED);u.rail_resources[2]=label(u.status_bar,"PS —",486,29,108,24,F16,NAVMUTED);
    u.rail_power=label(u.status_bar,"—",840,12,150,40,F28,NAVTEXT);u.status_media=label(u.status_bar,"SD USB",994,15,130,36,F20,NAVMUTED);
    for(unsigned i=0;i<6;i++)u.pages[i]=box(u.root,168,68,1096,636,TEXT);
    build_measure();build_roll();build_graph();build_record();build_settings();build_network();
    /* Production default: parent-matte cache, with renderer OOM fallback. */
    tab5_ui_set_scroll_cache_enabled(true);show_page(MEASURE);
    tab5_ui_state_t state={.light_theme=true};tab5_measure_config_default(&state.measurement.config);tab5_ui_update(&state);
}

static void elapsed(char *dest,size_t n,uint64_t seconds){snprintf(dest,n,"%02u:%02u:%02u",(unsigned)(seconds/3600),(unsigned)(seconds/60%60),(unsigned)(seconds%60));}
static uint64_t duration(void){if(u.state.rec_duration_known)return u.state.rec_duration_s;if(u.state.rec_known&&u.state.now_known&&u.state.now_us>=u.state.rec_started_us)return (u.state.now_us-u.state.rec_started_us)/1000000;return 0;}
static void update_record(void){
    char b[240],d[40];elapsed(d,sizeof(d),duration());text(u.rec_chip,u.state.recording?"● идёт запись":"остановлена");role(u.rec_chip,u.state.recording?ERR:NEUTRAL);
    text(u.rec_file_label,u.state.rec_known&&u.state.rec_file[0]?u.state.rec_file:"—");
    snprintf(b,sizeof(b),"%llu строк · %s · %s",(unsigned long long)(u.state.sd.recording?u.state.sd.written:u.state.usb.written),u.state.rec_duration_known?d:"время —",u.state.sd.recording&&u.state.usb.recording?"SD и USB":u.state.sd.recording?"SD":u.state.usb.recording?"USB":"запись выключена");text(u.rec_detail_label,b);
    const tab5_ui_medium_t *m[]={&u.state.sd,&u.state.usb};
    for(unsigned i=0;i<2;i++){
        const tab5_ui_capacity_t *c=&u.state.capacity[i];
        text(u.media_chip[i],!m[i]->known?"—":m[i]->failed?"✗ ошибка":m[i]->mounted?"✓ готов":"⚠ нет");role(u.media_chip[i],!m[i]->known?NEUTRAL:m[i]->failed?ERR:m[i]->mounted?OK:WARN);
        if(c->known&&c->total_bytes)snprintf(b,sizeof(b),"свободно %.1f ГБ",c->free_bytes/1073741824.);else snprintf(b,sizeof(b),m[i]->known&&!m[i]->mounted?"Вставьте носитель":"свободное место: —");text(u.capacity_text[i],b);
        hide(u.capacity_bar[i],!c->known||!c->total_bytes);
        if(c->known&&c->total_bytes)nui_bar_set_value(u.capacity_bar[i],(int32_t)(1000.*(c->total_bytes-fmin((double)c->free_bytes,(double)c->total_bytes))/c->total_bytes),NUI_ANIM_OFF);
        style_color(u.capacity_bar[i],NUI_STYLE_BG_COLOR,nui_color_hex(u.alt),0);style_color(u.capacity_bar[i],NUI_STYLE_BG_COLOR,nui_color_hex(u.action),NUI_PART_INDICATOR);
        if(c->details_known&&c->known)snprintf(b,sizeof(b),"%s · из %.1f ГБ\n%s",c->filesystem,c->total_bytes/1073741824.,c->estimate_known?"оценка записи доступна":"оценка времени: —");else if(c->details_known)snprintf(b,sizeof(b),"%s · объём: —\nоценка времени: —",c->filesystem);else snprintf(b,sizeof(b),"Нужна FAT32 / exFAT\nзапись на все носители");text(u.media_detail[i],b);
    }
}
static void update_measure(void){
    const tab5_ui_state_t *s=&u.state;char b[240];bool any=false,both=true;float worst=0;
    unsigned fill=s->measurement.sensor[0].roll_fill_s;
    for(unsigned i=0;i<2;i++){
        const tab5_measure_sensor_t *m=&s->measurement.sensor[i];
        bool valid=m->valid&&isfinite(m->x)&&isfinite(m->y);any|=valid;both&=valid;
        if(m->roll_fill_s<fill)fill=m->roll_fill_s;
        if(valid)worst=fmaxf(worst,fmaxf(m->roll_x,m->roll_y));
    }
    if(u.page==MEASURE){
        text(u.hero_title,!any?"Нет датчиков":s->recording?"Идёт запись":s->measurement.stable?"Покой":fill<s->measurement.config.roll_window_s?"Накопление":"Качка");
        snprintf(b,sizeof(b),"%s · окно %u / %u с",s->measurement.stable?"можно снимать отсчёт":"дождитесь покоя",fill,s->measurement.config.roll_window_s);
        const char *subtitle=!any?"проверьте питание и RS485":b;nui_point_t subtitle_size;nui_text_get_size(&subtitle_size,subtitle,F28,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);style_font(u.hero_sub,subtitle_size.x>510?F24:F28,0);text(u.hero_sub,subtitle);
        uint32_t dc=!any?u.err:s->recording?0xFF5A5F:s->measurement.stable?0xB8E039:0xF2B84B;
        style_color(u.hero_dot,NUI_STYLE_BG_COLOR,nui_color_hex(dc),0);style_color(u.hero_dot,NUI_STYLE_OUTLINE_COLOR,nui_color_hex(dc),0);
        for(unsigned i=0;i<2;i++){
            const tab5_measure_sensor_t *m=&s->measurement.sensor[i];bool valid=m->valid&&isfinite(m->x)&&isfinite(m->y);
            text(u.health[i],!valid?"✗ нет связи":m->fail_streak?"⚠ пропуск":"✓ связь");role(u.health[i],!valid?ERR:m->fail_streak?WARN:OK);hide(u.lost[i],valid);
            style_color(nui_obj_get_child(u.sensor_card[i],0),NUI_STYLE_BG_COLOR,nui_color_hex(u.sensor[i]),0);
            for(unsigned a=0;a<2;a++){
                hide(u.angle[i][a],!valid);float v=a?m->y:m->x;
                snprintf(b,sizeof(b),"%s%.2f°",v<0?"−":"+",fabsf(v));
                nui_point_t z;nui_text_get_size(&z,b,F108,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);
                style_font(u.angle[i][a],z.x<=380?F108:F64,0);nui_obj_set_y(u.angle[i][a],z.x<=380?44:76);text(u.angle[i][a],b);
            }
            snprintf(b,sizeof(b),"качка X %.2f° · Y %.2f° — %s",(double)m->roll_x,(double)m->roll_y,m->calm_x&&m->calm_y?"покой":"качка");text(u.measure_roll[i],valid?b:"нет данных");
            if(!valid){snprintf(b,sizeof(b),"Пропусков подряд: %u · проверьте питание и RS485",m->fail_streak);text(u.lost_text[i],b);}
        }
    }
    if(u.page==ROLL){
        snprintf(b,sizeof(b),"окно %u / %u с",fill,s->measurement.config.roll_window_s);text(u.roll_window,b);role(u.roll_window,fill<s->measurement.config.roll_window_s?WARN:OK);
        snprintf(b,sizeof(b),"порог покоя %.2f°",(double)s->measurement.config.roll_calm_deg);text(u.roll_calm,b);
        for(unsigned i=0;i<2;i++){
            const tab5_measure_sensor_t *m=&s->measurement.sensor[i];bool valid=m->valid&&isfinite(m->x)&&isfinite(m->y),calm=m->calm_x&&m->calm_y;
            text(u.roll_status[i],!valid?"нет данных":calm?"● покой":"▲ качка");role(u.roll_status[i],!valid?NEUTRAL:calm?OK:WARN);
            for(unsigned a=0;a<2;a++){
                float v=a?m->roll_y:m->roll_x;bool quiet=a?m->calm_y:m->calm_x;
                unsigned q=valid&&s->measurement.config.roll_calm_deg>0?(unsigned)fminf(1000,fmaxf(0,500*v/s->measurement.config.roll_calm_deg)):0;
                nui_bar_set_value(u.roll_bar[i][a],q,NUI_ANIM_OFF);style_color(u.roll_bar[i][a],NUI_STYLE_BG_COLOR,nui_color_hex(quiet?u.ok:u.warn),NUI_PART_INDICATOR);
                if(valid)snprintf(b,sizeof(b),"%.2f°",(double)v);else snprintf(b,sizeof(b),"—");text(u.roll_value[i][a],b);text(u.roll_chip[i][a],valid?(quiet?"покой":"качка"):"нет данных");role(u.roll_chip[i][a],!valid?NEUTRAL:quiet?OK:WARN);
            }
        }
    }
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
    if(!*s){if(u.notice)nui_obj_delete(u.notice);u.notice=u.notice_text=NULL;return;}
    bool error=backend_failure();
    if(!u.notice){u.notice=box(u.root,240,584,1024,120,INFO);nui_obj_set_layer_cache(u.notice,tab5_ui_scroll_cache_enabled());nui_obj_add_flag(u.notice,NUI_OBJ_FLAG_CLICKABLE);u.notice_text=label(u.notice,"",20,14,912,92,F36,TEXT);button(u.notice,"✗",948,32,56,56,GHOST,A_NOTICE_CLOSE);}
    role(u.notice,error?ERR:INFO);style_color(u.notice_text,NUI_STYLE_TEXT_COLOR,nui_color_hex(error?u.err:u.info),0);text(u.notice_text,s);nui_obj_move_foreground(u.notice);
}
void tab5_ui_update(const tab5_ui_state_t *s){
    if(!u.root||!s)return;
    /* Work on a borrowed snapshot copy only; never feed presentation values
     * back into acquisition, recording, roll or zero-offset calculation. */
    tab5_ui_state_t state=*s;
    if(state.live_unfiltered)for(unsigned i=0;i<TAB5_SENSOR_COUNT;i++){
        tab5_measure_sensor_t *m=&state.measurement.sensor[i];
        m->x=m->decoded_x-m->zero_x;
        m->y=m->decoded_y-m->zero_y;
    }
    u.state=state;
    encoder_sync(); /* Load clean/first brightness before dirty counting. */
    tab5_ui_graph_sample(&u.graph,&state);
    if(u.light!=state.light_theme){palette(state.light_theme);style_color(u.hero,NUI_STYLE_BG_GRAD_COLOR,nui_color_hex(u.navsel),0);}
    style_color(u.chart,NUI_STYLE_BG_COLOR,nui_color_hex(u.card),0);
    style_num(u.chart,NUI_STYLE_BG_OPA,255,0);style_num(u.chart,NUI_STYLE_RADIUS,0,0);
    style_num(u.chart,NUI_STYLE_BORDER_WIDTH,0,0);style_num(u.chart,NUI_STYLE_SHADOW_WIDTH,0,0);
    for(unsigned i=0;i<2;i++)for(unsigned axis=0;axis<2;axis++){
        nui_obj_t *o=u.angle[i][axis];style_color(o,NUI_STYLE_BG_COLOR,nui_color_hex(u.card),0);style_num(o,NUI_STYLE_BG_OPA,255,0);
    }
    for(unsigned k=0;k<4;k++){
        style_color(u.stats[k],NUI_STYLE_BG_COLOR,nui_color_hex(u.card),0);style_num(u.stats[k],NUI_STYLE_BG_OPA,255,0);style_num(u.stats[k],NUI_STYLE_RADIUS,0,0);
    }
    update_result();update_notice(state.status);if(u.notice_text)style_color(u.notice_text,NUI_STYLE_TEXT_COLOR,nui_color_hex(backend_failure()?u.err:u.info),0);
    if(u.pending&&!u.settings_failed&&(!state.action_result_known||u.settings_ack)&&cfg_equal(&state.measurement.config,&u.sent)){tab5_measure_config_t current=draft_config();u.pending=false;u.dirty=!cfg_equal(&current,&u.sent);}
    if(!u.dirty&&!u.pending)config_values(&state.measurement.config,u.draft);
    if(u.net_pending&&!u.network_failed&&(!state.action_result_known||u.network_ack)&&net_equal(&state.network,&u.net_sent)){u.net_pending=false;u.net_dirty=!net_equal(&u.net_draft,&u.net_sent);text(u.net_hint,"Сеть: настройки применены");role(u.net_hint,MUTED);}
    if(!u.net_dirty&&!u.net_pending&&u.modal_kind!=2)u.net_draft=state.network;
    refresh_settings();refresh_network();
    for(unsigned i=0;i<2;i++){
        role(u.theme_btn[i],state.light_theme==(i==0)?PRIMARY:DIM);
        style_color(u.net_switch[i],NUI_STYLE_BG_COLOR,nui_color_hex(u.action),NUI_PART_INDICATOR|NUI_STATE_CHECKED);style_color(u.net_switch[i],NUI_STYLE_BG_COLOR,nui_color_hex(u.light?0xFFFFFF:0x0B1733),NUI_PART_KNOB);
        text(u.net_address[i],!state.link.known?"—":i?(state.link.sta_connected?state.link.sta_ip:"—"):(state.link.ap_started?state.link.ap_ip:"—"));bool on=state.link.known&&(i?state.link.sta_connected:state.link.ap_started);text(u.net_chip[i],on?(i?"✓ подключено":"✓ работает"):"—");role(u.net_chip[i],on?OK:NEUTRAL);
    }
    char b[240];if(state.tcp_peer_known)snprintf(b,sizeof(b),"✓ клиент %s",state.tcp_peer);else if(state.tcp_clients_known)snprintf(b,sizeof(b),"клиентов %u · адрес —",state.tcp_clients);else snprintf(b,sizeof(b),"клиент: данных нет");text(u.tcp_chip,b);role(u.tcp_chip,state.tcp_clients_known&&state.tcp_clients?OK:NEUTRAL);
    if(state.link.known)snprintf(b,sizeof(b),"%s : %u",state.link.sta_connected?state.link.sta_ip:state.link.ap_started?state.link.ap_ip:"—",state.link.tcp_port);else snprintf(b,sizeof(b),"—");text(u.tcp_address,b);
    text(u.clock_text,state.clock.known&&state.clock.time_valid?state.clock.datetime:"Установите часы");text(u.panel_text,state.panel[0]?state.panel:"Экран: автоматический выбор");snprintf(b,sizeof(b),"Прошивка: %s",state.firmware[0]?state.firmware:"—");text(u.fw_text,b);
    if(state.cpu_known)snprintf(b,sizeof(b),"ЦП %u%%",state.cpu_load_pct>100?100u:(unsigned)state.cpu_load_pct);
    else snprintf(b,sizeof(b),"ЦП —");
    text(u.rail_resources[0],b);
    const uint32_t used[]={state.ram_used_bytes,state.psram_used_bytes};
    const uint32_t total[]={state.ram_total_bytes,state.psram_total_bytes};
    const char *resource_names[]={"ОЗУ","PS"};
    for(unsigned i=0;i<2;i++){
        if(state.resources_known&&total[i]){
            unsigned pct=used[i]>=total[i]?100u:(unsigned)((uint64_t)used[i]*100/total[i]);
            snprintf(b,sizeof(b),"%s %u%%",resource_names[i],pct);
        }else snprintf(b,sizeof(b),"%s —",resource_names[i]);
        text(u.rail_resources[i+1],b);
    }
    if(state.datetime_known)snprintf(b,sizeof(b),"%02u.%02u.%04u  %02u:%02u:%02u",state.datetime.day,state.datetime.month,state.datetime.year,state.datetime.hour,state.datetime.minute,state.datetime.second);else snprintf(b,sizeof(b),"Установите часы");text(u.rail_time,b);
    if(state.clock.known&&state.clock.battery_present&&isfinite(state.clock.battery_v))snprintf(b,sizeof(b),"%.2f В",(double)state.clock.battery_v);else snprintf(b,sizeof(b),"внешнее");text(u.rail_power,b);
    text(u.status_rec,state.recording?"● ЗАП":"● СТОП");role(u.status_rec,state.recording?RED:NAVSEL);
    snprintf(b,sizeof(b),"SD %s USB %s",state.sd.mounted?"✓":"—",state.usb.mounted?"✓":"—");text(u.status_media,b);
    for(unsigned i=0;i<2;i++){caption(u.start[i],state.recording?"■ Стоп записи":"● Старт записи");role(u.start[i],state.recording?RED:ORANGE);}
    uint64_t now=state.now_known?state.now_us:(uint64_t)nui_tick_get()*1000;if(!u.rendered||now<u.render_us||now-u.render_us>=100000){update_measure();if(u.page==RECORD)update_record();if(u.page==GRAPH)chart_refresh(false);u.rendered=true;u.render_us=now;}
    if(u.keyboard)keyboard_style(u.keyboard);
    encoder_sync();
}
void tab5_ui_destroy(void){memset(&e,0,sizeof(e));if(u.repeat)nui_timer_delete(u.repeat);if(u.root)nui_obj_delete(u.root);if(u.plot_pixels){nui_image_cache_drop(&u.plot_image);free(u.plot_pixels);}for(unsigned i=0;i<2;i++)if(u.level_pixels[i]){nui_image_cache_drop(&u.level_image[i]);free(u.level_pixels[i]);}if(u.styles_ready)for(int i=0;i<STYLE_COUNT;i++)nui_style_reset(&u.style[i]);memset(&u,0,sizeof(u));}
