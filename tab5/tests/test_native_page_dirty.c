/* Actual page transitions: bounded dirty drawing must equal a full repaint. */
#define main unused_full_suite_main
#include "test_native_ui.c"
#undef main
static uint16_t incremental[1280*720];
int main(void){
    uint64_t sum=0;unsigned count=0;
    for(unsigned theme=0;theme<2;theme++){
        assert(nui_native_init(1280,720,framebuffer));
        nui_native_set_flush_cb(flush,NULL);rgb565_mode=true;
        tab5_ui_init(action,NULL);
        tab5_ui_state_t s={.light_theme=theme,.now_known=true,.now_us=900000000};
        tab5_measure_config_default(&s.measurement.config);
        for(unsigned j=0;j<2;j++)s.measurement.sensor[j]=(tab5_measure_sensor_t){
            .valid=true,.good=20,.timestamp_us=s.now_us,
            .decoded_x=1.234f,.decoded_y=-2.345f,.x=1.234f,.y=-2.345f,
            .roll_x=.1f,.roll_y=.2f};
        tab5_ui_update(&s);pump();
        for(unsigned round=0;round<4;round++)for(unsigned page=0;page<5;page++){
            flushed_pixels=0;nav(page);sum+=flushed_pixels;count++;
            memcpy(incremental,framebuffer,sizeof(incremental));
            nui_obj_invalidate(nui_screen_active());nui_native_render();
            if(memcmp(incremental,framebuffer,sizeof(incremental))){
                fprintf(stderr,"DIRTY_COVERAGE FAIL theme=%u round=%u page=%u\n",theme,round,page);
                return 1;
            }
        }
        tab5_ui_destroy();nui_native_deinit();
    }
    printf("PAGE_DIRTY_COVERAGE %u transitions both themes incremental == full redraw; mean flushed pixels=%llu PASS\n",
           count,(unsigned long long)(sum/count));
    return 0;
}
