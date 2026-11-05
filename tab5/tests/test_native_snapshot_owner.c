#include "../main/tab5_ui.c"
#include <assert.h>
static uint16_t framebuffer[1280*720],proof[1280*720];
static tab5_ui_state_t input,original;
int main(void){
    assert(nui_native_init(1280,720,framebuffer));
    tab5_ui_init(NULL,NULL);
    assert(u.root&&u.state.light_theme&&u.state.measurement.config.frequency_hz);
    input=u.state;input.live_unfiltered=true;input.now_known=true;input.now_us=1000000;
    input.measurement.sensor[0]=(tab5_measure_sensor_t){.address=2,.valid=true,.good=1,
        .decoded_x=1.25f,.decoded_y=-2.5f,.zero_x=.25f,.zero_y=-.5f,.x=77,.y=88};
    input.workspace.known=true;input.workspace.count=TAB5_WORKSPACE_CAPACITY;
    for(unsigned i=0;i<TAB5_WORKSPACE_CAPACITY;i++)input.workspace.history[i]=
        (tab5_workspace_point_t){.timestamp_us=1000+i,.value={(float)i,-(float)i},.gap=i==63};
    for(unsigned theme=0;theme<2;theme++){
        input.light_theme=theme;original=input;
        tab5_ui_update(&input);
        assert(!memcmp(&input,&original,sizeof(input)));
        assert(u.state.measurement.sensor[0].x==1&&u.state.measurement.sensor[0].y==-2);
        assert(!memcmp(&u.state.workspace,&input.workspace,sizeof(input.workspace)));
        /* Init uses this same owned input. It must not clear itself, double
         * subtract zero, or lose the bounded history during a self-copy. */
        tab5_ui_update(&u.state);
        assert(u.state.measurement.sensor[0].x==1&&u.state.measurement.sensor[0].y==-2);
        assert(!memcmp(&u.state.workspace,&input.workspace,sizeof(input.workspace)));
        nui_native_render();memcpy(proof,framebuffer,sizeof(proof));
        nui_obj_invalidate(u.root);nui_native_render();assert(!memcmp(proof,framebuffer,sizeof(proof)));
        assert(!memcmp(&input,&original,sizeof(input)));
    }
    tab5_ui_destroy();nui_native_deinit();
    puts("PASS production UI snapshot ownership: boot init, unchanged borrowed source, owned decoded-minus-zero presentation, self-alias/idempotence, full history, both themes, partial/full parity");
}
