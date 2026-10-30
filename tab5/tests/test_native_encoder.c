/* Host-only production renderer/state machine: no device, OS window or driver. */
#include "../main/tab5_ui.c"
#include "tab5_native_port.h"
#include <assert.h>
static uint16_t pixels[1280*720];
static unsigned actions,frame_checks;
static const char *capture_dir;
static void capture_ntp(const char *state,unsigned theme){
    if(!capture_dir)return;
    nui_native_render();char path[1024];
    snprintf(path,sizeof(path),"%s/%s_ntp_%s.rgb565",capture_dir,theme?"light":"dark",state);
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(pixels,sizeof(pixels),1,f)==1);assert(!fclose(f));
}
static uint16_t partial_frame[1280*720];
static void assert_frame_eq(const char *state,unsigned theme){
    nui_native_render();memcpy(partial_frame,pixels,sizeof(pixels));
    nui_obj_invalidate(u.root);nui_native_render();
    if(memcmp(partial_frame,pixels,sizeof(pixels))){
        for(unsigned i=0;i<1280*720;i++)if(partial_frame[i]!=pixels[i]){
            fprintf(stderr,"DIRTY_FRAME FAIL theme=%u state=%s x=%u y=%u partial=%04x full=%04x\n",theme,state,i%1280,i/1280,partial_frame[i],pixels[i]);break;
        }
        assert(!"partial RGB565 frame differs from forced full render");
    }
    frame_checks++;
}
static tab5_ui_action_t last_action;
static uint32_t ms=100;
static void action(const tab5_ui_action_t *a,void *unused){(void)unused;actions++;last_action=*a;}
static void input(tab5_ui_input_event_t ev,int delta,unsigned elapsed){ms+=elapsed;tab5_ui_input(ev,delta,ms);}
static void rot(int delta){input(TAB5_UI_INPUT_ROT,delta,100);}
static void press(void){input(TAB5_UI_INPUT_PRESS,0,100);}
static void back(void){input(TAB5_UI_INPUT_LONG,0,100);}
static void menu(unsigned page){e.level=ENC_MENU;e.ring=true;ask_navigation(page);assert(u.page==page);}
static void touch(nui_obj_t *obj){nui_area_t a;nui_obj_get_coords(obj,&a);tab5_ui_touch((a.x1+a.x2)/2,(a.y1+a.y2)/2,true,ms);tab5_ui_touch((a.x1+a.x2)/2,(a.y1+a.y2)/2,false,ms+20);}
int main(int argc,char **argv){
 capture_dir=argc>1?argv[1]:NULL;
 for(unsigned theme=0;theme<2;theme++){
    assert(nui_native_init(1280,720,pixels));encoder_reset();tab5_ui_init(action,NULL);
    tab5_ui_state_t s={.light_theme=theme,.brightness_pct=60,.datetime_known=true,.datetime={2026,10,26,12,30,0}};
    tab5_measure_config_default(&s.measurement.config);s.measurement.config.frequency_hz=20;s.measurement.config.gap_ms=20;s.measurement.config.alpha=.2f;tab5_ui_update(&s);encoder_sync();assert(!u.dirty&&u.draft[0]==20&&u.draft[1]==20&&fabs(u.draft[2]-.2)<.0001);
    /* ZERO warning follows actual recording, independent of link/update fields. */
    for(unsigned phase=0;phase<5;phase++){
        s.recording=phase==1||phase==2;s.link.known=phase!=4;
        s.record_sync_known=phase!=4;s.record_sync=phase==2;
        tab5_ui_update(&s);confirm(A_ZERO);
        const char *message=nui_label_get_text(nui_obj_get_child(u.dialog,0));
        assert(message&&((strstr(message,"посреди записи")!=NULL)==s.recording));
        assert(u.modal_kind==4);close_modal();
    }
    s.recording=false;s.record_sync_known=false;s.record_sync=false;tab5_ui_update(&s);
    assert(e.scroll_thumb[0]&&e.scroll_thumb[1]);
    for(unsigned i=0;i<2;i++)assert(!nui_obj_has_flag(e.scroll_thumb[i],NUI_OBJ_FLAG_CLICKABLE));
    assert(!e.ring);s.brightness_pct=70;tab5_ui_update(&s);assert(!u.dirty&&u.draft[7]==70);s.brightness_pct=60;tab5_ui_update(&s);assert(!u.dirty&&u.draft[7]==60);unsigned before=actions;assert_frame_eq("main",theme);rot(5);assert(e.ring&&u.page==MEASURE&&actions==before);assert_frame_eq("menu-focus",theme);
    rot(1);assert(u.page==ROLL);assert_frame_eq("page-change",theme);press();assert(e.level==ENC_MENU);rot(99);assert(u.page==NETWORK);rot(99);assert(u.page==NETWORK);rot(-99);assert(u.page==MEASURE);
    press();assert(e.level==ENC_SELECT&&e.index==0);rot(2);assert(e.index==2);rot(99);assert(e.index==2);press();assert(u.modal_kind==4&&e.modal_index==0);assert_frame_eq("modal-open",theme);
    press();assert(!u.modal_kind&&actions==before);assert_frame_eq("modal-close",theme); /* default cancel */
    rot(-1);press();assert(u.modal_kind==4&&e.modal_index==0);press();assert(!u.modal_kind&&actions==before); /* ZERO also defaults to cancel */
    press();assert(u.modal_kind==4&&e.modal_index==0);rot(99);press();assert(last_action.kind==TAB5_UI_ZERO&&actions==before+1);
    input(TAB5_UI_INPUT_IDLE,0,14999);assert(e.ring);input(TAB5_UI_INPUT_IDLE,0,1);assert(!e.ring);before=actions;press();assert(e.ring&&actions==before&&!u.modal_kind);
    /* A static visible focus must not repaint on 10ms idle polls or sync. */
    nui_native_render();for(unsigned idle=0;idle<100;idle++){tab5_ui_input(TAB5_UI_INPUT_IDLE,0,ms+idle*10);encoder_sync();assert(nui_native_render()==0);}
    /* Actual touch hides ring and transfers the focus used on wake. */
    touch(enc_button(A_RESET));assert(!e.ring&&u.modal_kind==4);press();assert(u.modal_kind==4&&e.ring);press();assert(!u.modal_kind);assert(e.index==2);
    menu(SETTINGS);press();double old=u.draft[0];press();assert(e.level==ENC_EDIT);assert_frame_eq("edit",theme);nui_native_render();for(unsigned i=0;i<10;i++){encoder_sync();tab5_ui_input(TAB5_UI_INPUT_IDLE,0,ms+i*10);assert(!nui_native_render());}rot(1);assert(u.draft[0]==old+1);back();assert(u.draft[0]==old&&e.level==ENC_SELECT);
    /* Exactly the sixth fast detent accelerates; >=90ms and direction reset it. */
    press();old=u.draft[0];for(unsigned i=0;i<5;i++)input(TAB5_UI_INPUT_ROT,1,80);assert(u.draft[0]==fmin(hi[0],old+5));input(TAB5_UI_INPUT_ROT,1,80);assert(u.draft[0]==fmin(hi[0],old+10));
    input(TAB5_UI_INPUT_ROT,-1,80);assert(e.streak==1);input(TAB5_UI_INPUT_ROT,-1,90);assert(e.streak==1);rot(1000);assert(u.draft[0]==hi[0]);rot(-1000);assert(u.draft[0]==lo[0]);back();assert(u.draft[0]==old);
    rot(7);assert(e.index==7);nui_area_t row,view;nui_obj_get_coords(u.setting_rows[7],&row);nui_obj_get_coords(u.settings_view,&view);assert(row.y1>=view.y1&&row.y2<=view.y2);
    assert_frame_eq("settings-scroll",theme);nui_area_t thumb;nui_obj_get_coords(e.scroll_thumb[0],&thumb);assert(thumb.y1>view.y1+8&&thumb.y2<view.y2-7);
    press();rot(-1);assert(u.draft[7]==50);press();rot(6);assert(e.index==13);press();assert(last_action.kind==TAB5_UI_SETTINGS&&last_action.brightness_pct==50&&u.pending);
    s.brightness_pct=50;s.measurement.config=last_action.config;s.action_result_known=true;s.action_result_kind=TAB5_UI_SETTINGS;s.action_result_success=true;s.action_result_serial++;tab5_ui_update(&s);encoder_sync();tab5_ui_update(&s);assert(!u.dirty&&!u.pending);
    menu(GRAPH);press();assert(enc_count()==8);rot(3);press();assert(e.cursor&&e.level==ENC_EDIT);assert_frame_eq("graph-cursor",theme);rot(-99);assert(e.cursor_s==0);rot(99);assert(e.cursor_s==(u.range==0?30:u.range==1?120:600));back();assert(!e.cursor&&e.level==ENC_SELECT);
    menu(NETWORK);press();bool was_ap=u.net_draft.ap_enabled;press();assert(u.net_draft.ap_enabled!=was_ap);press();assert(u.net_draft.ap_enabled==was_ap);rot(5);assert(e.index==5&&enc_object(5)==u.net_fields[3]);nui_obj_get_coords(u.net_fields[3],&row);nui_obj_get_coords(u.network_view,&view);assert(row.y1>=view.y1&&row.y2<=view.y2);assert_frame_eq("network-scroll",theme);
    press();assert(u.modal_kind==2);unsigned keycount=nui_native_keyboard_get_keys(u.keyboard,NULL,NULL,0);rot(-999);assert(e.modal_index==0);rot(1);const char *key=nui_buttonmatrix_get_button_text(u.keyboard,1);char expected[32];snprintf(expected,sizeof(expected),"%s",key);press();assert(!strcmp(nui_textarea_get_text(u.text_field),expected));rot(999);assert(e.modal_index==enc_modal_count()-1);back();assert(!u.modal_kind&&!u.net_draft.sta_password[0]);
    menu(SETTINGS);press();rot(9);press();assert(u.modal_kind==5);rot(1);assert(atoi(nui_textarea_get_text(u.date_fields[0]))==2027);press();assert(e.modal_index==1);for(unsigned i=1;i<7;i++)press();assert(e.modal_index==7);press();assert(!u.modal_kind); /* cancel */
    press();assert(u.modal_kind==5);for(unsigned i=0;i<7;i++)press();rot(2);before=actions;press();assert(!u.modal_kind&&actions==before+1&&last_action.kind==TAB5_UI_TIME&&last_action.datetime.year==2026);
    /* Time window: UTC is explicit; unknown uses +03:00, never implicit UTC. */
    s.timezone_known=false;tab5_ui_update(&s);open_time();assert(e.timezone_draft==180);assert(!strcmp(nui_label_get_text(nui_obj_get_child(e.timezone_value,0)),"+03:00"));
    before=actions;e.ring=true;e.modal_index=6;rot(1);assert(e.timezone_draft==195);press();assert(e.modal_index==7);press();assert(!u.modal_kind&&actions==before&&!s.timezone_known);
    s.timezone_known=true;s.timezone_minutes=0;tab5_ui_update(&s);open_time();assert(e.timezone_draft==0);assert(!strcmp(nui_label_get_text(nui_obj_get_child(e.timezone_value,0)),"+00:00"));
    touch(enc_button(A_TZ_PLUS));assert(e.timezone_draft==15&&e.modal_index==6);touch(enc_button(A_TZ_MINUS));assert(e.timezone_draft==0);
    e.ring=true;e.modal_index=6;rot(-999);assert(e.timezone_draft==-720);assert(!strcmp(nui_label_get_text(nui_obj_get_child(e.timezone_value,0)),"-12:00"));rot(999);assert(e.timezone_draft==840);assert(!strcmp(nui_label_get_text(nui_obj_get_child(e.timezone_value,0)),"+14:00"));
    /* Calendar validation uses the production Gregorian validator and Done gate. */
    const unsigned calendar[][7]={{2024,2,29,23,59,59,1},{2026,2,29,12,0,0,0},{2000,2,29,0,0,0,1},{2100,2,28,0,0,0,0},{2026,4,31,0,0,0,0},{2026,10,26,24,0,0,0},{2026,10,26,23,60,0,0},{2026,10,26,23,59,60,0}};
    for(unsigned row=0;row<sizeof(calendar)/sizeof(calendar[0]);row++){
        for(unsigned col=0;col<6;col++){char b[12];snprintf(b,sizeof(b),"%u",calendar[row][col]);nui_textarea_set_text(u.date_fields[col],b);}
        date_edited(NULL);tab5_ui_datetime_t parsed;assert(date_valid(&parsed)==(bool)calendar[row][6]);assert(nui_obj_has_state(time_done,NUI_STATE_DISABLED)==!calendar[row][6]);
    }
    before=actions;e.modal_index=9;press();assert(u.modal_kind==5&&actions==before); /* invalid Done emits nothing */
    assert_frame_eq("time-window",theme);
    /* NTP does not require a valid manually entered date, only explicit request. */
    touch(enc_button(A_TIME_SYNC));assert(actions==before+1&&last_action.kind==TAB5_UI_TIME_SYNC&&last_action.timezone_set&&last_action.timezone_minutes==840);
    assert(u.modal_kind==5&&e.sync_requested&&nui_obj_has_state(e.sync_button,NUI_STATE_DISABLED));
    assert(strstr(nui_label_get_text(e.sync_status),"Запрос"));
    e.ring=true;e.modal_index=8;press();assert(actions==before+1); /* duplicate suppressed before busy snapshot */
    s.time_sync_busy=true;snprintf(s.time_sync_status,sizeof(s.time_sync_status),"Синхронизация по WiFi...");tab5_ui_update(&s);assert(nui_obj_has_state(e.sync_button,NUI_STATE_DISABLED));assert(!strcmp(nui_label_get_text(e.sync_status),s.time_sync_status));
    press();assert(actions==before+1);
    s.time_sync_busy=false;snprintf(s.time_sync_status,sizeof(s.time_sync_status),"Нет подключения STA: одной точки AP недостаточно");s.action_result_kind=TAB5_UI_TIME_SYNC;s.action_result_serial++;s.action_result_success=false;tab5_ui_update(&s);
    assert(!strcmp(nui_textarea_get_text(u.date_fields[5]),"60"));assert(e.timezone_draft==840);assert(!e.sync_requested&&!nui_obj_has_state(e.sync_button,NUI_STATE_DISABLED));assert(!strcmp(nui_label_get_text(e.sync_status),s.time_sync_status));assert_frame_eq("time-sync-feedback",theme);
    /* Completion is backend text, and only backend changes the clock snapshot. */
    snprintf(s.time_sync_status,sizeof(s.time_sync_status),"Время синхронизировано");s.action_result_serial++;s.action_result_success=true;tab5_ui_update(&s);assert(!strcmp(nui_label_get_text(e.sync_status),s.time_sync_status));assert(!strcmp(nui_textarea_get_text(u.date_fields[5]),"60"));
    e.ring=true;e.modal_index=8;press();assert(actions==before+2&&last_action.kind==TAB5_UI_TIME_SYNC);
    s.datetime=(tab5_ui_datetime_t){2028,2,29,10,11,12};s.timezone_minutes=-60;s.timezone_known=true;s.action_result_serial++;tab5_ui_update(&s);
    assert(!strcmp(nui_textarea_get_text(u.date_fields[0]),"2028")&&!strcmp(nui_textarea_get_text(u.date_fields[5]),"12"));assert(e.timezone_draft==-60&&!nui_obj_has_state(time_done,NUI_STATE_DISABLED));
    back();assert(!u.modal_kind&&actions==before+2); /* cancel while request is pending does not emit a second action */
    s.time_sync_busy=false;s.action_result_serial++;snprintf(s.time_sync_status,sizeof(s.time_sync_status),"Синхронизация завершена");tab5_ui_update(&s);
    s.timezone_minutes=0;tab5_ui_update(&s);open_time();assert(e.timezone_draft==0);touch(enc_button(A_TZ_MINUS));assert(e.timezone_draft==-15);
    const unsigned valid_fields[]={2024,2,29,23,59,59};for(unsigned col=0;col<6;col++){char b[12];snprintf(b,sizeof(b),"%u",valid_fields[col]);nui_textarea_set_text(u.date_fields[col],b);}date_edited(NULL);
    before=actions;touch(time_done);assert(!u.modal_kind&&actions==before+1&&last_action.kind==TAB5_UI_TIME&&last_action.timezone_set&&last_action.timezone_minutes==-15&&last_action.datetime.year==2024&&last_action.datetime.day==29);
    /* An unknown RTC must become editable after our successful explicit NTP. */
    s.datetime_known=false;s.timezone_known=false;tab5_ui_update(&s);open_time();assert(!*nui_textarea_get_text(u.date_fields[0])&&nui_obj_has_state(time_done,NUI_STATE_DISABLED));
    before=actions;touch(enc_button(A_TIME_SYNC));assert(actions==before+1&&last_action.kind==TAB5_UI_TIME_SYNC);capture_ntp("pending",theme);
    s.datetime_known=true;s.datetime=(tab5_ui_datetime_t){2026,10,26,14,15,16};s.timezone_known=true;s.timezone_minutes=180;s.action_result_kind=TAB5_UI_TIME_SYNC;s.action_result_success=true;s.action_result_serial++;snprintf(s.time_sync_status,sizeof(s.time_sync_status),"Время синхронизировано");tab5_ui_update(&s);
    assert(!strcmp(nui_textarea_get_text(u.date_fields[0]),"2026")&&!strcmp(nui_textarea_get_text(u.date_fields[5]),"16")&&!nui_obj_has_state(time_done,NUI_STATE_DISABLED)&&e.timezone_draft==180);capture_ntp("success",theme);
    /* The new typed result is consumed once; later telemetry preserves new manual edits. */
    nui_textarea_set_text(u.date_fields[5],"17");date_edited(NULL);tab5_ui_update(&s);assert(!strcmp(nui_textarea_get_text(u.date_fields[5]),"17"));
    touch(enc_button(A_MODAL_CANCEL));assert(!u.modal_kind&&actions==before+1&&s.datetime.second==16);
    /* Queue-full rejection: no busy transition and identical old/new error text.
     * Only the new typed TIME_SYNC serial can release the local pending state. */
    snprintf(s.time_sync_status,sizeof(s.time_sync_status),"Очередь команд заполнена. Повторите.");s.action_result_kind=TAB5_UI_TIME_SYNC;s.action_result_success=false;s.action_result_serial++;s.time_sync_busy=false;tab5_ui_update(&s);open_time();
    before=actions;int16_t timezone_before=e.timezone_draft;touch(enc_button(A_TIME_SYNC));assert(actions==before+1&&e.sync_requested&&!e.sync_seen_busy);assert(nui_obj_has_state(e.sync_button,NUI_STATE_DISABLED));
    assert(!strcmp(e.sync_previous_status,s.time_sync_status));tab5_ui_update(&s);assert(e.sync_requested); /* stale rejection must not clear the new request */
    s.action_result_serial++;tab5_ui_update(&s);
    assert(!e.sync_requested&&!nui_obj_has_state(e.sync_button,NUI_STATE_DISABLED)&&!u.state.time_sync_busy);assert(!strcmp(nui_label_get_text(e.sync_status),s.time_sync_status));assert(e.timezone_draft==timezone_before);capture_ntp("error",theme);
    touch(enc_button(A_TIME_SYNC));assert(actions==before+2&&e.sync_requested); /* retry is possible */
    s.action_result_serial++;tab5_ui_update(&s);assert(!e.sync_requested);touch(enc_button(A_MODAL_CANCEL));assert(!u.modal_kind);
    /* Page focus order and endpoints are independent from draw-tree ordering. */
    menu(MEASURE);assert(enc_count()==3&&enc_object(0)==u.start[0]&&enc_object(1)==enc_button(A_ZERO)&&enc_object(2)==enc_button(A_RESET));
    menu(ROLL);assert(enc_count()==0);menu(RECORD);assert(enc_count()==1&&enc_object(0)==u.start[1]);
    menu(GRAPH);for(unsigned i=0;i<8;i++)assert(enc_object(i)==(i<3?u.ranges[i]:i==3?u.chart_touch:u.legend[i-4]));
    menu(SETTINGS);for(unsigned i=0;i<13;i++)assert(enc_object(i)==u.setting_rows[i]);assert(enc_object(13)==u.set_apply);
    /* Swipe starts on an actionable value: scrolling must never open its keypad. */
    menu(SETTINGS);e.settings_offset=0;nui_obj_set_y(u.settings_content,0);nui_area_t value;nui_obj_get_coords(u.step_value[0],&value);int tx=(value.x1+value.x2)/2,ty=(value.y1+value.y2)/2;
    before=actions;tab5_ui_touch(tx,ty,true,ms);tab5_ui_touch(tx,ty-180,true,ms+20);tab5_ui_touch(tx,ty-180,false,ms+40);assert(!u.modal_kind&&actions==before&&e.settings_offset==180);encoder_sync();assert(e.settings_offset==180);assert_frame_eq("touch-scroll",theme);
    tab5_ui_touch(tx,ty,true,ms+50);tab5_ui_touch(tx,ty-10000,true,ms+70);tab5_ui_touch(tx,ty-10000,false,ms+90);assert(e.settings_offset==nui_obj_get_height(u.settings_content)-nui_obj_get_height(u.settings_view));
    nui_obj_get_coords(e.scroll_thumb[0],&thumb);nui_obj_get_coords(u.settings_view,&view);assert(thumb.y2==view.y2-8);
    tab5_ui_touch(tx,ty,true,ms+100);tab5_ui_touch(tx,ty+10000,true,ms+120);tab5_ui_touch(tx,ty+10000,false,ms+140);assert(e.settings_offset==0&&!u.modal_kind);
    nui_obj_get_coords(e.scroll_thumb[0],&thumb);assert(thumb.y1==view.y1+8);encoder_sync();nui_native_render();encoder_sync();assert(!nui_native_render());
    touch(u.step_value[0]);assert(u.modal_kind==1&&!e.ring);press();assert(u.modal_kind==1);rot(999);back();assert(!u.modal_kind);
    /* Repeated modal cycles must release the registry, not leave dead objects. */
    unsigned count=e.buttons_n;for(unsigned i=0;i<300;i++){e.injecting=true;enc_action(A_RESET);e.injecting=false;back();assert(!u.modal_kind);}assert(count==e.buttons_n);
    /* Settings flags and backend updates/reconnect must preserve known false/zero. */
    s.backlight_timeout_known=true;s.backlight_timeout_s=0;s.record_sync_known=true;s.record_sync=false;tab5_ui_update(&s);
    light_changed=sync_changed=false;u.dirty=false;u.pending=false;refresh_settings();
    menu(SETTINGS);e.level=ENC_SELECT;e.index=11;e.ring=true;encoder_paint();press();assert(light_changed&&light_draft==300);
    e.index=12;encoder_paint();press();assert(sync_changed&&sync_draft);apply_settings();
    assert(last_action.backlight_timeout_set&&last_action.backlight_timeout_s==300&&last_action.record_sync_set&&last_action.record_sync);
    s.backlight_timeout_s=300;s.record_sync=true;s.action_result_serial++;s.action_result_kind=TAB5_UI_SETTINGS;s.action_result_known=true;s.action_result_success=true;tab5_ui_update(&s);assert(!u.pending);
    capture_ntp("settings_start_stop",theme);
    confirm(A_ZERO);
    for(unsigned phase=0;phase<5;phase++){
        s.recording=phase==1||phase==2;s.link.known=phase!=4;s.record_sync_known=phase!=4;
        tab5_ui_update(&s);const char *message=nui_label_get_text(nui_obj_get_child(u.dialog,0));
        assert((strstr(message,"посреди записи")!=NULL)==s.recording);
        if(phase==0)capture_ntp("zero_idle",theme);
        if(phase==1)capture_ntp("zero_recording",theme);
    }
    close_modal();
    /* Actual port + UI: only one encoder event consumed after backlight sleep. */
    close_modal();u.pending=false;u.dirty=false;light_changed=sync_changed=false;
    show_page(MEASURE);e.level=ENC_MENU;e.ring=false;
    tab5_native_port_input_reset();assert(tab5_native_port_backlight_configure(300,60));
    tab5_native_port_input_dispatch(ms);ms+=300000;tab5_native_port_input_dispatch(ms);assert(!e.ring);
    before=actions;assert(tab5_native_port_post_input(TAB5_UI_INPUT_ROT,1,ms+1));tab5_native_port_input_dispatch(++ms);
    assert(e.ring&&u.page==MEASURE&&actions==before);
    assert(tab5_native_port_post_input(TAB5_UI_INPUT_ROT,1,ms+1));tab5_native_port_input_dispatch(++ms);assert(u.page==ROLL);
    ms+=300000;tab5_native_port_input_dispatch(ms);assert(!e.ring);
    assert(tab5_native_port_post_button(true,ms+1));tab5_native_port_input_dispatch(++ms);assert(e.ring);
    ms+=700;tab5_native_port_input_dispatch(ms);assert(tab5_native_port_post_button(false,ms));tab5_native_port_input_dispatch(ms);assert(actions==before&&u.page==ROLL);
    /* Batch wake consumes one detent, second detent navigates immediately. */
    ms+=300000;tab5_native_port_input_dispatch(ms);
    assert(tab5_native_port_post_input(TAB5_UI_INPUT_ROT,2,ms+1));tab5_native_port_input_dispatch(++ms);assert(u.page==GRAPH&&e.ring);
    nui_native_render();tab5_ui_destroy();nui_native_deinit();
 }
 assert(frame_checks==24);puts("DIRTY_FRAME PASS: 12 states per theme, partial RGB565 == forced full render");
 puts("ENCODER PASS: both themes, wake/idle, 6 pages, clamp, confirmations, edit/cancel/accel, brightness apply, settings/network scroll, graph cursor, keyboard, time/TZ/NTP/calendar/feedback/queue-rejection, modal lifetime");return 0;
}
