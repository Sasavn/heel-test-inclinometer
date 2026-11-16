/* Presentation captures of the production Tab5 UI. Only input state is a
 * host fixture. No replacement widgets, labels, screenshots or device IO. */
#include "../main/tab5_ui.c"
#include <assert.h>
static uint16_t fb[1280*720];static unsigned checks;static FILE *index_file,*audit_file;
static tab5_ui_action_t action_seen;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(2);}}while(0)
static void cb(const tab5_ui_action_t*a,void*ctx){(void)ctx;action_seen=*a;}
static void settle(void){for(unsigned i=0;i<3;i++){nui_native_tick(40);nui_obj_update_layout(nui_screen_active());nui_native_render();}}
static void audit(nui_obj_t*o,nui_area_t clip,const char*name){
    if(nui_obj_has_flag(o,NUI_OBJ_FLAG_HIDDEN))return;
    nui_area_t a;nui_obj_get_coords(o,&a);nui_area_t c={a.x1>clip.x1?a.x1:clip.x1,a.y1>clip.y1?a.y1:clip.y1,a.x2<clip.x2?a.x2:clip.x2,a.y2<clip.y2?a.y2:clip.y2};
    if(c.x1>c.x2||c.y1>c.y2)return;
    if(nui_obj_check_type(o,&nui_label_class)){
        const char*t=nui_label_get_text(o);const nui_font_t*f=nui_obj_get_style_text_font(o,0);nui_point_t z;
        nui_text_get_size(&z,t,f,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);
        fprintf(audit_file,"%s\t%d\t%d\t%d\t%d\t%d\t%d\t",name,a.x1,a.y1,a.x2,a.y2,z.x,z.y);
        for(const char*p=t;*p;p++){fputc(*p=='\n'?'|':*p,audit_file);}fputc('\n',audit_file);
    }
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++)audit(nui_obj_get_child(o,i),c,name);
}
static void shot(const char*out,const char*mode,const char*page,unsigned light,const char*kind){
    settle();char name[160],path[1024];snprintf(name,sizeof(name),"demo_%s_%s_%s",mode,page,light?"light":"dark");
    snprintf(path,sizeof(path),"%s/%s.rgb565",out,name);FILE*f=fopen(path,"wb");CHECK(f);CHECK(fwrite(fb,sizeof(fb),1,f)==1);fclose(f);
    fprintf(index_file,"%s\t%s\t%s\t%s\t%s\n",name,mode,page,light?"light":"dark",kind);
    audit(u.root,(nui_area_t){0,0,1279,719},name);
}
static void fixture(tab5_ui_state_t*s,unsigned light){
    memset(s,0,sizeof(*s));s->light_theme=light;s->dual_mode_known=true;s->now_known=true;s->datetime_known=true;
    s->datetime=(tab5_ui_datetime_t){2026,11,16,14,32,5};s->clock=(tab5_ui_clock_t){.known=true,.time_valid=true,.battery_present=false};
    strcpy(s->clock.datetime,"16.11.2026 · 14:32:05");strcpy(s->firmware,"1.8.7-esp32");strcpy(s->panel,"ST7123 · host fixture");
    s->resources_known=s->cpu_known=true;s->cpu_load_pct=18;s->ram_total_bytes=1024*1024;s->ram_used_bytes=450*1024;s->psram_total_bytes=16*1024*1024;s->psram_used_bytes=3*1024*1024;
    s->sd=(tab5_ui_medium_t){.known=true,.mounted=true};s->usb=s->sd;
    for(unsigned i=0;i<2;i++)s->capacity[i]=(tab5_ui_capacity_t){.known=true,.total_bytes=16ULL*1024*1024*1024,.free_bytes=12ULL*1024*1024*1024,.details_known=true,.estimate_known=true,.remaining_s=120000};
    strcpy(s->capacity[0].filesystem,"FAT32");strcpy(s->capacity[1].filesystem,"exFAT");
    s->tcp_clients_known=true;s->tcp_clients=1;s->tcp_peer_known=true;strcpy(s->tcp_peer,"192.0.2.20");
    s->link=(tab5_ui_link_t){.known=true,.ap_started=true,.sta_connected=true,.tcp_port=5000};strcpy(s->link.ap_ip,"192.0.2.1");strcpy(s->link.sta_ip,"192.0.2.10");
    s->network=(tab5_ui_network_t){.ap_enabled=true,.sta_enabled=true};strcpy(s->network.ap_ssid,"DEMO-TAB5");strcpy(s->network.sta_ssid,"DEMO-LAB");strcpy(s->network.ap_password,"********");strcpy(s->network.sta_password,"********");
    s->brightness_pct=80;s->timezone_known=true;s->timezone_minutes=180;s->backlight_timeout_known=true;s->backlight_timeout_s=1800;s->record_sync_known=true;s->record_sync=true;
    s->relative_config=(tab5_relative_config_t){.version=1,.reference_index=0,.object_index=1,.axes_confirmed=1};
    tab5_measure_config_default(&s->measurement.config);s->measurement.config.roll_calm_deg=.5f;s->measurement.stable=true;
    s->recent_known=true;s->recent_count=3;
    for(unsigned i=0;i<3;i++){s->recent[i]=(tab5_ui_recent_t){.known=true,.start_known=true,.start_unix=1794834000+i*1800,.duration_s=720,.rows=7200,.media_mask=3,.closed_ok=true};snprintf(s->recent[i].name,sizeof(s->recent[i].name),"DEMO_M%03u",i+1);}
    tab5_relative_t rel;tab5_relative_init(&rel,&s->relative_config);rel.mode=TAB5_MODE_LEVEL;rel.offset_valid=true;rel.offset[0]=.05f;rel.offset[1]=.10f;
    tab5_workspace_t work;tab5_workspace_config_t wc;tab5_workspace_config_default(&wc);wc.limits_enabled=true;wc.limit_deg[0]=wc.limit_deg[1]=.2f;tab5_workspace_init(&work,&wc);
    for(unsigned n=0;n<400;n++){
        s->now_us=700000000+n*200000;
        for(unsigned i=0;i<2;i++){
            float dx=n==399?0:(i?.08f:.025f)*sinf(n*.08f+i*.1f),dy=n==399?0:(i?.05f:.02f)*cosf(n*.06f+i*.1f);
            float x=(i?1.33f:1.20f)+dx,y=(i?-.34f:-.40f)+dy;
            s->measurement.sensor[i]=(tab5_measure_sensor_t){.address=2+i,.valid=true,.x=x,.y=y,.decoded_x=x,.decoded_y=y,.filtered_x=x,.filtered_y=y,.roll_x=.14f,.roll_y=.05f,.calm_x=true,.calm_y=true,.good=n+1,.timestamp_us=s->now_us-(i?60000:40000),.roll_fill_s=20};
        }
        tab5_relative_evaluate(&rel,&s->measurement,s->now_us,&s->relative);
        tab5_workspace_update(&work,&s->relative_config,&s->relative,s->now_us);tab5_workspace_snapshot(&work,&s->workspace);
        tab5_ui_update(s);
    }
    CHECK(s->relative.ready);CHECK(fabsf(s->relative.relative[0]-.08f)<.0001f);CHECK(fabsf(s->relative.relative[1]+.04f)<.0001f);
    s->relative_log.known=s->relative_log.session_known=s->relative_log.offset_valid=true;strcpy(s->relative_log.session,"DEMO_2026-11-16_M004");s->relative_log.config=s->relative_config;s->relative_log.offset[0]=.05f;s->relative_log.offset[1]=.10f;s->relative_log.generation=1;
    for(unsigned i=0;i<2;i++){s->relative_log.medium[i].known=true;s->relative_log.medium[i].closed_ok=true;s->relative_log.medium[i].rows=400;}
    s->rec_known=true;s->rec_duration_known=true;s->rec_duration_s=80;strcpy(s->rec_file,"DEMO_2026-11-16_M004");
}
static void numeric_checks(tab5_ui_state_t*s){
    CHECK(nui_obj_get_style_text_font(dual.value[0],0)==F128);
    CHECK(nui_obj_get_style_text_font(dual.raw[0][0],0)==F48);
    CHECK(strstr(nui_label_get_text(dual.raw[0][0]),"1.20"));CHECK(strstr(nui_label_get_text(dual.raw[1][0]),"1.33"));
    tab5_ui_state_t original=*s;
    s->relative.relative[0]=179.99f;s->relative.relative[1]=-179.99f;
    s->relative.source[0][0]=179.99f;s->relative.source[0][1]=-179.99f;
    tab5_ui_update(s);settle();
    for(unsigned a=0;a<2;a++){
        nui_point_t z;const nui_font_t*f=nui_obj_get_style_text_font(dual.value[a],0);nui_text_get_size(&z,nui_label_get_text(dual.value[a]),F128,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);printf("full delta F128 width=%d\n",z.x);nui_text_get_size(&z,nui_label_get_text(dual.value[a]),f,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);CHECK(z.x<=492);
        f=nui_obj_get_style_text_font(dual.raw[0][a],0);nui_text_get_size(&z,nui_label_get_text(dual.raw[0][a]),F48,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);printf("full raw F48 width=%d\n",z.x);nui_text_get_size(&z,nui_label_get_text(dual.raw[0][a]),f,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);CHECK(z.x<=208);CHECK(f==F48);
    }
    s->relative.ready=false;s->relative.quality=TAB5_PAIR_STALE;tab5_ui_update(s);CHECK(!strcmp(nui_label_get_text(dual.value[0]),"—"));CHECK(nui_obj_has_state(dual.capture,NUI_STATE_DISABLED));
    s->relative.timestamp_us[0]=0;s->relative.source[0][0]=NAN;tab5_ui_update(s);CHECK(!strcmp(nui_label_get_text(dual.raw[0][0]),"—"));
    *s=original;
    const float edge[]={-720.f,-180.f,-89.f,-88.999f,0.f,88.999f,89.f,180.f,720.f};
    for(unsigned unit=0;unit<2;unit++){
        if(dual.units!=unit){enc_action(A_REL_UNIT);}CHECK(dual.units==unit);
        for(unsigned n=0;n<sizeof(edge)/sizeof(edge[0]);n++){
            s->relative.relative[0]=edge[n];s->relative.relative[1]=-edge[n];tab5_ui_update(s);
            if(fabsf(edge[n])<=180){
                s->relative.source[0][0]=edge[n];s->relative.source[0][1]=-edge[n];tab5_ui_update(s);
                for(unsigned a=0;a<2;a++){
                    nui_point_t z;const nui_font_t*f=nui_obj_get_style_text_font(dual.raw[0][a],0);nui_text_get_size(&z,nui_label_get_text(dual.raw[0][a]),f,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);CHECK(f==F48);CHECK(z.x<=208);
                }
            }
            for(unsigned a=0;a<2;a++){
                const char*t=nui_label_get_text(dual.value[a]);nui_point_t z;const nui_font_t*f=nui_obj_get_style_text_font(dual.value[a],0);nui_text_get_size(&z,t,f,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);CHECK(z.x<=492);CHECK(z.y<=124);
                CHECK(!strstr(t,"nan")&&!strstr(t,"inf"));CHECK(strstr(nui_label_get_text(dual.axis[a]),unit?"мм/м":"°"));
                if(unit&&fabsf(edge[n])>=89)CHECK(!strcmp(t,"вне диапазона"));else CHECK(strcmp(t,"—")!=0);
            }
        }
    }
    dual.units=0;*s=original;tab5_ui_update(s);
    enc_action(A_HELP);CHECK(u.modal_kind==8);close_modal();CHECK(!nui_obj_has_state(dual.unit,NUI_STATE_DISABLED));
    memset(&action_seen,0,sizeof(action_seen));dual_send(TAB5_UI_REL_CAPTURE);CHECK(action_seen.kind==TAB5_UI_REL_CAPTURE);
}
int main(int argc,char**argv){
    CHECK(argc==2);char path[1024];snprintf(path,sizeof(path),"%s/index.tsv",argv[1]);index_file=fopen(path,"wb");CHECK(index_file);
    snprintf(path,sizeof(path),"%s/geometry.tsv",argv[1]);audit_file=fopen(path,"wb");CHECK(audit_file);
    for(unsigned light=0;light<2;light++){
        CHECK(nui_native_init(1280,720,fb));tab5_ui_init(cb,NULL);tab5_ui_state_t s;fixture(&s,light);
        for(unsigned mode=0;mode<2;mode++){
            s.mode=mode;tab5_ui_update(&s);const char*m=mode?"level":"heel";
            const char *pages[2][6]={{"angles","rolling","chart","recording","settings","network"},{"alignment","observation","quality","journal","settings","network"}};
            for(unsigned page=0;page<6;page++){
                show_page(page);tab5_ui_update(&s);if(page==GRAPH&&!mode)chart_refresh(true);if(mode&&page==0)numeric_checks(&s);
                shot(argv[1],m,pages[mode][page],light,"main");
                if(page==SETTINGS||page==NETWORK){
                    nui_obj_t*view=page==SETTINGS?u.settings_view:u.network_view,*content=page==SETTINGS?u.settings_content:u.network_content;
                    int*offset=page==SETTINGS?&e.settings_offset:&e.network_offset;*offset=nui_obj_get_height(content)-nui_obj_get_height(view);if(*offset<0)*offset=0;
                    if(page==SETTINGS){
                        const int positions[]={400,464,912};const char*names[]={"settings_rolling","settings_rolling_more","settings_device"};
                        for(unsigned section=0;section<3;section++){*offset=positions[section];nui_obj_set_y(content,-*offset);enc_scroll_thumb(view,content,*offset,0);shot(argv[1],m,names[section],light,"main");}
                        *offset=nui_obj_get_height(content)-nui_obj_get_height(view);
                    }
                    nui_obj_set_y(content,-*offset);enc_scroll_thumb(view,content,*offset,page==SETTINGS?0:1);
                    shot(argv[1],m,page==SETTINGS?"settings_bottom":"network_bottom",light,"main");*offset=0;nui_obj_set_y(content,0);enc_scroll_thumb(view,content,0,page==SETTINGS?0:1);
                }
            }
        }
        show_page(SETTINGS);tab5_ui_update(&s);dual_open();shot(argv[1],"shared","instrument_roles",light,"dialog");close_modal();
        workspace_limits();shot(argv[1],"level","observation_options",light,"dialog");close_modal();
        open_time();shot(argv[1],"shared","date_time",light,"dialog");close_modal();
        open_number(0);shot(argv[1],"shared","numeric_keyboard",light,"dialog");close_modal();
        open_text(0);shot(argv[1],"shared","network_keyboard",light,"dialog");close_modal();
        for(unsigned n=0;n<sizeof(help_pages)/sizeof(help_pages[0]);n++){help_open();help.index=n;help_refresh();char page[40];snprintf(page,sizeof(page),"help_%02u",n+1);shot(argv[1],"shared",page,light,"help");close_modal();}
        /* Validation is separate from presentation-ready healthy pages. */
        show_page(MEASURE);tab5_ui_update(&s);s.relative.relative[0]=179.99f;s.relative.relative[1]=-179.99f;s.relative.source[0][0]=179.99f;s.relative.source[0][1]=-179.99f;tab5_ui_update(&s);shot(argv[1],"level","full_angles",light,"validation");
        dual.units=1;tab5_ui_update(&s);shot(argv[1],"level","range_message",light,"validation");
        s.relative.relative[0]=88.99f;s.relative.relative[1]=-88.99f;tab5_ui_update(&s);shot(argv[1],"level","slope_limit",light,"validation");dual.units=0;
        s.relative.ready=false;s.relative.quality=TAB5_PAIR_STALE;tab5_ui_update(&s);shot(argv[1],"level","stale_pair",light,"validation");
        tab5_ui_destroy();nui_native_deinit();
    }
    fclose(index_file);fclose(audit_file);printf("Production Tab5 gallery: %u checks PASS; fixture-only state; no UI substitutions or hardware.\n",checks);return 0;
}
