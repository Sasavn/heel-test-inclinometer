/* Reuse every retained-cache mutation and actual SETTINGS gesture from 179.
 * Compile once with default production occlusion, once with its pass disabled. */
#define main retained_cache_original_main
#include "test_native_layer_cache.c"
#undef main
static bool tracing;
static unsigned callback_events;
static void trace_draw(nui_event_t *e){
    nui_layer_t *l=nui_event_get_layer(e);nui_event_code_t code=nui_event_get_code(e);
    int record[8]={(int)code,0,0,0,0,0,-1,0};
    if(l){record[1]=l->clip.x1;record[2]=l->clip.y1;record[3]=l->clip.x2;record[4]=l->clip.y2;record[5]=l->opa;}
    if(code==NUI_EVENT_DRAW_TASK_ADDED)record[6]=nui_draw_task_get_type(nui_event_get_draw_task(e));
    if(tracing){callback_events++;assert(fwrite(record,sizeof(record),1,raw)==1);}
    if(l){
        nui_draw_rect_dsc_t d={.bg_color=0x856731,.bg_opa=103};nui_area_t a={l->clip.x1,l->clip.y1,l->clip.x1+3,l->clip.y1+2};
        nui_draw_rect(l,&d,&a);
    }
}
static void traced_frame(nui_area_t dirty,unsigned phase,bool reachable){
    n.dirty_count=0;nui_native_invalidate(&dirty);
    uint64_t splits=layer_stats.occlusion_splits,fallbacks=layer_stats.occlusion_fallbacks;
    unsigned events=callback_events;
    assert(fwrite(&phase,sizeof(phase),1,raw)==1);tracing=true;nui_native_render();tracing=false;
    assert(n.rendered_count==1&&!memcmp(&n.rendered[0],&dirty,sizeof(dirty)));
    assert(fwrite(n.rendered,sizeof(dirty),1,raw)==1);
    assert(fwrite(frame,(size_t)n.width*n.height*2,1,raw)==1);
    if(reachable){
        assert(callback_events>events);
        assert(layer_stats.occlusion_splits==splits);
        if(NUI_LAYER_CACHE_OCCLUSION&&enabled)assert(layer_stats.occlusion_fallbacks>fallbacks);
    }else{
        assert(callback_events==events);
        if(NUI_LAYER_CACHE_OCCLUSION&&enabled)assert(layer_stats.occlusion_splits>splits);
    }
}
static void callback_proof(void){
    assert(nui_native_init(240,180,frame));
    nui_obj_t *rail=box_at(nui_screen_active(),0,0,20,180,0x223344,255,0);
    nui_obj_add_event_cb(rail,trace_draw,NUI_EVENT_DRAW_MAIN,NULL);
    nui_obj_t *view=box_at(nui_screen_active(),30,10,200,160,0x998866,255,20);
    nui_obj_t *content=box_at(view,1,0,196,330,0,0,0);box_at(content,20,30,140,70,0x773355,255,14);
    opt(content,true);FILE *output=raw;raw=NULL;capture();raw=output;
    nui_area_t dirty={30,10,229,169};
    traced_frame(dirty,0,false); /* live rail callback outside dirty */
    /* Child's absolute bounds intersect dirty, but its parent's clip excludes
     * it. Both the planner and the renderer must agree it is unreachable. */
    nui_obj_t *p=box_at(nui_screen_active(),100,145,25,25,0,0,0);
    nui_obj_t *outside=box_at(p,0,-100,20,20,0,0,0);
    nui_obj_add_event_cb(outside,trace_draw,NUI_EVENT_DRAW_MAIN,NULL);
    traced_frame(dirty,1,false);
    nui_obj_add_event_cb(nui_screen_active(),trace_draw,NUI_EVENT_DRAW_MAIN,NULL);
    traced_frame(dirty,2,true);
    nui_obj_add_event_cb(nui_screen_active(),trace_draw,NUI_EVENT_DRAW_POST,NULL);
    traced_frame(dirty,3,true);
    nui_obj_add_flag(nui_screen_active(),NUI_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    nui_obj_add_event_cb(nui_screen_active(),trace_draw,NUI_EVENT_DRAW_TASK_ADDED,NULL);
    traced_frame(dirty,4,true);
    nui_obj_add_event_cb(nui_screen_active(),trace_draw,NUI_EVENT_ALL,NULL);
    traced_frame(dirty,5,true);
    nui_obj_add_flag(nui_screen_active(),NUI_OBJ_FLAG_OCCLUSION_PAINT_INERT);
    traced_frame(dirty,6,true); /* paint-inert still cannot multiply callbacks */
    nui_native_deinit();assert(!n.stats.allocation_bytes&&!n.stats.allocation_count&&!layer_stats.bytes);
    printf("PASS 7 callback scenarios, %u event records; exact count/order/clip/ROI trace\n",callback_events);
}
static void settings_store_proof(unsigned theme){
    ms=1000;assert(nui_native_init(1280,720,frame));nui_native_set_clock_cb(clock_cb);nui_layer_cache_reset_stats();
    tab5_ui_init(action,NULL);tab5_ui_set_scroll_cache_enabled(false);tab5_native_port_input_reset();
    tab5_ui_state_t s={0};tab5_measure_config_default(&s.measurement.config);s.light_theme=theme!=0;s.brightness_pct=60;
    tab5_ui_update(&s);nui_native_render();touch(76,570,true);touch(76,570,false);nui_native_render();
    nui_obj_t *content=find_content(nui_screen_active());assert(content);opt(content,true);
    touch(600,450,true);touch(600,413,true);nui_native_render();touch(600,413,false);nui_native_render();
    touch(600,450,true);touch(600,413,true);
    uint64_t fill=n.stats.primitive[NUI_PRIM_FILL].pixels,area=layer_stats.occlusion_pixels,splits=layer_stats.occlusion_splits;
    nui_native_render();
    fill=n.stats.primitive[NUI_PRIM_FILL].pixels-fill;area=layer_stats.occlusion_pixels-area;splits=layer_stats.occlusion_splits-splits;
    printf("STOREPROOF theme=%u fill_pixels=%llu interior_pixels=%llu splits=%llu\n",theme,
        (unsigned long long)fill,(unsigned long long)area,(unsigned long long)splits);
    if(NUI_LAYER_CACHE_OCCLUSION&&enabled){
        nui_area_t expected;nui_layer_t probe={.clip=n.rendered[0],.opa=255};
        assert(layer_cache_fresh_region(content,probe,&expected));
        assert(area==(uint64_t)nui_area_get_width(&expected)*nui_area_get_height(&expected)&&splits==1);
    }
    else assert(!area&&!splits);
    assert(n.rendered_count==1);assert(fwrite(n.rendered,sizeof(nui_area_t),1,raw)==1);assert(fwrite(frame,sizeof(frame),1,raw)==1);
    touch(600,413,false);tab5_native_port_input_close();tab5_ui_destroy();nui_native_deinit();
    assert(!n.stats.allocation_count&&!n.stats.allocation_bytes&&!layer_stats.bytes);
}
int main(int argc,char **argv){
    assert(argc==3);setbuf(stdout,NULL);enabled=true;raw=fopen(argv[1],"wb");assert(raw);
    for(unsigned theme=0;theme<2;theme++){nui_layer_cache_reset_stats();unit(theme);}
    scroll(1);callback_proof();settings_store_proof(0);settings_store_proof(1);fclose(raw);
    printf("PASS optimized/reference validation: 76 mutations + 104 SETTINGS frames both themes, corners, callbacks and unchanged ROI\n");
    return 0;
}
