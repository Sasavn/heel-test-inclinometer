/* Headless consumer of real production UI + native RGB565 + port input FIFO.
 * No private UI structures, hardware, OS window, HTML renderer or LVGL. */
#include "tab5_native_port.h"
#include "native/native_ui.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static uint16_t frame[1280*720];
static uint32_t now_ms;
static tab5_ui_state_t state;
static tab5_ui_action_t last_action;
static unsigned actions,checks,shots;
static unsigned action_counts[32];
static FILE *events;
static const char *output;
static bool ready;
static void graph_fixture(void);
static uint64_t clock_us(void){return (uint64_t)now_ms*1000;}
static void action(const tab5_ui_action_t *a,void *user){
    (void)user;last_action=*a;actions++;
    if((unsigned)a->kind<32)action_counts[a->kind]++;
    fprintf(events,"{\"action\":%u,\"time_ms\":%"PRIu32",\"frequency_hz\":%u,\"light\":%s}\n",
        (unsigned)a->kind,now_ms,a->config.frequency_hz,a->light_theme?"true":"false");
}
static void check(bool ok,const char *name){
    fprintf(events,"{\"check\":\"%s\",\"passed\":%s}\n",name,ok?"true":"false");
    fflush(events);if(!ok){fprintf(stderr,"FAIL: %s\n",name);exit(2);}checks++;
}
static void pump(uint32_t elapsed){
    now_ms+=elapsed;nui_native_tick(elapsed);
    tab5_native_port_input_dispatch(now_ms);
    nui_obj_update_layout(nui_screen_active());nui_native_render();
}
static void input(tab5_ui_input_event_t kind,int32_t delta,uint32_t dt){
    now_ms+=dt;nui_native_tick(dt);
    check(tab5_native_port_post_input(kind,delta,now_ms),"input accepted");
    fprintf(events,"{\"event\":%u,\"delta\":%"PRId32",\"time_ms\":%"PRIu32"}\n",(unsigned)kind,delta,now_ms);
    tab5_native_port_input_dispatch(now_ms);nui_obj_update_layout(nui_screen_active());nui_native_render();
}
static void press(void){input(TAB5_UI_INPUT_PRESS,0,120);}
static void rot(int n){if(n)input(TAB5_UI_INPUT_ROT,n,120);}
static void back(void){input(TAB5_UI_INPUT_LONG,0,600);}
static void tap(int x,int y){
    now_ms+=20;nui_native_tick(20);
    check(tab5_native_port_post_touch(x,y,true,now_ms),"touch down accepted");
    tab5_native_port_input_dispatch(now_ms);pump(40);
    check(tab5_native_port_post_touch(x,y,false,now_ms),"touch up accepted");
    tab5_native_port_input_dispatch(now_ms);pump(20);
}
static nui_obj_t *find(nui_obj_t *o,const char *s){
    if(nui_obj_has_flag(o,NUI_OBJ_FLAG_HIDDEN))return NULL;
    if(nui_obj_check_type(o,&nui_label_class)&&!strcmp(nui_label_get_text(o),s))return o;
    for(int i=(int)nui_obj_get_child_count(o)-1;i>=0;i--){nui_obj_t *r=find(nui_obj_get_child(o,i),s);if(r)return r;}
    return NULL;
}
static void click(const char *s){
    nui_obj_t *o=find(nui_screen_active(),s);
    if(!o){fprintf(stderr,"Missing visible label: %s\n",s);exit(2);}
    nui_area_t a;nui_obj_get_coords(o,&a);tap((a.x1+a.x2)/2,(a.y1+a.y2)/2);
}
static nui_obj_t *ring(nui_obj_t *o){
    if(nui_obj_has_flag(o,NUI_OBJ_FLAG_HIDDEN))return NULL;
    if(nui_obj_get_style_border_width(o,0)==5&&nui_color_eq(nui_obj_get_style_border_color(o,0),nui_color_hex(0xF87128)))return o;
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++){nui_obj_t *r=ring(nui_obj_get_child(o,i));if(r)return r;}
    return NULL;
}
static void mode(const char *name){check(find(nui_screen_active(),name)!=NULL,"expected visible encoder mode");}
static void drag(int x1,int y1,int x2,int y2){
    check(tab5_native_port_post_touch(x1,y1,true,now_ms),"drag down");pump(20);
    for(int i=1;i<=10;i++){check(tab5_native_port_post_touch(x1+(x2-x1)*i/10,y1+(y2-y1)*i/10,true,now_ms),"drag move");pump(20);}
    check(tab5_native_port_post_touch(x2,y2,false,now_ms),"drag release");pump(20);
}
static void focus_in_view(void){
    nui_obj_t *r=ring(nui_screen_active());check(r!=NULL,"visible orange focus ring");
    nui_area_t a;nui_obj_get_coords(r,&a);check(a.x1>=164&&a.x2<1280&&a.y1>=160&&a.y2<=704,"focus remains inside clipped list viewport");
}
static void tree(FILE *f,nui_obj_t *o,unsigned depth){
    if(nui_obj_has_flag(o,NUI_OBJ_FLAG_HIDDEN))return;
    nui_area_t a;nui_obj_get_coords(o,&a);
    if(nui_obj_check_type(o,&nui_label_class))fprintf(f,"%u\t%d\t%d\t%d\t%d\t%s\n",depth,a.x1,a.y1,a.x2,a.y2,nui_label_get_text(o));
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++)tree(f,nui_obj_get_child(o,i),depth+1);
}
static void capture(const char *name){
    /* Framebuffer is exactly the buffer passed to production native_init. */
    bool safe=*name!=0;for(const char *p=name;*p;p++)safe=safe&&((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-');check(safe,"safe capture name");
    pump(0);char path[2048];snprintf(path,sizeof(path),"%s/%s.ppm",output,name);
    FILE *f=fopen(path,"wb");check(f!=NULL,"open capture");fprintf(f,"P6\n1280 720\n255\n");
    unsigned char row[1280*3];
    for(int y=0;y<720;y++){
        for(int x=0;x<1280;x++){
            unsigned v=frame[y*1280+x];row[x*3]=(unsigned char)(((v>>11)&31)*255/31);
            row[x*3+1]=(unsigned char)(((v>>5)&63)*255/63);row[x*3+2]=(unsigned char)((v&31)*255/31);
        }
        if(fwrite(row,1,sizeof(row),f)!=sizeof(row)){fprintf(stderr,"capture write failed\n");exit(2);}
    }
    fclose(f);snprintf(path,sizeof(path),"%s/%s.tsv",output,name);f=fopen(path,"w");check(f!=NULL,"open tree");tree(f,nui_screen_active(),0);fclose(f);shots++;
    fprintf(events,"{\"capture\":\"%s\",\"time_ms\":%"PRIu32",\"actions\":%u}\n",name,now_ms,actions);
    printf("CAPTURE %s\n",name);fflush(stdout);
}
static void reset(bool light){
    if(ready){tab5_ui_destroy();nui_native_deinit();}
    tab5_native_port_input_close();now_ms=1000;actions=0;memset(action_counts,0,sizeof(action_counts));
    memset(&last_action,0,sizeof(last_action));memset(&state,0,sizeof(state));
    check(nui_native_init(1280,720,frame),"production RGB565 init");nui_native_set_clock_cb(clock_us);
    tab5_ui_init(action,NULL);tab5_native_port_input_reset();ready=true;
    tab5_measure_config_default(&state.measurement.config);
    state.measurement.config.frequency_hz=20;state.measurement.config.gap_ms=20;state.measurement.config.alpha=.2f;
    state.light_theme=light;state.now_known=true;state.now_us=700000000;
    state.datetime_known=true;state.datetime=(tab5_ui_datetime_t){2026,10,23,14,32,5};
    state.clock=(tab5_ui_clock_t){.known=true,.time_valid=true,.battery_present=true,.battery_v=5.02f};
    strcpy(state.clock.datetime,"23.10.2026 · 14:32:05");strcpy(state.firmware,"host-fixture");
    strcpy(state.panel,"headless production RGB565 1280x720");
    state.sd=(tab5_ui_medium_t){.known=true,.mounted=true};state.usb=(tab5_ui_medium_t){.known=true};
    state.link=(tab5_ui_link_t){.known=true,.ap_started=true,.sta_connected=true,.tcp_port=5000};
    strcpy(state.link.ap_ip,"192.168.4.1");strcpy(state.link.sta_ip,"10.20.4.117");
    state.network=(tab5_ui_network_t){.ap_enabled=true,.sta_enabled=true};
    strcpy(state.network.ap_ssid,"KRENOMER-7F3A");strcpy(state.network.sta_ssid,"SAFU-Lab");
    strcpy(state.network.ap_password,"fixture123");strcpy(state.network.sta_password,"fixture123");
    state.resources_known=state.cpu_known=true;state.cpu_load_pct=18;
    state.ram_total_bytes=512*1024;state.ram_used_bytes=230*1024;state.psram_total_bytes=32*1024*1024;state.psram_used_bytes=7*1024*1024;
    for(unsigned j=0;j<2;j++)state.measurement.sensor[j]=(tab5_measure_sensor_t){.valid=true,.x=j?-.39f:-.42f,.y=j?1.41f:1.37f,.roll_x=.14f,.roll_y=.05f,.calm_x=true,.calm_y=true,.good=1,.timestamp_us=state.now_us,.roll_fill_s=20};
    state.measurement.stable=true; /* Fixture: calm axes and full roll window. */
    tab5_ui_update(&state);pump(20);
}
static void select_page(unsigned page){press();rot((int)page);press();}
static void shot(const char *stage,bool light){char name[128];snprintf(name,sizeof(name),"%s_%s",stage,light?"light":"dark");capture(name);}
static void pages(void){
    const char *names[]={"angles","rolling","graph","recording","settings","network"};
    for(unsigned theme=0;theme<2;theme++)for(unsigned p=0;p<6;p++){
        reset(theme!=0);tap(76,(int)p*120+90);if(p==2)graph_fixture();
        check(!ring(nui_screen_active()),"reference page has no encoder focus");
        if(p==5)check(find(nui_screen_active(),"Точка доступа прибора")!=NULL,"genuine network page");
        shot(names[p],theme!=0);press();mode("меню");
        char menu_name[64];snprintf(menu_name,sizeof(menu_name),"menu_%s",names[p]);shot(menu_name,theme!=0);
    }
}
static void queue_tests(void){
    reset(true);tab5_native_input_stats_t q;
    check(!tab5_native_port_post_input(TAB5_UI_INPUT_ROT,0,now_ms),"reject zero detents");
    check(!tab5_native_port_post_touch(1280,0,true,now_ms),"reject outside touch");
    check(!tab5_native_port_post_input(TAB5_UI_INPUT_TOUCH,0,now_ms),"touch requires coordinates");
    check(!tab5_native_port_post_input((tab5_ui_input_event_t)99,0,now_ms),"reject unknown kind");
    for(unsigned i=0;i<TAB5_NATIVE_INPUT_CAPACITY;i++)check(tab5_native_port_post_input(TAB5_UI_INPUT_IDLE,0,now_ms),"FIFO capacity");
    check(!tab5_native_port_post_input(TAB5_UI_INPUT_PRESS,0,now_ms),"full FIFO explicit rejection");
    tab5_native_port_input_stats(&q);check(q.pending==64&&q.posted==64&&q.rejected==5,"FIFO accounting");
    check(tab5_native_port_input_dispatch(now_ms),"FIFO drained");
    tab5_native_port_input_stats(&q);check(q.pending==0&&q.consumed==64,"FIFO consumption");
    tab5_native_port_input_close();check(!tab5_native_port_post_input(TAB5_UI_INPUT_PRESS,0,now_ms),"closed FIFO rejects");
    tab5_native_port_input_reset();tab5_native_port_input_stats(&q);check(!q.pending&&!q.posted&&!q.rejected,"FIFO lifecycle reset");
    /* A whole burst must be consumed in FIFO order; producers run no UI code. */
    check(tab5_native_port_post_input(TAB5_UI_INPUT_PRESS,0,now_ms),"queue wake");
    check(tab5_native_port_post_input(TAB5_UI_INPUT_PRESS,0,now_ms+10),"queue select");
    check(tab5_native_port_post_input(TAB5_UI_INPUT_ROT,1,now_ms+20),"queue zero focus");
    check(tab5_native_port_post_input(TAB5_UI_INPUT_PRESS,0,now_ms+30),"queue confirm dialog");
    check(tab5_native_port_post_input(TAB5_UI_INPUT_PRESS,0,now_ms+40),"queue cancel dialog");
    check(tab5_native_port_post_input(TAB5_UI_INPUT_ROT,-1,now_ms+50),"queue start focus");
    check(tab5_native_port_post_input(TAB5_UI_INPUT_PRESS,0,now_ms+60),"queue start");
    check(!actions&&!ring(nui_screen_active()),"producer never calls UI");
    now_ms+=60;tab5_native_port_input_dispatch(now_ms);
    check(actions==1&&last_action.kind==TAB5_UI_START&&!action_counts[TAB5_UI_ZERO],"FIFO ordering and safe cancel burst");
    reset(true);nui_obj_t *start=find(nui_screen_active(),"● Старт записи");check(start!=NULL,"touch button found");
    nui_area_t a;nui_obj_get_coords(start,&a);int x=(a.x1+a.x2)/2,y=(a.y1+a.y2)/2;
    check(tab5_native_port_post_touch(x,y,true,now_ms),"mixed queue touch down");
    check(tab5_native_port_post_touch(x,y,false,now_ms+30),"mixed queue touch release");
    check(tab5_native_port_post_input(TAB5_UI_INPUT_PRESS,0,now_ms+40),"mixed queue encoder wake");
    now_ms+=40;tab5_native_port_input_dispatch(now_ms);
    check(actions==1&&last_action.kind==TAB5_UI_START,"touch and encoder share FIFO without duplicate action");
    mode("выбор");
    /* A full FIFO must explicitly reject release; retry after draining must
     * release the original capture exactly once, alongside signed detents. */
    reset(true);start=find(nui_screen_active(),"● Старт записи");check(start!=NULL,"release retry button found");
    nui_obj_get_coords(start,&a);x=(a.x1+a.x2)/2;y=(a.y1+a.y2)/2;
    check(tab5_native_port_post_touch(x,y,true,now_ms),"release retry initial down");tab5_native_port_input_dispatch(now_ms);
    for(unsigned i=0;i<TAB5_NATIVE_INPUT_CAPACITY;i++)check(tab5_native_port_post_input(TAB5_UI_INPUT_IDLE,0,now_ms),"release retry fill FIFO");
    check(!tab5_native_port_post_touch(x,y,false,now_ms),"full FIFO returns false for touch release");
    tab5_native_port_input_dispatch(now_ms);check(!actions,"rejected release does not synthesize click");
    check(tab5_native_port_post_touch(x,y,false,now_ms),"retry touch release after draining");
    check(tab5_native_port_post_input(TAB5_UI_INPUT_ROT,1,now_ms+1),"mixed release positive detent");
    check(tab5_native_port_post_input(TAB5_UI_INPUT_ROT,-1,now_ms+2),"mixed release negative detent");
    check(tab5_native_port_post_input(TAB5_UI_INPUT_LONG,0,now_ms+3),"mixed release LONG");
    now_ms+=3;tab5_native_port_input_dispatch(now_ms);mode("меню");
    tab5_native_port_input_stats(&q);
    check(actions==1&&last_action.kind==TAB5_UI_START&&q.rejected==1&&!q.pending&&q.consumed==69,"release retry mixed FIFO delivered exactly once");
    /* Event time and idle use uint32 subtraction across monotonic wrap. */
    reset(true);now_ms=UINT32_MAX-500;press();press();pump(14999);
    check(ring(nui_screen_active())!=NULL,"idle before threshold across timestamp wrap");
    pump(1);check(!ring(nui_screen_active()),"idle at threshold across timestamp wrap");
}
static void graph_fixture(void){
    for(unsigned i=0;i<250;i++){
        state.now_us+=500000;
        for(unsigned j=0;j<2;j++){
            tab5_measure_sensor_t *s=&state.measurement.sensor[j];float t=i*.5f;
            s->timestamp_us=state.now_us;s->good++;s->x=.2f+1.95f/(1+expf(-(t-62)/3))+.38f*sinf(t*.9f+j*.1f)*fminf(1,fmaxf(0,(t-55)/10));
            s->y=-.12f+.1f*sinf(t*.9f+j)*fminf(1,fmaxf(0,(t-55)/10));s->filtered_x=s->decoded_x=s->x;s->filtered_y=s->decoded_y=s->y;
        }
        tab5_ui_update(&state);
    }
    pump(20);
}
static void time_fixtures(bool light){
    /* Backend status is injected fixture data: no NTP client is linked/run. */
    reset(light);state.timezone_known=true;state.timezone_minutes=180;tab5_ui_update(&state);
    select_page(4);rot(9);press();mode("окно");
    for(unsigned i=0;i<6;i++)press();
    rot(1);check(find(nui_screen_active(),"+03:15")!=NULL,"manual timezone advances by 15 minutes");
    shot("time_manual",light);press();rot(2);press();
    check(actions==1&&last_action.kind==TAB5_UI_TIME&&last_action.timezone_set&&last_action.timezone_minutes==195&&last_action.datetime.year==2026,"manual time and timezone callback");
    reset(light);state.timezone_known=true;state.timezone_minutes=180;tab5_ui_update(&state);
    select_page(4);rot(9);press();
    for(unsigned i=0;i<7;i++)press();
    rot(1);press();
    check(actions==1&&last_action.kind==TAB5_UI_TIME_SYNC&&last_action.timezone_set&&last_action.timezone_minutes==180,"WiFi time sync emits request callback only");
    state.time_sync_busy=true;snprintf(state.time_sync_status,sizeof(state.time_sync_status),"Синхронизация по WiFi…");tab5_ui_update(&state);
    check(find(nui_screen_active(),"Ожидание…")!=NULL,"busy sync feedback visible");
    shot("time_sync_pending",light);press();check(actions==1,"busy sync does not emit duplicate request");
    state.time_sync_busy=false;state.action_result_known=true;state.action_result_kind=TAB5_UI_TIME_SYNC;
    state.action_result_serial=1;state.action_result_success=false;
    snprintf(state.time_sync_status,sizeof(state.time_sync_status),"Ошибка NTP: тайм-аут (fixture)");tab5_ui_update(&state);
    check(find(nui_screen_active(),state.time_sync_status)!=NULL&&find(nui_screen_active(),"По WiFi")!=NULL,"backend sync error displayed and retry restored");
    shot("time_sync_error",light);check(actions==1&&!state.recording,"sync feedback does not invent actions or recording");
}
static void suite(void){
    queue_tests();
    reset(true);press();press();nui_native_stats_t idle_before,idle_after;
    nui_native_profile_snapshot(&idle_before);
    for(unsigned i=0;i<100;i++)pump(10);
    nui_native_profile_snapshot(&idle_after);
    check(idle_after.frames==idle_before.frames,"100 unchanged queue/IDLE polls do not render static focus");
    pages();
    for(unsigned theme=0;theme<2;theme++){
        bool light=theme!=0;
        reset(light);press();mode("меню");
        for(unsigned p=0;p<6;p++){if(p)rot(1);mode("меню");}
        check(actions==0,"menu rotation emits no backend actions");
        /* Start, safe default cancel, deliberate confirm. */
        reset(light);press();check(!actions,"wake PRESS is inert");press();mode("выбор");shot("select",light);
        press();check(actions==1&&last_action.kind==TAB5_UI_START,"encoder START action");
        state.recording=true;tab5_ui_update(&state);press();check(last_action.kind==TAB5_UI_STOP,"encoder STOP action");
        state.recording=false;tab5_ui_update(&state);rot(1);unsigned before=actions;press();mode("окно");shot("confirm_cancel_default",light);
        press();check(actions==before,"confirmation defaults to cancel");
        press();rot(1);press();check(actions==before+1&&last_action.kind==TAB5_UI_ZERO,"deliberate ZERO confirmation");
        /* Edit cancel leaves backend untouched; commit then apply emits draft. */
        reset(light);select_page(4);check(find(nui_screen_active(),"20 Гц")!=NULL,"backend fixture frequency accepted");press();rot(3);mode("правка");shot("edit",light);back();check(!actions,"LONG cancels edit without backend action");
        press();rot(1);press();rot(7);focus_in_view();shot("scroll",light);rot(100);press();
        check(actions==1&&last_action.kind==TAB5_UI_SETTINGS&&last_action.config.frequency_hz==21,"edit commit then apply action");
        /* Fast-detent threshold and bounds, with the real UI handling time. */
        reset(light);select_page(4);press();for(unsigned i=0;i<8;i++)input(TAB5_UI_INPUT_ROT,1,40);check(find(nui_screen_active(),"40 Гц")!=NULL,"sixth fast detent starts fivefold acceleration");shot("accelerated_edit",light);
        rot(1000);press();rot(100);press();check(last_action.config.frequency_hz==50,"frequency upper clamp");
        reset(light);select_page(4);press();rot(-1000);press();rot(100);press();check(last_action.config.frequency_hz==1,"frequency lower clamp");
        /* Cursor and modal keyboard are entered through normal controls. */
        reset(light);graph_fixture();select_page(2);rot(3);press();rot(-12);shot("graph_cursor",light);back();check(!actions,"graph cursor has no backend action");
        reset(light);select_page(5);rot(5);focus_in_view();shot("network_scroll",light);rot(-4);press();mode("окно");shot("keyboard",light);rot(3);press();shot("keyboard_typed",light);back();check(!actions,"keyboard LONG cancels draft");
        /* Numeric keyboard and touch-only startup, no encoder dependency. */
        reset(light);click("Настройки");click("20 Гц");shot("numeric_keyboard_touch",light);back();
        reset(light);click("Углы");click("● Старт записи");check(actions==1&&last_action.kind==TAB5_UI_START,"touch-only START action");
        reset(light);click("Настройки");drag(700,600,700,220);shot("settings_touch_scroll",light);check(!actions&&!ring(nui_screen_active()),"touch swipe no backend action or ring");
        reset(light);click("Сеть");drag(700,580,700,220);shot("network_touch_scroll",light);check(!actions&&!ring(nui_screen_active()),"network touch swipe no backend action or ring");
        reset(light);select_page(0);pump(15001);check(!ring(nui_screen_active()),"idle hides ring");shot("idle",light);before=actions;press();check(actions==before,"wake after idle is inert");mode("выбор");shot("wake",light);
        /* Presentation fixtures only: no simulated hardware/backend success. */
        reset(light);state.now_us+=100000;
        for(unsigned j=0;j<2;j++){
            tab5_measure_sensor_t *s=&state.measurement.sensor[j];
            s->x=s->decoded_x=s->filtered_x=j?-180.0f:180.0f;
            s->y=s->decoded_y=s->filtered_y=j?180.0f:-180.0f;s->timestamp_us=state.now_us;s->good++;
        }
        tab5_ui_update(&state);shot("wide_angles_180",light);
        reset(light);state.recording=true;state.rec_known=true;state.rec_duration_known=true;state.rec_duration_s=192;
        state.rec_started_us=state.now_us-192000000;strcpy(state.rec_file,"2026-10-23_M001");state.sd.recording=true;state.sd.written=3812;
        tab5_ui_update(&state);tap(76,450);shot("recording_active",light);
        reset(light);state.now_us+=100000;state.measurement.sensor[1].valid=false;state.measurement.sensor[1].fail_streak=3;
        state.measurement.sensor[1].last_error=TAB5_MEASURE_TIMEOUT;state.measurement.sensor[1].errors=27;
        tab5_ui_update(&state);shot("sensor_missing",light);
        time_fixtures(light);
    }
    printf("PASS %u checks; %u production RGB565 captures\n",checks,shots);
}
/* Line protocol, suitable for redirected scenario files or a persistent pipe.
 * RESET light|dark; ROT n [interval_ms]; PRESS; LONG; HOLD ms; WAIT ms;
 * TAP x y; CLICK visible text; CAPTURE safe_name; EXPECT kind total; QUIT.
 * HOLD models a press at t=0 and emits LONG at 600ms, without a release PRESS. */
static void protocol(void){
    char line[2048];bool enter_down=false,long_sent=false;uint32_t enter_at=0;reset(true);
    while(fgets(line,sizeof(line),stdin)){
        line[strcspn(line,"\r\n")]=0;if(!*line||line[0]=='#')continue;
        int n=0,x=0,y=0,x2=0,y2=0;unsigned ms=120;char word[256];
        if(sscanf(line,"RESET %255s",word)==1){enter_down=long_sent=false;reset(!strcmp(word,"light"));}
        else if(sscanf(line,"ROT %d %u",&n,&ms)>=1)input(TAB5_UI_INPUT_ROT,n,ms);
        else if(!strcmp(line,"PRESS")||!strcmp(line,"ENTER"))press();
        else if(!strcmp(line,"LONG")||!strcmp(line,"ESC"))back();
        else if(!strcmp(line,"UP")||!strcmp(line,"LEFT"))rot(-1);
        else if(!strcmp(line,"DOWN")||!strcmp(line,"RIGHT"))rot(1);
        else if(!strcmp(line,"HOLD600"))input(TAB5_UI_INPUT_LONG,0,600);
        else if(sscanf(line,"KEYDOWN %255s",word)==1){
            if(!strcmp(word,"ENTER")||!strcmp(word,"Enter")){
                if(!enter_down){enter_down=true;long_sent=false;enter_at=now_ms;}
            }else if(!strcmp(word,"UP")||!strcmp(word,"LEFT")||!strcmp(word,"ArrowUp")||!strcmp(word,"ArrowLeft"))rot(-1);
            else if(!strcmp(word,"DOWN")||!strcmp(word,"RIGHT")||!strcmp(word,"ArrowDown")||!strcmp(word,"ArrowRight"))rot(1);
            else if(!strcmp(word,"ESC")||!strcmp(word,"Escape"))back();
            else {fprintf(stderr,"Unknown key: %s\n",word);exit(2);}
        }
        else if(sscanf(line,"KEYUP %255s",word)==1){
            if((!strcmp(word,"ENTER")||!strcmp(word,"Enter"))&&enter_down){
                if(!long_sent)input(TAB5_UI_INPUT_PRESS,0,0);
                enter_down=false;long_sent=false;
            }
        }
        else if(sscanf(line,"HOLD %u",&ms)==1){if(ms>=600){input(TAB5_UI_INPUT_LONG,0,600);pump(ms-600);}else input(TAB5_UI_INPUT_PRESS,0,ms);}
        else if(sscanf(line,"WAIT %u",&ms)==1){
            uint32_t elapsed=now_ms-enter_at;
            if(enter_down&&!long_sent&&elapsed<600&&ms>=600-elapsed){
                uint32_t until_long=600-elapsed;pump(until_long);input(TAB5_UI_INPUT_LONG,0,0);long_sent=true;pump(ms-until_long);
            }else pump(ms);
        }
        else if(sscanf(line,"TAP %d %d",&x,&y)==2)tap(x,y);
        else if(sscanf(line,"DRAG %d %d %d %d",&x,&y,&x2,&y2)==4)drag(x,y,x2,y2);
        else if(!strncmp(line,"CLICK ",6))click(line+6);
        else if(sscanf(line,"CAPTURE %255s",word)==1)capture(word);
        else if(sscanf(line,"EXPECT %d %u",&n,&ms)==2)check(actions==ms&&(int)last_action.kind==n,"scenario expected callback");
        else if(!strcmp(line,"QUIT"))break;
        else {fprintf(stderr,"Unknown command: %s\n",line);exit(2);}
    }
}
int main(int argc,char **argv){
    if(argc!=3){fprintf(stderr,"usage: native_large_capture OUTPUT suite|pages|protocol\n");return 2;}
    output=argv[1];char path[2048];snprintf(path,sizeof(path),"%s/events.jsonl",output);events=fopen(path,"w");if(!events)return 2;
    setbuf(stdout,NULL);setbuf(events,NULL);
    if(!strcmp(argv[2],"suite"))suite();else if(!strcmp(argv[2],"pages"))pages();else if(!strcmp(argv[2],"protocol"))protocol();else return 2;
    if(ready){tab5_native_port_input_close();tab5_ui_destroy();nui_native_deinit();}
    nui_native_stats_t stats;nui_native_profile_snapshot(&stats);
    check(stats.allocation_count==0&&stats.allocation_bytes==0,"renderer allocations released");
    fclose(events);return 0;
}
