/* Real renderer: release mode must not read the clock per primitive or write
 * disabled fine counters. Coarse frame and memory accounting remain valid. */
#include "native/native_ui.h"
#include <assert.h>
#include <stdio.h>
static uint16_t pixels[128*96];
static uint64_t clock_reads;
static uint64_t fake_clock(void){return ++clock_reads;}
int main(void){
    assert(nui_native_init(128,96,pixels));nui_native_set_clock_cb(fake_clock);
    nui_obj_t *box=nui_obj_create(nui_screen_active());
    nui_obj_set_pos(box,10,12);nui_obj_set_size(box,70,45);
    nui_obj_set_style_bg_color(box,nui_color_hex(0x345678),0);
    nui_obj_set_style_bg_opa(box,255,0);nui_obj_set_style_radius(box,8,0);
    nui_obj_set_style_border_width(box,1,0);
    nui_obj_set_style_shadow_width(box,8,0);nui_obj_set_style_shadow_opa(box,70,0);
    nui_native_profile_reset();clock_reads=0;
    assert(nui_native_render()>0);
    nui_native_stats_t stats;nui_native_profile_snapshot(&stats);
    assert(stats.frames==1&&stats.dirty_pixels>0&&stats.allocation_bytes>0);
    uint64_t calls=0,ink=0,time=0;
    for(unsigned i=0;i<NUI_PRIM_COUNT;i++){calls+=stats.primitive[i].calls;ink+=stats.primitive[i].pixels;time+=stats.primitive[i].us;}
#if NUI_PROFILE_FINE_COUNTERS
    assert(calls>0&&ink>0);
#else
    assert(calls==0&&ink==0&&stats.style_cache_hits==0&&stats.style_cache_misses==0);
    assert(stats.layout_cache_hits==0&&stats.layout_cache_misses==0);
#endif
#if NUI_PROFILE_PRIMITIVE_TIMING
    assert(clock_reads>2&&time>0);
#else
    assert(clock_reads==2&&time==0); /* One coarse start/end, no draw clocks. */
#endif
    printf("native profile flags: fine=%d timing=%d clock_reads=%llu frames=%llu calls=%llu PASS\n",
           NUI_PROFILE_FINE_COUNTERS,NUI_PROFILE_PRIMITIVE_TIMING,
           (unsigned long long)clock_reads,(unsigned long long)stats.frames,(unsigned long long)calls);
    nui_native_deinit();nui_native_profile_snapshot(&stats);assert(stats.allocation_bytes==0);
    return 0;
}
