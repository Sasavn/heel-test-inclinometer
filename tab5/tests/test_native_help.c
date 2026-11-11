/* Real production RGB565 renderer and touch/encoder paths, no OS window. */
#include "../main/tab5_ui.c"
static uint16_t fb[1280*720],proof[1280*720];
static unsigned checks,actions;static FILE *geometry;
#define CHECK(c) do{checks++;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(2);}}while(0)
static void callback(const tab5_ui_action_t*a,void*x){(void)a;(void)x;actions++;}
static void click(nui_obj_t*o){nui_obj_update_layout(nui_screen_active());nui_area_t a;nui_obj_get_coords(o,&a);tab5_ui_touch((a.x1+a.x2)/2,(a.y1+a.y2)/2,true,100);tab5_ui_touch((a.x1+a.x2)/2,(a.y1+a.y2)/2,false,130);}
static uint32_t utf8(const char **s){const unsigned char*p=(const unsigned char*)*s;uint32_t c=*p++;if(c>=0xc0){unsigned n=c<0xe0?1:c<0xf0?2:3;c&=n==1?31:n==2?15:7;while(n--)c=(c<<6)|(*p++&63);}*s=(const char*)p;return c;}
static void readable(nui_obj_t*o){
    if(nui_obj_check_type(o,&nui_label_class)){
        const char*s=nui_label_get_text(o);const nui_font_t*f=nui_obj_get_style_text_font(o,0);nui_point_t size;
        nui_text_get_size(&size,s,f,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);
        nui_area_t a;nui_obj_get_coords(o,&a);CHECK(size.x<=nui_obj_get_width(o));CHECK(size.y<=nui_obj_get_height(o));
        CHECK(a.x1>=184&&a.x2<1264&&a.y1>=76&&a.y2<696);CHECK(f->line_height>=28);
        fprintf(geometry,"%u\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n",help.index,a.x1,a.y1,a.x2,a.y2,size.x,size.y,s);
        while(*s){uint32_t cp=utf8(&s);nui_font_glyph_dsc_t d;if(!nui_font_get_glyph_dsc(f,&d,cp,0)||d.is_placeholder){fprintf(stderr,"Missing glyph U+%04x page %u in %s\n",cp,help.index,nui_label_get_text(o));exit(2);}checks++;}
    }
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++)readable(nui_obj_get_child(o,i));
}
static void shot(const char*dir,const char*name,unsigned theme,unsigned mode){nui_native_render();memcpy(proof,fb,sizeof(fb));nui_obj_invalidate(u.root);nui_native_render();CHECK(!memcmp(fb,proof,sizeof(fb)));char p[1024];snprintf(p,sizeof(p),"%s/%s-%u-%u.rgb565",dir,name,theme,mode);FILE*f=fopen(p,"wb");CHECK(f);fwrite(fb,sizeof(fb),1,f);fclose(f);}
int main(int argc,char**argv){CHECK(argc==2);char path[1024];snprintf(path,sizeof(path),"%s/geometry.tsv",argv[1]);geometry=fopen(path,"w");CHECK(geometry);
for(unsigned theme=0;theme<2;theme++)for(unsigned mode=0;mode<2;mode++){
    CHECK(nui_native_init(1280,720,fb));tab5_ui_init(callback,NULL);
    tab5_ui_state_t s={.light_theme=theme,.dual_mode_known=true,.mode=mode,.now_known=true,.now_us=1000000,.relative_config={.version=1,.reference_index=0,.object_index=1,.axes_confirmed=1},.relative={.ready=true,.live={1,1},.relative={0,0},.offset={1,1},.offset_valid=true,.source={{2,2},{3,3}},.timestamp_us={900000,920000},.sequence={1,1},.age_us={100000,80000},.skew_us=20000}};
    tab5_measure_config_default(&s.measurement.config);s.measurement.sensor[0].address=2;s.measurement.sensor[1].address=3;s.measurement.sensor[0].zero_x=2;s.measurement.sensor[1].zero_x=3;tab5_ui_update(&s);
    show_page(SETTINGS);tab5_ui_update(&s);u.draft[0]=17;u.dirty=true;unsigned before=actions;dual.units=1;
    click(help.entry);CHECK(u.modal_kind==8);CHECK(nui_obj_has_state(help.previous,NUI_STATE_DISABLED));
    for(unsigned i=0;i<sizeof(help_pages)/sizeof(help_pages[0]);i++){
        CHECK(help.index==i);nui_obj_update_layout(nui_screen_active());readable(u.dialog);char name[32];snprintf(name,sizeof(name),"help-%u",i);shot(argv[1],name,theme,mode);
        tab5_ui_update(&s);CHECK(u.modal_kind==8);CHECK(u.state.mode==mode);CHECK(dual.units==1);CHECK(u.state.relative.offset[0]==1);CHECK(u.state.measurement.sensor[0].zero_x==2);
        if(i+1<sizeof(help_pages)/sizeof(help_pages[0]))click(help.next);
    }
    CHECK(nui_obj_has_state(help.next,NUI_STATE_DISABLED));click(help.previous);CHECK(help.index==6);
    click(enc_button(A_MODAL_CANCEL));CHECK(!u.modal_kind&&u.page==SETTINGS);CHECK(u.draft[0]==17&&u.dirty);CHECK(actions==before);
    /* Encoder entry and back preserve the selected control, mode and units. */
    e.level=ENC_SELECT;e.index=1;e.ring=true;e.last_ms=800;tab5_ui_input(TAB5_UI_INPUT_PRESS,0,900);CHECK(u.modal_kind==8);tab5_ui_input(TAB5_UI_INPUT_LONG,0,1000);CHECK(!u.modal_kind&&e.index==1&&e.level==ENC_SELECT);
    u.dirty=false;show_page(MEASURE);tab5_ui_update(&s);
    if(mode){e.ring=false;encoder_paint();shot(argv[1],"level-entry",theme,mode);click(help.level_entry);CHECK(u.modal_kind==8);click(enc_button(A_MODAL_CANCEL));CHECK(u.page==MEASURE&&u.state.mode==TAB5_MODE_LEVEL);e.level=ENC_SELECT;e.index=4;enc_activate();CHECK(u.modal_kind==8);close_modal();}
    show_page(SETTINGS);tab5_ui_update(&s);shot(argv[1],"settings-entry",theme,mode);
    size_t bytes=0,count=0;for(unsigned cycle=0;cycle<32;cycle++){click(help.entry);click(help.next);click(help.previous);click(enc_button(A_MODAL_CANCEL));nui_native_render();nui_native_stats_t st;nui_native_profile_snapshot(&st);if(cycle==0){bytes=st.allocation_bytes;count=st.allocation_count;}else{CHECK(bytes==st.allocation_bytes);CHECK(count==st.allocation_count);}CHECK(e.buttons_n<256);}
    CHECK(actions==before);e.level=ENC_SELECT;e.index=2;e.ring=true;e.last_ms=1800;tab5_ui_input(TAB5_UI_INPUT_PRESS,0,1900);CHECK(e.level==ENC_EDIT&&e.edit==0);tab5_ui_input(TAB5_UI_INPUT_LONG,0,2000);CHECK(e.level==ENC_SELECT);e.index=0;tab5_ui_input(TAB5_UI_INPUT_PRESS,0,2100);CHECK(u.modal_kind==6);close_modal();tab5_ui_destroy();nui_native_deinit();
}
fclose(geometry);printf("native help: %u checks PASS; 8 pages, both themes/modes; touch/encoder/back/state/glyph/layout/allocation\n",checks);return 0;}
