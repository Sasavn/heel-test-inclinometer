/* Focused 1.6.14 regressions: public API, real production RGB565 renderer. */
#include "tab5_ui.h"
#include "native/native_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
NUI_FONT_DECLARE(font_tab5_16);
NUI_FONT_DECLARE(font_tab5_24);
static uint16_t fb[1280*720],before[1280*720],partial[1280*720],warm[1280*720];
static unsigned checks,actions;
static nui_obj_t *kb,*ta;
static nui_area_t last_dirty[512];static size_t last_dirty_count;
static bool inside(nui_area_t a,int x,int y){return x>=a.x1&&x<=a.x2&&y>=a.y1&&y<=a.y2;}
static nui_area_t rect(nui_obj_t *o){nui_area_t a;nui_obj_get_coords(o,&a);return a;}
static void begin(void){memcpy(before,fb,sizeof(fb));}
/* Capture dirty evidence before forced full render replaces it. Keyboard
 * global PRESSED affects every ITEMS key in accepted v1: full-keyboard fallback
 * is required for this style contract, not a performance-test waiver. */
static void exact(const char *name,const nui_area_t *allowed,unsigned count){
    nui_native_render();nui_area_t dirty[512];size_t n=nui_native_get_dirty(dirty,512);assert(n<=512);
    last_dirty_count=n;memcpy(last_dirty,dirty,n*sizeof(*dirty));
    for(int y=0;y<720;y++)for(int x=0;x<1280;x++){
        bool covered=false;for(size_t i=0;i<n;i++)if(inside(dirty[i],x,y)){covered=true;break;}
        if(fb[y*1280+x]!=before[y*1280+x])assert(covered);
        if(covered&&allowed){bool ok=false;for(unsigned i=0;i<count;i++)if(inside(allowed[i],x,y)){ok=true;break;}if(!ok)fprintf(stderr,"DIRTY_BOUND %s escaped allowed union at %d,%d\n",name,x,y);assert(ok);}
    }
    memcpy(partial,fb,sizeof(fb));nui_native_invalidate(&(nui_area_t){0,0,1279,719});nui_native_render();
    assert(!memcmp(partial,fb,sizeof(fb)));checks++;printf("PARTIAL_FULL %s PASS\n",name);
}
static void init(void){assert(nui_native_init(1280,720,fb));}
static void end(void){nui_native_deinit();nui_native_stats_t s;nui_native_profile_snapshot(&s);assert(s.allocation_bytes==0&&s.allocation_count==0&&!s.allocation_failures);}
static nui_area_t key(const char *text){
    nui_area_t a[256];const char *t[256];size_t n=nui_native_keyboard_get_keys(kb,a,t,256);assert(n<=256);
    for(size_t i=0;i<n;i++)if(!strcmp(t[i],text))return a[i];assert(!"key missing");return a[0];
}
static void touch_key(const char *text,bool down,bool mode_change){
    (void)mode_change;nui_area_t a=key(text),allowed[2]={rect(kb),rect(ta)};begin();
    nui_native_touch((a.x1+a.x2)/2,(a.y1+a.y2)/2,down);exact(down?"key press":"key release",allowed,2);
}
static void click_key(const char *text){touch_key(text,true,false);touch_key(text,false,false);}
static void hold(const char *text,unsigned ms){(void)text;nui_area_t allowed[]={rect(kb),rect(ta)};begin();nui_native_tick(ms);nui_timer_handler();exact("held repeat",allowed,2);}
static void text_is(const char *s){assert(!strcmp(nui_textarea_get_text(ta),s));}
static void keyboard_group(void){
    init();ta=nui_textarea_create(nui_screen_active());nui_obj_set_pos(ta,20,20);nui_obj_set_size(ta,600,70);nui_textarea_set_one_line(ta,true);
    kb=nui_keyboard_create(nui_screen_active());nui_obj_set_pos(kb,0,320);nui_obj_set_size(kb,1280,400);nui_keyboard_set_textarea(kb,ta);nui_native_render();
    nui_obj_set_style_bg_opa(kb,255,NUI_PART_ITEMS);nui_obj_set_style_bg_color(kb,nui_color_hex(0x204060),NUI_PART_ITEMS);
    nui_obj_set_style_bg_color(kb,nui_color_hex(0xe08020),NUI_PART_ITEMS|NUI_STATE_PRESSED);
    nui_textarea_set_text(ta,"");nui_native_render();touch_key("q",true,false);text_is("q");
    nui_area_t keys[256];size_t key_count=nui_native_keyboard_get_keys(kb,keys,NULL,256);assert(key_count>20&&key_count<=256);
    for(size_t i=0;i<key_count;i++){
        bool changed=false;for(int y=keys[i].y1;y<=keys[i].y2;y++)for(int x=keys[i].x1;x<=keys[i].x2;x++){
            bool covered=false;for(size_t d=0;d<last_dirty_count;d++)if(inside(last_dirty[d],x,y)){covered=true;break;}assert(covered);
            if(before[y*1280+x]!=partial[y*1280+x])changed=true;
        }assert(changed);
    }
    printf("KEYBOARD_GLOBAL_PRESSED PASS all %zu key rectangles dirty and visibly changed under ITEMS|PRESSED\n",key_count);
    touch_key("q",false,false);text_is("q");
    /* Drag inside the keyboard and then lose capture outside it. Preserve the
     * existing key-on-press behavior and intermediate pixels, including the
     * accepted key_pressed rendering after PRESS_LOST. */
    nui_textarea_set_text(ta,"");nui_native_render();touch_key("q",true,false);text_is("q");
    nui_area_t moving=key("w"),allowed[]={rect(kb),rect(ta)};begin();nui_native_touch((moving.x1+moving.x2)/2,(moving.y1+moving.y2)/2,true);exact("move between keys",allowed,2);text_is("q");
    touch_key("w",false,false);text_is("q");touch_key("q",true,false);text_is("qq");
    begin();nui_native_touch(1279,200,true);exact("press lost outside keyboard",allowed,2);text_is("qq");
    begin();nui_native_touch(1279,200,false);exact("release after press lost",allowed,2);text_is("qq");
    nui_textarea_set_text(ta,"q");nui_native_render();
    touch_key("ABC",true,false);hold("ABC",900);text_is("q");touch_key("ABC",false,true);
    click_key("Q");text_is("qQ");touch_key("abc",true,false);touch_key("abc",false,true);
    touch_key("1#",true,false);touch_key("1#",false,true);click_key("&");text_is("qQ&");touch_key("abc",true,false);touch_key("abc",false,true);
    nui_textarea_set_text(ta,"яюяюяю");nui_native_render();touch_key(NUI_SYMBOL_BACKSPACE,true,false);text_is("яюяюя");
    hold(NUI_SYMBOL_BACKSPACE,401);text_is("яюяю");hold(NUI_SYMBOL_BACKSPACE,100);text_is("яюя");touch_key(NUI_SYMBOL_BACKSPACE,false,false);
    nui_textarea_set_text(ta,"ab");nui_native_render();click_key(NUI_SYMBOL_LEFT);click_key("x");text_is("axb");click_key(NUI_SYMBOL_RIGHT);click_key(" ");text_is("axb ");
    begin();nui_textarea_set_cursor_pos(ta,0);nui_area_t a=rect(ta);exact("cursor setter",&a,1);
    begin();nui_native_tick(401);nui_timer_handler();exact("caret blink",&a,1);
    /* Rebind focused textarea; caches must not retain the old text owner. */
    nui_obj_t *other=nui_textarea_create(nui_screen_active());nui_obj_set_pos(other,650,20);nui_obj_set_size(other,600,70);nui_keyboard_set_textarea(kb,other);ta=other;nui_native_render();click_key("z");text_is("z");
    begin();nui_keyboard_set_mode(kb,NUI_KEYBOARD_MODE_NUMBER);nui_area_t all[]={rect(kb),rect(ta)};exact("numeric mode",all,2);click_key("7");text_is("z7");
    static const char *const map[]={"u","v",NUI_SYMBOL_LEFT,NUI_SYMBOL_RIGHT,NUI_SYMBOL_BACKSPACE,""};
    static const nui_buttonmatrix_ctrl_t ctrl[]={1,1,1,1,1};
    begin();nui_keyboard_set_map(kb,NUI_KEYBOARD_MODE_TEXT_LOWER,map,ctrl);nui_keyboard_set_mode(kb,NUI_KEYBOARD_MODE_TEXT_LOWER);exact("custom map replace",all,2);
    click_key("u");text_is("z7u");touch_key(NUI_SYMBOL_LEFT,true,false);hold(NUI_SYMBOL_LEFT,401);touch_key(NUI_SYMBOL_LEFT,false,false);click_key("v");text_is("zv7u");
    end();puts("KEYBOARD_DIRTY PASS conservative-full-keyboard/press/move/release/presslost/NO_REPEAT/UTF8-repeat/mode/map/rebind/cursor partial-full exact");
}
typedef struct {nui_obj_t *parent,*button,*label;} scene;
static scene make_scene(void){
    scene s={0};s.parent=nui_obj_create(nui_screen_active());nui_obj_set_pos(s.parent,100,90);nui_obj_set_size(s.parent,600,220);
    nui_obj_set_style_bg_grad_dir(s.parent,NUI_GRAD_DIR_HOR,0);nui_obj_set_style_bg_grad_color(s.parent,nui_color_hex(0x304080),0);
    nui_obj_set_style_shadow_width(s.parent,12,0);nui_obj_set_style_shadow_opa(s.parent,80,0);
    nui_obj_set_style_text_color(s.parent,nui_color_hex(0xf0f0f0),0);nui_obj_set_style_text_font(s.parent,&font_tab5_16,0);
    s.button=nui_button_create(s.parent);nui_obj_set_pos(s.button,30,30);nui_obj_set_size(s.button,300,100);
    nui_obj_set_style_border_width(s.button,3,0);nui_obj_set_style_border_color(s.button,nui_color_hex(0x55aa22),0);
    nui_obj_set_style_bg_color(s.button,nui_color_hex(0x202030),0);nui_obj_set_style_bg_color(s.button,nui_color_hex(0x908020),NUI_STATE_DISABLED);
    s.label=nui_label_create(s.button);nui_obj_set_pos(s.label,12,12);nui_obj_set_size(s.label,260,60);nui_label_set_text(s.label,"Cache яю 123");return s;
}
static void variant(scene s,unsigned v){
    nui_obj_set_style_text_color(s.parent,nui_color_hex(v&1?0x22bbff:0xffaa22),0);
    nui_obj_set_style_text_font(s.parent,v&2?&font_tab5_24:&font_tab5_16,0);
    nui_obj_set_pos(s.parent,100+(int)(v%3)*11,90+(int)(v%4)*7);nui_obj_set_size(s.parent,600-(int)v*3,220+(int)v*2);
    nui_obj_set_style_pad_left(s.button,5+(int)v,0);nui_obj_set_style_border_color(s.button,nui_color_hex(v&1?0xff2244:0x44ff22),0);
    if(v&4)nui_obj_add_state(s.button,NUI_STATE_DISABLED);else nui_obj_remove_state(s.button,NUI_STATE_DISABLED);
}
static void cache_group(void){
    for(unsigned v=0;v<8;v++){
        init();scene s=make_scene();variant(s,(v+3)%8);nui_native_render();begin();variant(s,v);exact("warm state/inherited/font/layout",NULL,0);
        nui_area_t geometry=rect(s.label);const nui_font_t *f=nui_obj_get_style_text_font(s.label,0);assert(f==(v&2?&font_tab5_24:&font_tab5_16));
        assert(nui_color_eq(nui_obj_get_style_prop(s.label,0,NUI_STYLE_TEXT_COLOR).color,nui_color_hex(v&1?0x22bbff:0xffaa22)));
        memcpy(warm,fb,sizeof(fb));end();init();s=make_scene();variant(s,v);nui_native_render();nui_area_t cold=rect(s.label);
        assert(!memcmp(&geometry,&cold,sizeof(cold))&&!memcmp(warm,fb,sizeof(fb)));end();
    }puts("CACHE_STATE PASS 8 warm/cold exact state/inherited-color/font/layout/border variants");
}
static unsigned unknown_main,unknown_post,unknown_all;
static void custom(nui_event_t *e){unsigned *counter=nui_event_get_user_data(e);(*counter)++;}
static void custom_partial_exact(const char *name){
    unsigned m=unknown_main,p=unknown_post,a=unknown_all;
    size_t regions=nui_native_render();assert(regions>0);
    /* Count the incremental pass BEFORE the forced full render; the latter
     * must never hide a skipped unknown callback in the optimized pass. */
    assert(unknown_main==m+regions&&unknown_post==p+regions&&unknown_all==a+2*regions);
    memcpy(partial,fb,sizeof(fb));m=unknown_main;p=unknown_post;a=unknown_all;
    nui_native_invalidate(&(nui_area_t){0,0,1279,719});assert(nui_native_render()==1);
    assert(unknown_main==m+1&&unknown_post==p+1&&unknown_all==a+2);
    assert(!memcmp(partial,fb,sizeof(fb)));checks++;
    printf("UNKNOWN_CALLBACK_COUNTS %s incremental_regions=%zu MAIN/POST/ALL exact; full RGB exact PASS\n",name,regions);
}
static void callback_group(void){
    init();nui_obj_t *early=nui_button_create(nui_screen_active());nui_obj_set_size(early,1280,720);
    nui_obj_t *child=nui_obj_create(early);nui_obj_set_size(child,1280,720);
    nui_obj_add_event_cb(child,custom,NUI_EVENT_DRAW_MAIN,&unknown_main);nui_obj_add_event_cb(child,custom,NUI_EVENT_DRAW_POST,&unknown_post);nui_obj_add_event_cb(child,custom,NUI_EVENT_ALL,&unknown_all);
    nui_native_render();
    begin();nui_obj_add_state(early,NUI_STATE_PRESSED);custom_partial_exact("unknown custom press");
    begin();nui_obj_remove_state(early,NUI_STATE_PRESSED);custom_partial_exact("unknown custom release");
    nui_obj_t *cover=nui_obj_create(nui_screen_active());nui_obj_set_size(cover,1280,720);nui_obj_set_style_bg_opa(cover,255,0);
    custom_partial_exact("unknown sibling under opaque cover");
    end();puts("UNKNOWN_CUSTOM PASS press invalidation and covered sibling MAIN/POST/ALL preserved");
}
static void action(const tab5_ui_action_t *a,void *ctx){(void)a;(void)ctx;actions++;}
static nui_obj_t *label(nui_obj_t *o,const char *s){
    if(!nui_obj_is_visible(o))return NULL;if(nui_obj_check_type(o,&nui_label_class)&&!strcmp(nui_label_get_text(o),s))return o;
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++){nui_obj_t *r=label(nui_obj_get_child(o,i),s);if(r)return r;}return NULL;
}
static void tap(int x,int y){begin();nui_native_touch(x,y,true);exact("modal press",NULL,0);begin();nui_native_touch(x,y,false);exact("modal release",NULL,0);}
static void click(const char *s){nui_obj_t *o=label(nui_screen_active(),s);assert(o);nui_area_t a=rect(o);tap((a.x1+a.x2)/2,(a.y1+a.y2)/2);}
static void numeric_group(void){
    for(unsigned theme=0;theme<2;theme++){
        init();tab5_ui_init(action,NULL);tab5_ui_state_t s={.light_theme=theme,.now_known=true,.now_us=100000000};tab5_measure_config_default(&s.measurement.config);tab5_ui_update(&s);nui_native_render();
        tap(52,396);tap(535,200);click("C");click("7");click("5");nui_obj_t *done=nui_obj_get_parent(label(nui_screen_active(),"Готово"));assert(nui_obj_has_state(done,NUI_STATE_DISABLED));
        nui_obj_t *number=label(nui_screen_active(),"75");assert(number);nui_obj_t *frame=nui_obj_get_parent(number);
        nui_color_t invalid_border=nui_obj_get_style_prop(frame,0,NUI_STYLE_BORDER_COLOR).color;
        unsigned saved=actions;click("Готово");assert(actions==saved);
        /* 75 -> 7 -> empty -> 20 -> 207 -> 20: actual numeric buttons, not keyboard. */
        click("⌫");click("C");click("2");click("0");done=nui_obj_get_parent(label(nui_screen_active(),"Готово"));assert(!nui_obj_has_state(done,NUI_STATE_DISABLED));
        nui_color_t valid_border=nui_obj_get_style_prop(frame,0,NUI_STYLE_BORDER_COLOR).color;assert(!nui_color_eq(valid_border,invalid_border));
        click("7");assert(nui_obj_has_state(done,NUI_STATE_DISABLED));assert(nui_color_eq(nui_obj_get_style_prop(frame,0,NUI_STYLE_BORDER_COLOR).color,invalid_border));
        click("⌫");assert(!nui_obj_has_state(done,NUI_STATE_DISABLED));assert(nui_color_eq(nui_obj_get_style_prop(frame,0,NUI_STYLE_BORDER_COLOR).color,valid_border));
        click("Готово");tab5_ui_destroy();end();
    }puts("NUMERIC_MODAL PASS both themes invalid/valid/disabled/border redraw partial-full exact");
}
static void cold_group(void){
    for(unsigned i=0;i<16;i++){init();scene s=make_scene();variant(s,i%8);nui_native_render();nui_native_stats_t warmstats;nui_native_profile_snapshot(&warmstats);assert(warmstats.allocation_bytes>0);
        assert(warmstats.shadow_cache_bytes>=warmstats.shadow_cache_static_bytes&&warmstats.shadow_cache_bytes<=warmstats.shadow_cache_capacity_bytes);
        assert(warmstats.gradient_cache_bytes<=warmstats.gradient_cache_capacity_bytes);
        nui_obj_delete(s.parent);nui_native_render();end();}
    puts("COLD_MEMORY PASS 16 cold init/style/font/layout/delete/deinit zero retained allocations");
}
int main(int argc,char **argv){
    assert(argc==2);const char *g=argv[1];
    if(!strcmp(g,"keyboard"))keyboard_group();else if(!strcmp(g,"cache"))cache_group();else if(!strcmp(g,"custom"))callback_group();else if(!strcmp(g,"numeric"))numeric_group();else if(!strcmp(g,"cold-memory"))cold_group();else assert(!"unknown group");
    printf("TARGETED_GROUP %s fine=%d checks=%u PASS\n",g,NUI_PROFILE_FINE_COUNTERS,checks);return 0;
}
