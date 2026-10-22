/* Production UI lifecycle under open dialogs, timers and deferred event deletes.
 * Runs without a window or device; checks retained allocations, not RSS. */
#include "tab5_ui.h"
#include "native/native_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t frame[1280*720];
static unsigned actions;
static bool has_text(nui_obj_t *o,const char *text){
    if(!nui_obj_is_visible(o))return false;
    if(nui_obj_check_type(o,&nui_label_class)&&!strcmp(nui_label_get_text(o),text))return true;
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++)if(has_text(nui_obj_get_child(o,i),text))return true;
    return false;
}
static void action(const tab5_ui_action_t *a,void *context){(void)a;(void)context;actions++;}
static void tap(int x,int y){
    nui_native_touch(x,y,true);nui_native_tick(40);nui_timer_handler();
    nui_native_touch(x,y,false);nui_native_tick(40);nui_timer_handler();
    nui_native_render();
}
int main(void){
    assert(nui_native_init(1280,720,frame));
    nui_native_stats_t initial,after;
    nui_native_profile_snapshot(&initial);
    for(unsigned cycle=0;cycle<64;cycle++){
        tab5_ui_init(action,NULL);
        tab5_ui_state_t s={.light_theme=(cycle&1)!=0,.now_known=true,.now_us=100000000};
        tab5_measure_config_default(&s.measurement.config);
        tab5_ui_update(&s);nui_native_render();
        tap(52,396); /* settings */
        if(cycle%3==0){
            tap(535,200); /* numeric dialog */
            assert(has_text(nui_screen_active(),"Готово"));
        }else if(cycle%3==1){
            tap(52,484);tap(400,204); /* network text keyboard */
            assert(has_text(nui_screen_active(),"AP: имя сети"));
        }else{
            /* Destroy while the stepper is held and owns a repeat timer. */
            nui_native_touch(635,200,true);nui_native_tick(500);nui_timer_handler();
        }
        tab5_ui_destroy();
        nui_native_touch(635,200,false);nui_native_tick(1000);nui_timer_handler();
        nui_native_render();
        nui_native_profile_snapshot(&after);
        /* Global masks may survive an individual UI destroy for reuse. Every
         * retained heap byte must be accounted for by the bounded cache, never
         * waived as an arbitrary warmup allowance. Per-object caches must die. */
        assert(after.shadow_cache_bytes>=after.shadow_cache_static_bytes);
        assert(after.shadow_cache_bytes<=after.shadow_cache_capacity_bytes);
        size_t retained_masks=after.shadow_cache_bytes-after.shadow_cache_static_bytes;
        if(after.allocation_bytes!=initial.allocation_bytes+retained_masks||
           after.object_cache_bytes!=initial.object_cache_bytes){
            fprintf(stderr,"cycle %u retained %zu bytes/%zu blocks; root=%zu masks=%zu object-cache=%zu expected=%zu\n",cycle,
                    after.allocation_bytes,after.allocation_count,initial.allocation_bytes,retained_masks,
                    after.object_cache_bytes,initial.object_cache_bytes);
            return 1;
        }
        assert(!after.allocation_failures);
    }
    printf("native lifetime: 64 production UI cycles, dialogs/keyboards/held repeat; all retained bytes accounted, masks bounded; actions=%u\n",actions);
    nui_native_deinit();nui_native_profile_snapshot(&after);
    assert(after.allocation_bytes==0&&after.allocation_count==0);
    return 0;
}
