/* Real production RGB565 UI + native encoder FIFO, no OS window/device. */
#ifndef POPUP_FOCUS_UI_SOURCE
#define POPUP_FOCUS_UI_SOURCE "../main/tab5_ui.c"
#endif
#include POPUP_FOCUS_UI_SOURCE
#include "tab5_native_port.h"
#include <assert.h>

static uint16_t pixels[1280*720],partial[1280*720];
static tab5_ui_state_t snapshot; /* Never put the history-bearing state on stack. */
static unsigned actions,checks;
static uint32_t ms;
static bool reply_on_action;
static tab5_ui_action_kind_t last_kind;
static const char *capture_dir;
static void frame_equal(void);

static void capture(const char *name,unsigned theme,tab5_mode_t mode){
    if(!capture_dir)return;
    frame_equal();char path[1024];
    snprintf(path,sizeof(path),"%s/%s_%s_%s.rgb565",capture_dir,
             mode==TAB5_MODE_LEVEL?"LEVEL":"HEEL",theme?"light":"dark",name);
    FILE *f=fopen(path,"wb");assert(f);
    assert(fwrite(pixels,sizeof(pixels),1,f)==1);assert(!fclose(f));
}

static void action(const tab5_ui_action_t *a,void *unused){
    (void)unused;actions++;last_kind=a->kind;
    if(reply_on_action){
        strcpy(snapshot.status,"Action completed");
        tab5_ui_update(&snapshot);
    }
}
static void advance(unsigned elapsed){ms+=elapsed;nui_native_tick(elapsed);}
static void pump(void){tab5_native_port_input_dispatch(ms);}
static void input(tab5_ui_input_event_t kind,int delta){
    advance(100);assert(tab5_native_port_post_input(kind,delta,ms));pump();
}
static void rotate(int delta){input(TAB5_UI_INPUT_ROT,delta);}
static void push(void){
    advance(100);assert(tab5_native_port_post_button(true,ms));pump();
    advance(50);assert(tab5_native_port_post_button(false,ms));pump();
}
static void touch(nui_obj_t *obj){
    nui_area_t a;nui_obj_get_coords(obj,&a);
    int x=(a.x1+a.x2)/2,y=(a.y1+a.y2)/2;
    advance(100);assert(tab5_native_port_post_touch(x,y,true,ms));pump();
    advance(30);assert(tab5_native_port_post_touch(x,y,false,ms));pump();
}
static void frame_equal(void){
    nui_native_render();memcpy(partial,pixels,sizeof(pixels));
    nui_obj_invalidate(u.root);nui_native_render();
    assert(!memcmp(partial,pixels,sizeof(pixels)));checks++;
}
static void close_selected(void){
    if(!(u.notice&&e.ring&&e.frame&&!nui_obj_has_flag(e.frame,NUI_OBJ_FLAG_HIDDEN)))
        fprintf(stderr,"Popup focus missing: status=%s ring=%u frame=%p level=%u index=%u page=%u\n",
                snapshot.status,e.ring,(void*)e.frame,e.level,e.index,u.page);
    assert(u.notice&&e.ring&&e.frame&&!nui_obj_has_flag(e.frame,NUI_OBJ_FLAG_HIDDEN));
    nui_area_t a,b;
    nui_obj_get_coords(nui_obj_get_child(u.notice,1),&a);
    nui_obj_get_coords(e.frame,&b);
    assert(b.x1==a.x1-4&&b.y1==a.y1-4&&b.x2==a.x2+4&&b.y2==a.y2+4);
    assert(!strcmp(nui_label_get_text(e.mode),"окно"));
    frame_equal();
}
static void notice(const char *message){
    strcpy(snapshot.status,message);tab5_ui_update(&snapshot);close_selected();
}
static void clear_status(void){snapshot.status[0]=0;tab5_ui_update(&snapshot);assert(!u.notice);}
static void start(unsigned theme,tab5_mode_t mode){
    assert(nui_native_init(1280,720,pixels));
    memset(&snapshot,0,sizeof(snapshot));actions=0;reply_on_action=false;ms=1000;
    tab5_ui_init(action,NULL);
    snapshot.light_theme=theme;snapshot.brightness_pct=60;
    snapshot.dual_mode_known=true;snapshot.mode=mode;
    snapshot.relative_config.reference_index=0;snapshot.relative_config.object_index=1;
    snapshot.datetime_known=true;snapshot.datetime=(tab5_ui_datetime_t){2026,11,5,12,30,0};
    tab5_measure_config_default(&snapshot.measurement.config);
    tab5_ui_update(&snapshot);
    tab5_native_port_input_reset();assert(tab5_native_port_backlight_configure(0,60));pump();
}
static void finish(void){tab5_ui_destroy();nui_native_deinit();}

static void contexts(unsigned theme,tab5_mode_t mode){
    start(theme,mode);
    /* First arrival immediately selects Close, even with initially hidden focus. */
    notice("Прибор готов. Перед записью установите часы");capture("popup_close",theme,mode);
    push();assert(!u.notice&&!e.ring&&actions==0);
    tab5_ui_update(&snapshot);assert(!u.notice); /* Dismissed text stays dismissed. */
    push();assert(e.ring&&e.level==ENC_MENU); /* One event wakes hidden focus. */
    push();rotate(1);assert(e.level==ENC_SELECT&&e.index==1);
    unsigned level=e.level,index=e.index,page=u.page;
    notice("Select context");
    rotate(INT32_MAX);rotate(INT32_MIN);close_selected();
    assert(e.level==level&&e.index==index&&u.page==page&&actions==0&&!u.modal_kind);
    push();assert(!u.notice&&e.ring&&e.level==level&&e.index==index&&actions==0);
    frame_equal();

    input(TAB5_UI_INPUT_LONG,0);rotate(1);assert(e.level==ENC_MENU&&u.page==ROLL);
    notice("Navigation context");rotate(-100);push();
    assert(!u.notice&&e.level==ENC_MENU&&u.page==ROLL&&actions==0);

    /* Capture an actual edit through menu/select/press input, preserve draft. */
    rotate(SETTINGS-ROLL);push();rotate(1);push();rotate(1);
    assert(e.level==ENC_EDIT&&e.edit==0&&u.dirty);
    double original=e.original,draft=u.draft[0];
    int offset=e.settings_offset;
    notice("Editing context");rotate(999);push();
    assert(!u.notice&&e.level==ENC_EDIT&&e.edit==0&&e.index==1);
    assert(e.original==original&&u.draft[0]==draft&&e.settings_offset==offset&&actions==0);
    frame_equal();
    rotate(1);assert(u.draft[0]>draft);
    input(TAB5_UI_INPUT_LONG,0);assert(u.draft[0]==original&&e.level==ENC_SELECT);

    /* A status can interrupt the numeric modal without losing its cursor/text. */
    touch(u.step_value[0]);assert(u.modal_kind==1);
    input(TAB5_UI_INPUT_ROT,1); /* wake after touch */
    rotate(2);unsigned modal_index=e.modal_index;char number[24];strcpy(number,u.num);
    notice("Over numeric dialog");rotate(-200);push();
    assert(!u.notice&&u.modal_kind==1&&e.modal_index==modal_index&&!strcmp(number,u.num));
    input(TAB5_UI_INPUT_LONG,0);assert(!u.modal_kind);

    /* Time modal: rotation must not change date/time while notice owns input. */
    rotate(10-(int)e.index);push();assert(u.modal_kind==5);
    rotate(2);modal_index=e.modal_index;
    char year[16];strcpy(year,nui_textarea_get_text(u.date_fields[0]));
    notice("Over time dialog");rotate(999);push();
    assert(!u.notice&&u.modal_kind==5&&e.modal_index==modal_index);
    assert(!strcmp(year,nui_textarea_get_text(u.date_fields[0])));
    input(TAB5_UI_INPUT_LONG,0);assert(!u.modal_kind);

    /* New text and typed same-text results refocus Close; retain one saved ring. */
    advance(100);assert(tab5_native_port_post_touch(1279,719,true,ms));pump();
    advance(30);assert(tab5_native_port_post_touch(1279,719,false,ms));pump();assert(!e.ring);
    unsigned registry=e.buttons_n;
    notice("First text");notice("Replacement text");touch(nui_obj_get_child(u.notice,1));
    assert(!u.notice&&!e.ring&&e.buttons_n==registry);
    snapshot.action_result_known=true;snapshot.action_result_success=true;
    snapshot.action_result_kind=TAB5_UI_TIME;snapshot.action_result_serial++;
    tab5_ui_update(&snapshot);close_selected();push();assert(!u.notice&&!e.ring);
    clear_status();snapshot.action_result_known=false;
    for(unsigned i=0;i<80;i++){
        notice(i%2?"Odd":"Even");push();assert(!u.notice&&e.buttons_n==registry);
    }
    notice("Backend clears notice");clear_status();assert(!e.ring);
    finish();
}

static void opening_and_wake(unsigned theme,tab5_mode_t mode){
    start(theme,mode);
    snapshot.relative.ready=true;tab5_ui_update(&snapshot); /* LEVEL START prerequisite. */
    rotate(1);push();assert(e.level==ENC_SELECT&&e.index==0);
    reply_on_action=true;push();
    assert(actions==1&&u.notice);close_selected(); /* Opening PRESS must not close. */
    push();assert(!u.notice&&actions==1&&e.level==ENC_SELECT&&e.index==0);
    reply_on_action=false;

    /* Arrival after input idle focuses Close for the very next push. */
    advance(16000);pump();assert(!e.ring);
    notice("After idle");push();assert(!u.notice&&!e.ring&&actions==1);
    notice("Popup idle");advance(16000);pump();assert(u.notice&&!e.ring);
    push();assert(u.notice&&e.ring&&actions==1);push();assert(!u.notice&&actions==1);

    assert(tab5_native_port_backlight_configure(300,60));pump();
    advance(300000);pump();assert(!e.ring);
    notice("While backlight sleeps");
    push();assert(u.notice&&e.ring&&actions==1); /* Raw DOWN/UP gesture only wakes. */
    push();assert(!u.notice&&actions==1);
    notice("Rotate wake");advance(300000);pump();assert(!e.ring);
    rotate(2);assert(u.notice&&e.ring&&u.page==MEASURE&&actions==1);
    push();assert(!u.notice&&actions==1);
    finish();
}

static void held_touch(unsigned theme){
    start(theme,TAB5_MODE_HEEL);
    rotate(1);rotate(SETTINGS);assert(u.page==SETTINGS);
    nui_obj_t *stepper=enc_button(201);nui_area_t a;nui_obj_get_coords(stepper,&a);
    int x=(a.x1+a.x2)/2,y=(a.y1+a.y2)/2;
    advance(100);assert(tab5_native_port_post_touch(x,y,true,ms));pump();
    assert(u.repeat);double draft=u.draft[0];
    notice("During held stepper");advance(1000);pump();assert(u.draft[0]>draft);
    draft=u.draft[0];
    advance(10);assert(tab5_native_port_post_touch(x,y,false,ms));pump();
    assert(u.notice&&u.draft[0]==draft&&actions==0);
    push();assert(u.notice&&e.ring&&actions==0);push();assert(!u.notice&&actions==0);
    finish();
}

static void device_settings(unsigned theme,tab5_mode_t mode){
    start(theme,mode);
    capture("primary",theme,mode);
    assert(dual.entry&&enc_descendant(dual.entry,u.pages[SETTINGS]));
    assert(!enc_descendant(dual.entry,u.status_bar));
    unsigned entries=0;
    for(unsigned i=0;i<e.buttons_n;i++)if(e.buttons[i].id==A_DEVICE)entries++;
    assert(entries==1&&enc_count()==(mode==TAB5_MODE_LEVEL?4u:3u));
    assert(enc_object(0)==(mode==TAB5_MODE_LEVEL?dual.start:u.start[0]));
    if(mode==TAB5_MODE_LEVEL)assert(enc_object(3)==dual.unit);
    rotate(1);rotate(SETTINGS);assert(u.page==SETTINGS);
    nui_area_t entry,view,page;
    nui_obj_get_coords(dual.entry,&entry);nui_obj_get_coords(u.settings_view,&view);
    nui_obj_get_coords(u.pages[SETTINGS],&page);
    assert(nui_obj_is_visible(dual.entry)&&entry.y1>=page.y1&&entry.y2<view.y1);
    assert(entry.x1>=page.x1&&entry.x2<=page.x2&&nui_obj_get_width(u.rail_time)==300);
    assert(enc_count()==15&&enc_object(0)==dual.entry&&enc_object(1)==u.setting_rows[0]);
    push();assert(e.index==0&&e.level==ENC_SELECT);
    capture("settings_device",theme,mode);push();
    assert(u.modal_kind==6&&dual.roles&&dual.axes&&dual.boot&&dual.mode[0]&&dual.mode[1]);
    assert(e.modal_index==0&&e.saved_index==0&&e.saved_level==ENC_SELECT);
    assert(dual.draft.reference_index==snapshot.relative_config.reference_index);
    assert(dual.draft.object_index==snapshot.relative_config.object_index);
    unsigned boot=0;
    for(unsigned i=0;i<e.buttons_n;i++){
        if(!enc_descendant(e.buttons[i].obj,u.dialog))continue;
        if(e.buttons[i].id==A_REL_BOOT)break;
        boot++;
    }
    rotate((int)boot);push();assert(dual.draft.startup_mode!=snapshot.relative_config.startup_mode);
    notice("Device dialog message");rotate(-100);push();
    assert(!u.notice&&u.modal_kind==6&&e.modal_index==boot&&actions==0);
    input(TAB5_UI_INPUT_LONG,0);
    assert(!u.modal_kind&&e.level==ENC_SELECT&&e.index==0&&u.page==SETTINGS&&actions==0);
    assert(snapshot.relative_config.startup_mode==0); /* Cancel preserves backend config. */
    frame_equal();
    touch(dual.entry);assert(u.modal_kind==6);input(TAB5_UI_INPUT_ROT,1);input(TAB5_UI_INPUT_LONG,0);
    assert(!u.modal_kind&&actions==0);
    finish();
}

static void touch_stop(unsigned theme,tab5_mode_t mode){
    start(theme,mode);snapshot.recording=true;tab5_ui_update(&snapshot);
    notice("Запись продолжается");
    nui_obj_t *stop=mode==TAB5_MODE_LEVEL?dual.start:u.start[0];
    nui_area_t banner,stop_area;nui_obj_get_coords(u.notice,&banner);nui_obj_get_coords(stop,&stop_area);
    assert(banner.y2<stop_area.y1);capture("recording_popup_stop",theme,mode);
    assert(!nui_obj_has_state(stop,NUI_STATE_DISABLED));
    touch(stop);assert(actions==1&&last_kind==TAB5_UI_STOP&&u.notice);
    rotate(1);close_selected();push();assert(!u.notice&&actions==1);
    /* A new notice while STOP is already held must not swallow its release. */
    nui_area_t a;nui_obj_get_coords(stop,&a);int x=(a.x1+a.x2)/2,y=(a.y1+a.y2)/2;
    advance(100);assert(tab5_native_port_post_touch(x,y,true,ms));pump();
    notice("Message during STOP touch");
    advance(50);assert(tab5_native_port_post_touch(x,y,false,ms));pump();
    assert(actions==2&&last_kind==TAB5_UI_STOP&&u.notice);
    /* Ordinary touch navigation still works while the notice is present. */
    touch(u.nav[SETTINGS]);assert(u.page==SETTINGS&&u.notice&&actions==2);
    rotate(1);push();assert(!u.notice&&u.page==SETTINGS&&e.level==ENC_MENU&&actions==2);
    finish();
}

int main(int argc,char **argv){
    capture_dir=argc>1?argv[1]:NULL;
    for(unsigned theme=0;theme<2;theme++){
        for(unsigned mode=TAB5_MODE_HEEL;mode<=TAB5_MODE_LEVEL;mode++){
            contexts(theme,(tab5_mode_t)mode);opening_and_wake(theme,(tab5_mode_t)mode);
            device_settings(theme,(tab5_mode_t)mode);
            touch_stop(theme,(tab5_mode_t)mode);
        }
        held_touch(theme);
    }
    printf("POPUP_FOCUS PASS: HEEL/LEVEL x dark/light; real FIFO/raw button/touch; "
           "auto-Close, no underlying actions, nav/edit/modal restore, opening-event, "
           "idle/backlight wake, touch STOP/navigation/held-gesture semantics, lifetime, Settings-only device entry/roles/startup; "
           "%u RGB565 partial/full checks\n",checks);
    return 0;
}
