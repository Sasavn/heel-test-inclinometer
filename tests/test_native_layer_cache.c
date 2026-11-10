/* Real native renderer + actual SETTINGS, on/off through public cache APIs. */
#include <stdlib.h>
#include <stdbool.h>
static bool fail_all,fail_surface;
static void *test_calloc(size_t count,size_t size){
    if(fail_all||(fail_surface&&count*size>1024*1024))return NULL;
    return calloc(count,size);
}
static unsigned long long callback_audit_visits;
#define NUI_CALLBACK_AUDIT_VISIT() (++callback_audit_visits)
#define calloc test_calloc
#include "../tab5/main/native/native_ui.c"
#undef calloc
#include "tab5_ui.h"
#include "tab5_native_port.h"
static uint16_t frame[1280*720],proof[1280*720];
static FILE *raw,*csv;
static bool enabled;
static unsigned checks,callbacks,draw_calls,ms;
static double stamp(void){
#ifdef _WIN32
    static LARGE_INTEGER f;LARGE_INTEGER t;if(!f.QuadPart)QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t);return (double)t.QuadPart*1e6/f.QuadPart;
#else
    struct timespec t;timespec_get(&t,TIME_UTC);return (double)t.tv_sec*1e6+t.tv_nsec/1000.;
#endif
}
static uint64_t clock_cb(void){return (uint64_t)stamp();}
static void capture(void){
    uint16_t *saved=n.fb;int w=n.width,h=n.height;
    nui_area_t all={0,0,w-1,h-1};nui_native_invalidate(&all);nui_native_render();
    assert(n.fb==saved&&n.width==w&&n.height==h);checks++;
    if(raw)assert(fwrite(frame,(size_t)w*h*2,1,raw)==1);
}
static void opt(nui_obj_t *o,bool matte){
    assert(matte?nui_obj_set_layer_cache_parent_matte(o,enabled):nui_obj_set_layer_cache(o,enabled));
    assert(nui_obj_get_layer_cache_enabled(o)==enabled);
}
static nui_obj_t *box_at(nui_obj_t *p,int x,int y,int w,int h,unsigned bg,unsigned opa,int radius){
    nui_obj_t *o=nui_obj_create(p);assert(o);nui_obj_set_pos(o,x,y);nui_obj_set_size(o,w,h);
    nui_obj_set_style_bg_color(o,bg,0);nui_obj_set_style_bg_opa(o,opa,0);nui_obj_set_style_radius(o,radius,0);return o;
}
static void custom_draw(nui_event_t *e){
    draw_calls++;nui_draw_rect_dsc_t d={.bg_color=0x998877,.bg_opa=160};nui_area_t a;
    nui_obj_get_coords(nui_event_get_target(e),&a);nui_draw_rect(nui_event_get_layer(e),&d,&a);
}
static void unit(unsigned theme){
    assert(nui_native_init(240,160,frame));nui_native_set_clock_cb(clock_cb);
    nui_obj_set_style_bg_color(nui_screen_active(),theme?0xeef3f9:0x122035,0);
    nui_obj_t *view=box_at(nui_screen_active(),7,9,180,135,theme?0xffffff:0x1b2233,255,20);
    nui_obj_set_style_border_width(view,1,0);nui_obj_set_style_border_color(view,0x775522,0);
    nui_obj_t *content=box_at(view,1,0,176,330,0,0,16);
    nui_obj_t *child=box_at(content,0,0,176,60,0x557799,255,16);
    nui_obj_t *label=nui_label_create(child);nui_obj_set_pos(label,24,4);nui_obj_set_size(label,150,42);
    nui_label_set_text(label,"Text UTF-8 тест");opt(content,true);capture();
    nui_layer_cache_stats_t initial;nui_layer_cache_get_stats(&initial);
    if(enabled){assert(initial.hits&&initial.builds==1&&initial.bytes==176*330*2);}
    nui_obj_set_y(content,-37);capture();nui_obj_set_pos(view,21,13);capture();
    if(enabled){nui_layer_cache_stats_t st;nui_layer_cache_get_stats(&st);assert(st.builds==initial.builds&&st.hits>=initial.hits+2);}
    /* Old/new child bounds repair locally, including changes outside viewport. */
    nui_label_set_text(label,"changed");capture();
    if(enabled){nui_layer_cache_stats_t st;nui_layer_cache_get_stats(&st);assert(st.updates>0&&st.builds==1);}
    nui_obj_set_pos(child,5,180);capture();nui_obj_set_y(content,-170);capture();
    nui_obj_set_size(child,120,80);capture();
    nui_obj_set_style_text_color(label,0xeeddcc,0);capture();
    nui_obj_set_style_text_letter_space(label,2,0);capture();
    nui_obj_add_flag(child,NUI_OBJ_FLAG_HIDDEN);capture();nui_obj_remove_flag(child,NUI_OBJ_FLAG_HIDDEN);capture();
    nui_obj_set_style_bg_color(view,theme?0xf8eedd:0x283849,0);capture();
    nui_obj_set_style_radius(view,31,0);capture();
    nui_obj_set_style_bg_opa(view,160,0);capture(); /* matte unsafe -> ordinary painter */
    nui_obj_set_style_bg_opa(view,255,0);capture();
    nui_obj_set_style_opa(content,150,0);capture();nui_obj_set_style_opa(content,255,0);capture();
    nui_obj_t *overlay=box_at(view,40,35,90,60,0x993322,150,7);capture(); /* later sibling remains on top */
    nui_obj_move_foreground(content);capture(); /* earlier visible sibling forbids matte */
    nui_obj_move_foreground(overlay);capture();
    nui_obj_delete(child);capture();
    child=box_at(content,10,220,100,60,0x335522,255,12);capture();
    nui_obj_add_event_cb(child,custom_draw,NUI_EVENT_DRAW_MAIN,NULL);unsigned old=draw_calls;capture();assert(draw_calls>old);
    nui_obj_delete(child);capture();
    /* Explicit clear/off/re-enable; API changes pixels neither way. */
    nui_obj_clear_layer_cache(content);capture();assert(nui_obj_set_layer_cache_parent_matte(content,false));capture();opt(content,true);capture();
    nui_native_deinit();assert(!n.stats.allocation_count&&!n.stats.allocation_bytes&&!layer_stats.bytes&&!layer_cache_requests);
    /* Truly opaque root, surface-size and aggregate-byte bounds. */
    assert(nui_native_init(240,160,frame));
    nui_obj_t *opaque=box_at(nui_screen_active(),5,4,180,120,0x446688,255,0);opt(opaque,false);capture();
    nui_obj_set_pos(opaque,-9,-7);capture();nui_obj_set_size(opaque,160,110);capture();
    nui_obj_set_size(opaque,4000,4000);capture();if(enabled)assert(!layer_stats.bytes);
    nui_obj_set_size(opaque,120,100);capture();
    nui_obj_delete(opaque);capture();assert(!layer_stats.bytes);
    /* PSRAM/host allocation failures: metadata, full surface, no retry storm. */
    opaque=box_at(nui_screen_active(),0,0,1094,1490,0x334455,255,0);
    if(enabled){fail_all=true;assert(!nui_obj_set_layer_cache(opaque,true));fail_all=false;}
    opt(opaque,false);fail_surface=true;capture();
    uint64_t failures=n.stats.allocation_failures;capture();if(enabled)assert(n.stats.allocation_failures==failures&&!layer_stats.bytes);
    fail_surface=false;opt(opaque,false);capture();if(enabled)assert(layer_stats.bytes==payload64(1094*1490*2));
    nui_obj_t *second=box_at(nui_screen_active(),1,1,1094,1490,0x445566,255,0);opt(second,false);capture();
    assert(layer_stats.bytes<=LAYER_CACHE_LIMIT);
    nui_obj_delete(opaque);nui_obj_clear_layer_cache(second);capture();
    if(enabled)assert(layer_stats.bytes==payload64(1094*1490*2));
    nui_native_deinit();nui_native_deinit();assert(!n.stats.allocation_count&&!n.stats.allocation_bytes&&!layer_stats.bytes&&!layer_cache_requests);
}
static void action(const tab5_ui_action_t *a,void *user){(void)a;(void)user;callbacks++;}
static void touch(int x,int y,bool down){ms+=16;assert(tab5_native_port_post_touch(x,y,down,ms));tab5_native_port_input_dispatch(ms);}
static nui_obj_t *find_content(nui_obj_t *o){
    if(nui_obj_get_width(o)==1094&&nui_obj_get_height(o)==1714)return o;
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++){nui_obj_t *c=find_content(nui_obj_get_child(o,i));if(c)return c;}return NULL;
}
static void scroll(unsigned cycles){
    for(unsigned theme=0;theme<2;theme++){
        ms=1000;assert(nui_native_init(1280,720,frame));nui_native_set_clock_cb(clock_cb);nui_layer_cache_reset_stats();
        tab5_ui_init(action,NULL);tab5_ui_set_scroll_cache_enabled(false);tab5_native_port_input_reset();tab5_ui_state_t s={0};tab5_measure_config_default(&s.measurement.config);
        s.light_theme=theme!=0;s.brightness_pct=60;s.now_known=true;s.now_us=1000000;strcpy(s.firmware,"layer-cache-fixture");
        tab5_ui_update(&s);nui_native_render();touch(76,570,true);touch(76,570,false);assert(nui_native_render());
        nui_obj_t *content=find_content(nui_screen_active());assert(content);opt(content,true);
        nui_area_t initial;nui_obj_get_coords(content,&initial);
        for(unsigned i=0;i<cycles*52;i++){
            int delta=i%52<26?37:-37;touch(600,450,true);
            double start=stamp();touch(600,450-delta,true);double before=stamp();size_t dirty=nui_native_render();double end=stamp();assert(dirty);
            nui_area_t a;nui_obj_get_coords(content,&a);int offset=initial.y1-a.y1;assert(offset>=0&&offset<=nui_obj_get_height(content)-540);
            if(csv)fprintf(csv,"%u,%u,%d,%.6f,%.6f\n",theme,i,offset,before-start,end-before);
            if(raw){
                assert(fwrite(frame,sizeof(frame),1,raw)==1);memcpy(proof,frame,sizeof(frame));
                nui_area_t full={0,0,1279,719};nui_native_invalidate(&full);nui_native_render();assert(!memcmp(proof,frame,sizeof(frame)));
            }
            touch(600,450-delta,false);nui_native_render();
        }
        nui_layer_cache_stats_t st;nui_layer_cache_get_stats(&st);
        if(enabled)assert(st.hits>=52&&st.builds==1&&st.updates==0&&st.bytes==payload64((size_t)nui_obj_get_width(content)*nui_obj_get_height(content)*2)&&st.blit_pixels>0);
        else assert(!st.hits&&!st.bytes);
        printf("theme=%u hits=%llu misses=%llu builds=%llu updates=%llu bytes=%zu blit_pixels=%llu fallbacks=%llu build_us=%llu\n",theme,
            (unsigned long long)st.hits,(unsigned long long)st.misses,(unsigned long long)st.builds,(unsigned long long)st.updates,st.bytes,
            (unsigned long long)st.blit_pixels,(unsigned long long)st.fallbacks,(unsigned long long)st.build_us);
        assert(!callbacks);tab5_native_port_input_close();tab5_ui_destroy();nui_native_deinit();
        assert(!n.stats.allocation_count&&!n.stats.allocation_bytes&&!layer_stats.bytes&&!layer_cache_requests);
    }
}
int main(int argc,char **argv){
    assert(argc==4);setbuf(stdout,NULL);enabled=!strcmp(argv[1],"on");
    if(!strcmp(argv[3],"proof")){
        raw=fopen(argv[2],"wb");assert(raw);for(unsigned theme=0;theme<2;theme++){nui_layer_cache_reset_stats();unit(theme);}
        scroll(1);fclose(raw);printf("PASS %u scene mutations, 104 unchanged SETTINGS frames, both themes/corners, partial==full, fallback/OOM/delete\n",checks);
    }else{csv=fopen(argv[2],"w");assert(csv);fprintf(csv,"theme,frame,offset,input_us,render_us\n");scroll(4);fclose(csv);}
    return 0;
}
