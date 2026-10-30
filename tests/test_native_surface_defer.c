/* Actual UI/renderer, simulated EXACT-ROI GPU90 + deferred portrait DMA0.
 * A separate run renders every scene fully via ordinary CPU with cache OFF.
 * Host compares complete persistent portrait output, not just submitted tiles. */
#define main layer_cache_original_main
#include "test_native_layer_cache.c"
#undef main
static uint16_t canonical[1280*720],rotated[1280*720],second_cpu[1280*720];
static bool double_targets;
static unsigned requested_surface_alignment=16,odd_seen;static bool expect_odd;
static nui_surface_view_t views[8];
static unsigned view_count,proposal_count,composed_frames,flushes;
static bool gpu_mode,reject_views;
static FILE *output;
static bool enqueue(const nui_surface_view_t *view,void *user){
    (void)user;proposal_count++;assert(view->storage_id&&view->portrait_generation);
    assert((uintptr_t)view->landscape%64==0&&(uintptr_t)view->portrait%64==0);
    assert(view->portrait_stride%32==0&&view->landscape_stride==view->logical_width);
    nui_area_t d=view->destination;
    if(nui_native_surface_alignment()==16)assert(!(d.x1&15)&&!(d.y1&15)&&!((d.x2+1)&15)&&!((d.y2+1)&15));
    if(expect_odd){assert(d.x1==2&&d.y1==4&&d.x2==4&&d.y2==6&&view->source_x==1&&view->source_y==1);odd_seen++;}
    if(reject_views||view_count==8)return false;
    for(unsigned i=0;i<view_count;i++){nui_area_t overlap;assert(!area_intersect(&overlap,views[i].destination,d));}
    views[view_count++]=*view;
    /* Prove the accepted destination is NOT subsequently CPU-blitted/painted. */
    for(int y=d.y1;y<=d.y2;y++)for(int x=d.x1;x<=d.x2;x++)n.fb[(size_t)y*n.width+x]=0x5aa5;
    return true;
}
static bool deferred_pixel(int x,int y){
    for(unsigned i=0;i<view_count;i++){nui_area_t a=views[i].destination;if(x>=a.x1&&x<=a.x2&&y>=a.y1&&y<=a.y2)return true;}
    return false;
}
static void rotate_cpu(void){
    for(int y=0;y<n.height;y++)for(int x=0;x<n.width;x++)rotated[(size_t)(n.width-1-x)*n.height+y]=n.fb[(size_t)y*n.width+x];
}
static void compose(bool diagnostic){
    assert(n.fb==frame||n.fb==second_cpu);
    for(unsigned i=0;i<view_count;i++){
        nui_area_t a=views[i].destination;
        for(int y=a.y1;y<=a.y2;y++)for(int x=a.x1;x<=a.x2;x++)assert(n.fb[(size_t)y*n.width+x]==0x5aa5);
    }
    /* EXACT actually rendered CPU rectangles minus accepted destinations. */
    for(size_t r=0;r<n.rendered_count;r++){
        nui_area_t a=n.rendered[r];
        for(int y=a.y1;y<=a.y2;y++)for(int x=a.x1;x<=a.x2;x++)if(!deferred_pixel(x,y))
            canonical[(size_t)(n.width-1-x)*n.height+y]=n.fb[(size_t)y*n.width+x];
    }
    /* Portrait DMA0: direct strided copies. Independent of CPU90 materializer. */
    for(unsigned i=0;i<view_count;i++){
        const nui_surface_view_t *v=&views[i];nui_area_t a=v->destination;
        int w=nui_area_get_width(&a),h=nui_area_get_height(&a);
        int sx=(int)v->source_y,sy=(int)v->logical_width-(int)v->source_x-w;
        int dx=a.y1,dy=n.width-1-a.x2;
        for(int row=0;row<w;row++)memcpy(canonical+(size_t)(dy+row)*n.height+dx,
            v->portrait+(size_t)(sy+row)*v->portrait_stride+sx,(size_t)h*2);
    }
    if(diagnostic){
        for(unsigned i=0;i<view_count;i++)assert(nui_surface_view_materialize(&views[i]));
        rotate_cpu();assert(!memcmp(canonical,rotated,(size_t)n.width*n.height*2));
    }
    if(output){int dimensions[2]={n.width,n.height};assert(fwrite(dimensions,sizeof(dimensions),1,output)==1);assert(fwrite(canonical,(size_t)n.width*n.height*2,1,output)==1);}
    for(unsigned i=0;i<view_count;i++){nui_surface_view_release(&views[i]);nui_surface_view_release(&views[i]);}
    view_count=0;assert(!layer_stats.pins);composed_frames++;
}
/* Frozen pre-refcount reachability oracle, counting actual visited nodes. */
static unsigned long long oracle_nodes,audit_nodes,baseline_nodes;
static bool audit_oracle(const nui_obj_t *o,nui_layer_t parent){
    oracle_nodes++;
    if(!nui_obj_is_visible(o))return false;
    nui_area_t a,visual=object_visual(o),check;
    if(!area_intersect(&check,parent.clip,visual))return false;
    nui_layer_t inner=parent;inner.opa=(uint8_t)((unsigned)parent.opa*(unsigned)num(o,NUI_STYLE_OPA,0)/255);
    if(!inner.opa)return false;
    if(layer_draw_callbacks(o))return true;
    nui_obj_get_coords(o,&a);if(!area_intersect(&inner.clip,parent.clip,a))return false;
    for(nui_obj_t *c=o->first;c;c=c->next)if(audit_oracle(c,inner))return true;
    return false;
}
static size_t audit_count(const nui_obj_t *o){
    size_t count=0;
    for(handler *h=o->handlers;h;h=h->next)if(potential_draw_code(h->code))count++;
    for(nui_obj_t *c=o->first;c;c=c->next)count+=audit_count(c);
    assert(o->potential_draw_handlers==count);return count;
}
static void audit_cached_queries(nui_obj_t *o){
    if((o->w==1094&&o->h==1714)||(o->w==1024&&o->h==120)){
        nui_layer_t q={.clip={0,0,n.width-1,n.height-1},.opa=255};
        unsigned long long before=callback_audit_visits;bool actual=callbacks_reachable(o,q);
        oracle_nodes=0;assert(actual==audit_oracle(o,q));
        audit_nodes+=callback_audit_visits-before;baseline_nodes+=oracle_nodes;
    }
    for(nui_obj_t *c=o->first;c;c=c->next)audit_cached_queries(c);
}
static void audit_compare(void){
    audit_count(n.root);
    nui_layer_t query={.clip={0,0,n.width-1,n.height-1},.opa=255};
    unsigned long long before=callback_audit_visits;bool actual=callbacks_reachable(n.root,query);
    oracle_nodes=0;assert(actual==audit_oracle(n.root,query));
    audit_nodes+=callback_audit_visits-before;baseline_nodes+=oracle_nodes;
    audit_cached_queries(n.root);
}
static void draw_frame(bool full,bool diagnostic){
    if(double_targets){
        uint16_t *next=n.fb==frame?second_cpu:frame;uint64_t epoch=framebuffer_epoch;
        size_t dirty=n.dirty_count;assert(nui_native_bind_framebuffer(next));
        assert(framebuffer_epoch==epoch+1&&n.dirty_count==dirty&&n.fb==next);
    }
    if(full||!gpu_mode){nui_area_t all={0,0,n.width-1,n.height-1};nui_native_invalidate(&all);}
    audit_compare();nui_native_render();compose((diagnostic&&(!double_targets||full))||!gpu_mode);
}
static void configure(void){
    assert(nui_native_set_dirty_alignment(16));assert(!nui_native_set_dirty_alignment(2));
    assert(nui_native_surface_alignment()==16);assert(nui_native_set_surface_alignment(requested_surface_alignment));
    assert(nui_native_surface_alignment()==requested_surface_alignment&&dirty_alignment==16);
    assert(!nui_native_set_surface_alignment(0)&&!nui_native_set_surface_alignment(2)&&nui_native_surface_alignment()==requested_surface_alignment);
    nui_native_set_surface_defer_cb(gpu_mode?enqueue:NULL,NULL);
    memset(canonical,0,sizeof(canonical));view_count=0;
}
static void actual_settings(unsigned theme){
    audit_nodes=baseline_nodes=0;
    ms=1000;assert(nui_native_init(1280,720,frame));configure();nui_layer_cache_reset_stats();
    tab5_ui_init(action,NULL);tab5_ui_set_scroll_cache_enabled(gpu_mode);tab5_native_port_input_reset();
    tab5_ui_state_t s={0};tab5_measure_config_default(&s.measurement.config);s.light_theme=theme!=0;s.brightness_pct=60;
    /* Real INFO notice, exactly the production x240/y584/1024x120 overlay
     * responsible for the cropped1711 HIL view. Never hide it for the test. */
    strcpy(s.status,"Дата, время и часовой пояс установлены");
    strcpy(s.firmware,"deferred-proof");tab5_ui_update(&s);touch(76,570,true);touch(76,570,false);
    nui_obj_t *notice=NULL;
    nui_obj_t *pending[256];unsigned pending_count=0;pending[pending_count++]=n.root;
    while(pending_count){nui_obj_t *o=pending[--pending_count];
        if(o->w==1024&&o->h==120)notice=o;
        for(nui_obj_t *c=o->first;c;c=c->next){assert(pending_count<256);pending[pending_count++]=c;}}
    assert(notice);assert(nui_obj_get_layer_cache_enabled(notice)==gpu_mode);
    draw_frame(true,true); /* first full reference, GPU path still enabled */
    if(gpu_mode){
        nui_layer_t query={.clip={0,0,1279,719},.opa=255};nui_area_t best;
        assert(layer_cache_split_region(n.root,query,&best));
        assert((uint64_t)nui_area_get_width(&best)*nui_area_get_height(&best)>(uint64_t)notice->w*notice->h);
    }
    for(unsigned i=0;i<52;i++){
        int delta=i<26?37:-37;touch(600,450,true);touch(600,450-delta,true);
        draw_frame(false,i==0); /* second check partial+materialize, then holes persist */
        touch(600,450-delta,false);
    }
    draw_frame(true,true); /* full resynchronization after historical CPU holes */
    if(gpu_mode){assert(layer_stats.deferred_views>=50&&layer_stats.deferred_pixels>0&&layer_stats.pins==0);}
    printf("SETTINGS theme=%u views=%llu gpu_pixels=%llu cpu_cache_pixels=%llu portrait_updates=%llu payload=%zu\n",theme,
        (unsigned long long)layer_stats.deferred_views,(unsigned long long)layer_stats.deferred_pixels,
        (unsigned long long)layer_stats.cpu_blit_pixels,(unsigned long long)layer_stats.portrait_updates,layer_stats.bytes);
    printf("SURFACE_ALIGNMENT tile=%u unaligned_views=%llu\n",nui_native_surface_alignment(),(unsigned long long)layer_stats.deferred_unaligned_views);
    if(gpu_mode&&requested_surface_alignment==1)assert(layer_stats.deferred_unaligned_views);
    printf("CALLBACK_AUDIT theme=%u optimized_nodes=%llu legacy_nodes=%llu\n",theme,audit_nodes,baseline_nodes);
    assert(audit_nodes<baseline_nodes);
    tab5_native_port_input_close();tab5_ui_destroy();nui_native_deinit();assert(!n.stats.allocation_bytes&&!layer_stats.bytes);
}
static void paint_callback(nui_event_t *e){
    nui_draw_rect_dsc_t d={.bg_color=0x73a852,.bg_opa=120};nui_area_t a;nui_obj_get_coords(nui_event_get_target(e),&a);
    nui_draw_rect(nui_event_get_layer(e),&d,&a);
}
static void flush(const nui_area_t *a,const uint16_t *pixels,size_t stride,void *user){(void)a;(void)pixels;(void)stride;(void)user;flushes++;}
static void overlays(unsigned theme){
    assert(nui_native_init(240,176,frame));configure();
    nui_obj_t *outer=box_at(nui_screen_active(),0,0,240,176,theme?0xf0f2f4:0x112233,255,0);
    nui_obj_t *view=box_at(outer,16,8,208,160,theme?0xffffff:0x1b2233,255,20);
    nui_obj_t *content=box_at(view,1,0,204,350,0,0,0);
    nui_obj_t *child=box_at(content,12,20,170,80,0x667755,255,14);
    nui_obj_t *text=nui_label_create(child);nui_obj_set_pos(text,15,12);nui_obj_set_size(text,140,50);nui_label_set_text(text,"AV UTF-8 текст");
    assert(nui_obj_set_layer_cache_parent_matte(content,gpu_mode));draw_frame(true,true);
    nui_obj_set_y(content,-37);draw_frame(false,true);
    nui_obj_t *notice=box_at(view,0,100,208,45,0x993344,150,8);draw_frame(false,true); /* later alpha */
    nui_obj_set_style_bg_opa(notice,255,0);draw_frame(false,true); /* later opaque */
    nui_obj_add_flag(notice,NUI_OBJ_FLAG_HIDDEN);draw_frame(false,true);
    nui_obj_remove_flag(notice,NUI_OBJ_FLAG_HIDDEN);draw_frame(false,true);
    nui_obj_add_event_cb(notice,paint_callback,NUI_EVENT_DRAW_MAIN,NULL);nui_obj_invalidate(notice);draw_frame(false,true);
    nui_obj_set_y(notice,130);draw_frame(false,true);
    nui_obj_t *parent_later=box_at(outer,140,20,50,85,0xcc8833,120,9);draw_frame(false,true);
    nui_obj_set_pos(parent_later,5,120);draw_frame(false,true);
    /* Adversarial cover chains: never treat an opaque child under a
     * translucent or clipped ancestor as a whole-region opaque cover. */
    nui_obj_add_flag(notice,NUI_OBJ_FLAG_HIDDEN);draw_frame(false,true);
    uint64_t opaque_before=layer_stats.deferred_views;
    nui_obj_set_style_opa(outer,128,0);draw_frame(false,true);
    if(gpu_mode)assert(layer_stats.deferred_views==opaque_before);
    nui_obj_set_style_opa(outer,255,0);draw_frame(false,true);
    nui_obj_set_size(outer,240,110);draw_frame(false,true);
    nui_obj_set_size(outer,240,176);draw_frame(false,true);
    nui_obj_t *clip_parent=box_at(outer,190,140,20,20,0,0,0);
    nui_obj_t *outside_child=box_at(clip_parent,-140,-120,200,140,0x559977,255,0);
    nui_obj_add_event_cb(outside_child,paint_callback,NUI_EVENT_DRAW_MAIN,NULL);draw_frame(false,true);
    nui_obj_delete(clip_parent);draw_frame(false,true);
    nui_obj_remove_flag(notice,NUI_OBJ_FLAG_HIDDEN);draw_frame(false,true);
    nui_obj_t *veil=box_at(nui_screen_active(),0,0,240,176,0x080e1e,140,0);
    uint64_t before=layer_stats.deferred_views;draw_frame(false,true);
    if(gpu_mode)assert(layer_stats.deferred_views==before); /* date/modal full-screen veil */
    nui_obj_add_flag(veil,NUI_OBJ_FLAG_HIDDEN);draw_frame(false,true);
    nui_label_set_text(text,"clock/text changed");draw_frame(false,true);
    nui_obj_set_style_bg_color(view,theme?0xf0eedd:0x283a40,0);draw_frame(false,true);
    nui_obj_set_pos(view,0,15);draw_frame(false,true);
    nui_obj_clear_layer_cache(content);draw_frame(false,true);
    reject_views=true;nui_obj_set_y(content,-70);draw_frame(false,true);reject_views=false;
    nui_native_set_flush_cb(flush,NULL);before=proposal_count;nui_obj_set_y(content,-90);draw_frame(false,true);
    assert(proposal_count==before&&flushes);nui_native_set_flush_cb(NULL,NULL);
    nui_obj_delete(notice);nui_obj_delete(parent_later);nui_obj_set_y(content,-120);draw_frame(false,true);
    nui_obj_delete(child);draw_frame(false,true);nui_native_deinit();assert(!n.stats.allocation_bytes&&!layer_stats.bytes);
}
static void odd_surface(unsigned theme){
    assert(nui_native_init(32,32,frame));configure();
    nui_obj_set_style_bg_color(n.root,theme?0xf1e2d3:0x123456,0);
    nui_obj_t *o=box_at(n.root,1,3,5,5,0x759abc,255,0);
    nui_obj_set_style_border_width(o,1,0);nui_obj_set_style_border_color(o,0xbe582a,0);
    assert(nui_obj_set_layer_cache(o,gpu_mode));
    unsigned seen=odd_seen;expect_odd=gpu_mode&&requested_surface_alignment==1;draw_frame(true,true);expect_odd=false;
    if(gpu_mode&&requested_surface_alignment==1)assert(odd_seen>seen);
    nui_obj_set_pos(o,3,5);draw_frame(false,true);
    nui_obj_t *overlay=box_at(n.root,4,6,3,3,0x997722,128,1);draw_frame(false,true);
    nui_obj_set_style_bg_opa(o,254,0);draw_frame(false,true);
    nui_obj_delete(overlay);nui_obj_set_style_bg_opa(o,255,0);draw_frame(false,true);
    nui_native_deinit();assert(!layer_stats.pins&&!layer_stats.bytes&&!n.stats.allocation_bytes);
    assert(nui_native_surface_alignment()==16);
}
static void surface_alignment_proof(void){
    assert(nui_native_surface_alignment()==16);assert(nui_native_set_dirty_alignment(16));
    assert(nui_native_set_surface_alignment(1)&&nui_native_surface_alignment()==1&&dirty_alignment==16);
    assert(!nui_native_set_surface_alignment(32)&&nui_native_surface_alignment()==1);
    assert(nui_native_init(32,32,frame)&&nui_native_surface_alignment()==16);
    assert(nui_native_set_surface_alignment(1));nui_native_deinit();assert(nui_native_surface_alignment()==16);
    printf("PASS surface alignment: independent 1/16 setter/getter, invalid reject, init/deinit legacy16 reset\n");
}
static unsigned own_clicks;
static void own_click(nui_event_t *e){(void)e;own_clicks++;}
static void own_rounded(unsigned theme){
    assert(nui_native_init(240,176,frame));configure();
    nui_obj_set_style_bg_color(n.root,theme?0xf0eee8:0x132835,0);
    nui_obj_t *panel=box_at(n.root,8,8,220,148,theme?0xffeedd:0x345678,255,19);
    nui_obj_set_style_border_width(panel,3,0);nui_obj_set_style_border_color(panel,0xa34f72,0);
    nui_obj_t *label=nui_label_create(panel);nui_obj_set_pos(label,9,9);nui_obj_set_size(label,180,80);
    nui_label_set_text(label,"Rounded UTF-8 текст");
    nui_obj_add_flag(panel,NUI_OBJ_FLAG_CLICKABLE);nui_obj_add_event_cb(panel,own_click,NUI_EVENT_CLICKED,NULL);
    assert(nui_obj_set_layer_cache(panel,gpu_mode));draw_frame(true,true);
    uint64_t builds=layer_stats.builds;
    nui_obj_set_pos(panel,-7,-5);draw_frame(false,true);
    if(gpu_mode)assert(layer_stats.builds==builds&&panel->layer_cache->valid);
    unsigned clicks=own_clicks;nui_native_touch(150,100,true);nui_native_touch(150,100,false);
    assert(own_clicks==clicks+1);draw_frame(false,true);
    nui_label_set_text(label,"Changed glyphs");draw_frame(false,true);
    nui_obj_set_style_radius(panel,31,0);draw_frame(false,true);
    nui_obj_set_size(panel,205,135);draw_frame(false,true);
    nui_obj_set_style_bg_grad_color(panel,theme?0x998877:0x667799,0);
    nui_obj_set_style_bg_grad_dir(panel,NUI_GRAD_DIR_HOR,0);draw_frame(false,true);
    nui_obj_set_style_bg_opa(panel,180,0);draw_frame(false,true);
    nui_obj_set_style_bg_opa(panel,255,0);draw_frame(false,true);
    nui_obj_set_style_opa(panel,128,0);draw_frame(false,true);
    nui_obj_set_style_opa(panel,255,0);draw_frame(false,true);
    nui_obj_add_flag(panel,NUI_OBJ_FLAG_HIDDEN);draw_frame(false,true);
    nui_obj_remove_flag(panel,NUI_OBJ_FLAG_HIDDEN);draw_frame(false,true);
    nui_obj_clear_layer_cache(panel);fail_all=true;draw_frame(false,true);fail_all=false;
    nui_obj_clear_layer_cache(panel);draw_frame(false,true);
    nui_obj_add_event_cb(panel,paint_callback,NUI_EVENT_DRAW_MAIN,NULL);nui_obj_invalidate(panel);draw_frame(false,true);
    nui_obj_delete(panel);draw_frame(false,true);
    nui_native_deinit();assert(!n.stats.allocation_bytes&&!layer_stats.bytes&&!layer_stats.pins);
}
static void bind_event(nui_event_t *e){
    (void)e;uint64_t epoch=framebuffer_epoch;uint16_t *current=n.fb;
    assert(!nui_native_bind_framebuffer(current==frame?second_cpu:frame));
    assert(n.fb==current&&framebuffer_epoch==epoch);
}
static void bind_proof(void){
    assert(!nui_native_bind_framebuffer(frame));
    assert(nui_native_init(240,160,frame));configure();
    n.dirty_count=0;nui_area_t one={1,2,3,4},two={100,110,120,130},peek[2];
    nui_native_invalidate(&one);nui_native_invalidate(&two);
    assert(nui_native_get_pending_dirty(NULL,0)==2&&nui_native_get_pending_dirty(peek,1)==2);
    assert(!memcmp(&peek[0],&one,sizeof(one))&&n.dirty_count==2);
    assert(nui_native_get_pending_dirty(peek,2)==2&&!memcmp(&peek[1],&two,sizeof(two)));
    nui_obj_invalidate(n.root);
    nui_obj_t *o=box_at(n.root,0,0,220,150,0x557799,255,0);assert(nui_obj_set_layer_cache(o,true));
    nui_obj_add_event_cb(o,bind_event,NUI_EVENT_CLICKED,NULL);event_send(o,NUI_EVENT_CLICKED,NULL,NULL);
    uint64_t epoch=framebuffer_epoch;assert(!nui_native_bind_framebuffer(NULL));assert(framebuffer_epoch==epoch);
    n.rendering=true;assert(!nui_native_bind_framebuffer(second_cpu));n.rendering=false;
    assert(nui_native_bind_framebuffer(frame)&&framebuffer_epoch==epoch);
    size_t dirty=n.dirty_count,allocated=n.stats.allocation_bytes;
    fail_all=true;assert(nui_native_bind_framebuffer(second_cpu));fail_all=false;
    assert(framebuffer_epoch==epoch+1&&n.dirty_count==dirty&&n.stats.allocation_bytes==allocated);
    assert(nui_native_bind_framebuffer(frame));nui_native_render();assert(view_count);
    nui_surface_view_t held=views[0];assert(nui_surface_view_materialize(&held));
    uint16_t *captured=n.fb;uint16_t sample=captured[30*240+30];
    surface_storage *st=view_lease(&held)->storage;uint64_t id=st->id,gen=st->portrait_generation;
    nui_area_t actual[MAX_DIRTY];size_t count=n.rendered_count;memcpy(actual,n.rendered,count*sizeof(*actual));
    assert(nui_native_bind_framebuffer(second_cpu));
    assert(!nui_surface_view_materialize(&held)&&st->id==id&&st->portrait_generation==gen&&st->pins);
    assert(n.rendered_count==count&&!memcmp(actual,n.rendered,count*sizeof(*actual))&&captured[30*240+30]==sample);
    /* A second frame can lease the same immutable backing while the first
     * plan retains its own CPU target and view. */
    nui_surface_view_t old[8];unsigned old_count=view_count;memcpy(old,views,view_count*sizeof(*views));view_count=0;
    nui_obj_set_pos(o,1,0);nui_native_render();assert(view_count&&views[0].storage_id==held.storage_id);
    assert(views[0].portrait_generation==held.portrait_generation&&nui_surface_view_materialize(&views[0]));
    assert(!nui_surface_view_materialize(&held));
    nui_obj_clear_layer_cache(o);nui_obj_delete(o);assert(!st->owned&&st->pins);
    nui_native_deinit();assert(!nui_surface_view_materialize(&held));
    for(unsigned i=0;i<old_count;i++)nui_surface_view_release(&old[i]);
    for(unsigned i=0;i<view_count;i++)nui_surface_view_release(&views[i]);
    view_count=0;
    assert(!layer_stats.pins&&!layer_stats.bytes&&!n.stats.allocation_bytes);
    /* init(NULL) ownership must never transfer to/free a borrowed slot. */
    assert(nui_native_init(240,160,NULL));uint16_t *owned=n.fb;allocated=n.stats.allocation_bytes;
    assert(nui_native_bind_framebuffer(second_cpu)&&n.owned_fb==owned&&n.stats.allocation_bytes==allocated);
    assert(nui_native_bind_framebuffer(owned));nui_native_deinit();assert(!n.stats.allocation_bytes);
    printf("PASS framebuffer bind: no allocation/dirty/cache clear, rendering/event guards, same-pointer epoch, two pinned frames, late materialize/release, owned cleanup\n");
}
static uint64_t phase_clock_ticks;
static uint64_t phase_clock(void){return ++phase_clock_ticks;}
static void phase_proof(void){
    assert(nui_native_init(240,176,frame));nui_native_set_surface_defer_cb(NULL,NULL);
    nui_native_set_clock_cb(phase_clock);phase_clock_ticks=0;
    nui_obj_t *o=box_at(n.root,8,8,200,140,0x123456,255,16);assert(nui_obj_set_layer_cache(o,true));
    nui_native_profile_reset();assert(nui_native_render());
    nui_native_stats_t stats;nui_native_profile_snapshot(&stats);
    assert(stats.align_us==1&&stats.logical_render_us>0&&stats.gc_us==1&&stats.cache_cpu_blit_us>0);
    uint64_t before=stats.callback_audit_us;nui_layer_t query={.clip={0,0,239,175},.opa=255};
    assert(!callbacks_reachable(n.root,query));nui_native_profile_snapshot(&stats);assert(stats.callback_audit_us==before+1);
    nui_obj_set_pos(o,9,8);assert(nui_native_render());nui_native_profile_snapshot(&stats);
    assert(stats.align_us==2&&stats.gc_us==2&&stats.callback_audit_us>before+1);
    size_t allocated=stats.allocation_bytes;nui_native_profile_reset();nui_native_profile_snapshot(&stats);
    assert(!stats.align_us&&!stats.logical_render_us&&!stats.gc_us&&!stats.callback_audit_us&&!stats.cache_cpu_blit_us);
    assert(stats.allocation_bytes==allocated);nui_native_set_clock_cb(clock_cb);
    nui_native_deinit();assert(!n.stats.allocation_bytes&&!layer_stats.bytes);
    printf("PASS coarse renderer phases: cumulative snapshot/reset, external audit clock pair, no FINE dependency\n");
}
static unsigned tick_timer_calls,tick_press_calls;
static void tick_timer(nui_timer_t *t){tick_timer_calls++;nui_timer_delete(t);}
static void tick_press(nui_event_t *e){(void)e;tick_press_calls++;}
static void tick_proof(void){
    const uint32_t old_values[]={0,79,80,159,399,UINT32_MAX-3,UINT32_MAX};
    const uint32_t deltas[]={0,1,7,79,80,160};unsigned cases=0;
    for(unsigned i=0;i<7;i++)for(unsigned j=0;j<6;j++){
        assert(nui_native_init(240,176,frame));
        nui_obj_t *scroll=nui_label_create(n.root);nui_obj_set_pos(scroll,10,10);nui_obj_set_size(scroll,12,22);
        nui_label_set_text(scroll,"A long scrolling label");nui_label_set_long_mode(scroll,NUI_LABEL_LONG_SCROLL_CIRCULAR);
        nui_obj_t *caret=nui_textarea_create(n.root);nui_obj_set_pos(caret,100,90);nui_obj_set_size(caret,80,40);
        nui_obj_t *button=box_at(n.root,10,120,60,30,0x223344,255,0);
        nui_obj_add_event_cb(button,tick_press,NUI_EVENT_PRESSING,NULL);
        uint32_t old=old_values[i],delta=deltas[j];n.ticks=old;scroll->text_epoch=old-2000;
        n.focus=caret;caret->blink_epoch=old-399;n.capture=button;n.input.pressed=true;
        nui_timer_t *timer=nui_timer_create(tick_timer,7,NULL);assert(timer);
        timer->due=delta?old+7:old-1;bool due=(int32_t)((old+delta)-timer->due)>=0;
        tick_timer_calls=tick_press_calls=0;n.dirty_count=0;nui_native_tick(delta);
        assert(n.ticks==(uint32_t)(old+delta)&&tick_timer_calls==(unsigned)due&&tick_press_calls==1);
        if(due)assert(!n.timers);
        nui_area_t actual[MAX_DIRTY];size_t count=n.dirty_count;memcpy(actual,n.dirty,count*sizeof(*actual));
        n.dirty_count=0;
        if((old-caret->blink_epoch)/400!=(n.ticks-caret->blink_epoch)/400)nui_obj_invalidate(caret);
        tick_walk(n.root,old); /* Original unconditional traversal, exact dirty oracle. */
        assert(count==n.dirty_count&&!memcmp(actual,n.dirty,count*sizeof(*actual)));
        nui_native_deinit();assert(!n.stats.allocation_bytes&&!layer_stats.bytes);cases++;
    }
    printf("PASS tick guard: %u old/new/80ms/wrap/ms0 cases; identical scroll/caret dirty, timer and pressing callbacks\n",cases);
}
static void span_proof(void){
    const int lengths[]={0,1,2,127,128,129,255,256,257,511};
    const int starts[]={-5,0,1,7,513};const unsigned alphas[]={0,1,128,254,255},opas[]={0,128,255};
    const uint16_t colors[]={0x0000,0xffff,0x1234,0x07e0,0xcafe};unsigned cases=0;
    for(unsigned offset=0;offset<2;offset++){
        assert(nui_native_init(520,3,frame+offset));
        for(unsigned c=0;c<5;c++)for(unsigned a=0;a<5;a++)for(unsigned o=0;o<3;o++)
        for(unsigned len=0;len<10;len++)for(unsigned x=0;x<5;x++)for(unsigned clip=0;clip<2;clip++)for(int y=0;y<3;y++){
            for(unsigned i=0;i<1564;i++)frame[i]=proof[i]=(uint16_t)(i*3181u+cases*7919u);
            nui_layer_t layer={.clip=clip?(nui_area_t){5,1,501,1}:(nui_area_t){0,0,519,2},.opa=(uint8_t)opas[o]};
            int from=starts[x],to=from+lengths[len]-1;unsigned alpha=alphas[a]*opas[o]/255;
            if(y>=layer.clip.y1&&y<=layer.clip.y2)for(int px=MAX(from,layer.clip.x1);px<=MIN(to,layer.clip.x2);px++){
                uint16_t *dst=&proof[offset+y*520+px],src=colors[c];
                if(alpha>=255)*dst=src;
                else if(alpha&&*dst!=src){unsigned mix=(alpha+4)>>3,inv=32-mix;
                    *dst=(uint16_t)((((src&0xf81fu)*mix+(*dst&0xf81fu)*inv)>>5&0xf81fu)|
                        (((src&0x07e0u)*mix+(*dst&0x07e0u)*inv)>>5&0x07e0u));}
            }
            span(&layer,from,to,y,colors[c],alphas[a]);assert(!memcmp(frame,proof,1564*sizeof(*frame)));cases++;
        }
        nui_native_deinit();assert(!n.stats.allocation_bytes);
    }
    printf("PASS opaque/translucent spans: %u scalar RGB565 cases, 127/128 threshold, clips, 2-byte offsets and tails\n",cases);
}
static unsigned audit_events;
static void audit_noop(nui_event_t *e){(void)e;audit_events++;}
static void audit_delete(nui_event_t *e){
    nui_obj_t *o=nui_event_get_target(e);nui_obj_delete(o);
    nui_obj_add_event_cb(o,audit_noop,NUI_EVENT_DRAW_POST,NULL); /* deleted during event: ignored */
}
static void audit_lifecycle(void){
    assert(nui_native_init(240,176,frame));configure();
    nui_obj_t *parent=box_at(n.root,0,0,240,176,0x345678,255,0);
    nui_obj_t *leaf=box_at(parent,20,20,80,80,0x556677,255,0);
    nui_obj_add_event_cb(leaf,audit_noop,NUI_EVENT_CLICKED,NULL);assert(!audit_count(n.root));audit_compare();
    fail_all=true;nui_obj_add_event_cb(leaf,audit_noop,NUI_EVENT_DRAW_MAIN,NULL);fail_all=false;
    assert(!audit_count(n.root));
    nui_obj_add_event_cb(leaf,audit_noop,NUI_EVENT_DRAW_TASK_ADDED,NULL);assert(audit_count(n.root)==1);audit_compare();
    nui_layer_t query={.clip={0,0,239,175},.opa=255};assert(!callbacks_reachable(n.root,query));
    nui_obj_add_flag(leaf,NUI_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);audit_compare();assert(callbacks_reachable(n.root,query));
    nui_obj_add_flag(leaf,NUI_OBJ_FLAG_HIDDEN);audit_compare();assert(!callbacks_reachable(n.root,query));
    nui_obj_remove_flag(leaf,NUI_OBJ_FLAG_HIDDEN);audit_compare();
    nui_obj_add_event_cb(leaf,audit_noop,NUI_EVENT_DRAW_MAIN,NULL);
    nui_obj_add_event_cb(parent,audit_noop,NUI_EVENT_DRAW_POST,NULL);assert(audit_count(n.root)==3);audit_compare();
    nui_obj_t *sibling=box_at(n.root,4,4,20,20,0,0,0);
    nui_obj_add_event_cb(sibling,audit_noop,NUI_EVENT_ALL,NULL);assert(audit_count(n.root)==4);audit_compare();
    nui_obj_move_foreground(parent);assert(audit_count(n.root)==4);
    nui_obj_delete(parent);assert(audit_count(n.root)==1);audit_compare();
    nui_obj_add_event_cb(sibling,audit_delete,NUI_EVENT_CLICKED,NULL);event_send(sibling,NUI_EVENT_CLICKED,NULL,NULL);
    assert(!audit_count(n.root));audit_compare();
    nui_native_deinit();assert(!n.stats.allocation_bytes&&!layer_stats.bytes);
    printf("PASS callback refcounts: failed/late add, TASK flag, hidden, MAIN/POST/ALL, subtree/event deletion\n");
}
static uint64_t checksum(const uint16_t *p,size_t count){uint64_t h=0;for(size_t i=0;i<count;i++)h=h*33+p[i];return h;}
static void lifetimes(void){
    gpu_mode=true;assert(nui_native_init(240,160,frame));configure();
    nui_obj_t *o=box_at(nui_screen_active(),0,0,1094,1490,0x556677,255,0);
    assert(nui_obj_set_layer_cache(o,true));nui_native_render();assert(view_count==1&&layer_stats.pins==1);
    nui_surface_view_t held=views[0];surface_lease *lease=view_lease(&held);assert(lease);
    surface_storage *storage=lease->storage;
    uint64_t old_id=held.storage_id,old_generation=held.portrait_generation;
    assert(old_id==storage->id&&old_generation==storage->portrait_generation);
    uint64_t a=checksum(held.landscape,(size_t)held.logical_width*held.logical_height);
    uint64_t b=checksum(held.portrait,(size_t)held.portrait_stride*storage->ph);
    nui_obj_set_style_bg_color(o,0x998877,0);nui_native_set_surface_defer_cb(NULL,NULL);nui_native_render();
    assert(checksum(held.landscape,(size_t)held.logical_width*held.logical_height)==a);
    assert(checksum(held.portrait,(size_t)held.portrait_stride*storage->ph)==b);
    assert(storage->id==old_id&&storage->portrait_generation==old_generation);
    nui_layer_cache_reset_stats();assert(layer_stats.pins==1);
    nui_obj_clear_layer_cache(o);nui_obj_delete(o);assert(!storage->owned&&layer_stats.bytes>0);
    assert(nui_surface_view_materialize(&held));
    nui_native_deinit();assert(!nui_surface_view_materialize(&held));
    assert(layer_stats.pins==1&&layer_stats.bytes>0);nui_surface_view_release(&held);views[0].lease=NULL;view_count=0;
    assert(!layer_stats.pins&&!layer_stats.bytes&&!n.stats.allocation_bytes);nui_surface_view_release(&held);
    /* Reject callback releases pins; OOM portrait build keeps full CPU path. */
    assert(nui_native_init(240,160,frame));configure();o=box_at(nui_screen_active(),0,0,1094,1490,0x446688,255,0);
    assert(nui_obj_set_layer_cache(o,true));fail_surface=true;nui_native_render();fail_surface=false;
    assert(!view_count&&!layer_stats.pins);
    nui_obj_clear_layer_cache(o);nui_native_set_surface_defer_cb(NULL,NULL);nui_native_render(); /* landscape only */
    size_t landscape_payload=layer_stats.bytes;assert(landscape_payload==payload64(1094*1490*2));
    nui_native_set_surface_defer_cb(enqueue,NULL);fail_surface=true;nui_obj_set_pos(o,1,0);nui_native_render();fail_surface=false;
    assert(!view_count&&!layer_stats.pins&&layer_stats.bytes==landscape_payload); /* portrait OOM */
    nui_obj_clear_layer_cache(o);reject_views=true;nui_native_render();reject_views=false;
    assert(!view_count&&!layer_stats.pins);
    nui_obj_set_pos(o,0,0);nui_native_render();assert(view_count);
    assert(views[0].storage_id!=old_id&&views[0].portrait_generation);
    nui_surface_view_t latest=views[0];compose(true);
    nui_obj_set_pos(o,1,0);nui_native_render();assert(view_count);
    assert(views[0].storage_id==latest.storage_id&&views[0].portrait_generation==latest.portrait_generation);compose(true);
    nui_obj_set_style_bg_color(o,0x777799,0);nui_native_render();assert(view_count);
    assert(views[0].storage_id==latest.storage_id&&views[0].portrait_generation>latest.portrait_generation);compose(true);
    nui_native_deinit();assert(!n.stats.allocation_bytes&&!layer_stats.bytes);
    printf("PASS immutable pins, mutation fallback, clear/delete/deinit+late release, failure/reject cleanup\n");
}
int main(int argc,char **argv){
    assert(argc==3);setbuf(stdout,NULL);double_targets=!strncmp(argv[1],"double",6);gpu_mode=strcmp(argv[1],"reference")!=0;requested_surface_alignment=strchr(argv[1],'1')?1:16;output=fopen(argv[2],"wb");assert(output);
    for(unsigned theme=0;theme<2;theme++){actual_settings(theme);overlays(theme);own_rounded(theme);odd_surface(theme);}
    fclose(output);output=NULL;
    surface_alignment_proof();phase_proof();tick_proof();span_proof();audit_lifecycle();if(gpu_mode){bind_proof();lifetimes();}
    nui_native_set_surface_defer_cb(NULL,NULL);nui_native_set_dirty_alignment(1);
    printf("PASS %u full portrait frames: independent full-scene CPU vs persistent exact-ROI GPU composition, overlays and diagnostics\n",composed_frames);
    return 0;
}
