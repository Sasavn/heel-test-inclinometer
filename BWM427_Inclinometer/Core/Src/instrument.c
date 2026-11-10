/* STM32 runtime instrument owner. All calls run in the main superloop. */
#include "app.h"
#ifndef APP_INSTRUMENT_SIM
#include "main.h"
#else
extern uint32_t HAL_GetTick(void);
#endif
#include "settings.h"
#include <string.h>
relative_t g_relative;
static relative_snapshot_t inputs;
static uint32_t last_ms;
static uint64_t extended_ms;
__attribute__((weak)) bool app_transport_busy(void) {return false;}
uint64_t app_relative_now(void) {
    uint32_t now=HAL_GetTick();extended_ms+=(uint32_t)(now-last_ms);last_ms=now;
    return extended_ms*1000;
}
void app_relative_init(uint16_t flags) {
    relative_config_t cfg;relative_config_default(&cfg);
    g_app.level_units=0;
    if((flags&0xfff0u)==0xa600u){
        cfg.startup_mode=flags&1;cfg.reference_index=(flags>>1)&1;cfg.object_index=1-cfg.reference_index;
        cfg.axes_confirmed=(flags>>2)&1;g_app.level_units=(flags>>3)&1;
    }
    memset(&inputs,0,sizeof(inputs));last_ms=HAL_GetTick();extended_ms=last_ms;
    relative_init(&g_relative,&cfg);
}
uint16_t app_instrument_flags(void) {
    return 0xa600u|g_relative.mode|(g_relative.config.reference_index<<1)|
        (g_relative.config.axes_confirmed<<2)|(g_app.level_units<<3);
}
bool app_instrument_busy(void) {
    return g_app.rec_switch_on||g_app.sd_state==SD_RECORDING||g_app.svc_state==SVC_BUSY||app_transport_busy();
}
static void changed(void) {
    relative_boundary(&g_relative,&inputs);settings_touch(HAL_GetTick());
}
bool app_set_instrument(uint8_t mode) {
    if(mode>REL_MODE_LEVEL||app_instrument_busy())return false;
    g_relative.mode=(relative_mode_t)mode;g_relative.config.startup_mode=mode;changed();return true;
}
bool app_relative_roles(unsigned ref,unsigned obj) {
    if(ref<2||ref>3||obj<2||obj>3||ref==obj||app_instrument_busy())return false;
    g_relative.config.reference_index=ref-2;g_relative.config.object_index=obj-2;
    g_relative.config.axes_confirmed=0;changed();return true;
}
bool app_relative_axes(bool confirmed) {
    if(app_instrument_busy())return false;
    g_relative.config.axes_confirmed=confirmed;changed();return true;
}
bool app_relative_zero(bool reset) {
    if(app_instrument_busy())return false;
    if(reset){relative_boundary(&g_relative,&inputs);return true;}
    return relative_capture(&g_relative,&inputs,app_relative_now());
}
void app_relative_read(relative_result_t *out) {relative_read(&g_relative,&inputs,app_relative_now(),out);}
void app_relative_record_boundary(void) {
    for(unsigned i=0;i<2;i++)g_relative.floor_good[i]=inputs.sensor[i].good;
    relative_pair_reset(&g_relative);
}
void app_relative_observe(unsigned i,bool success,uint32_t rx_ms) {
    if(i>=2)return;
    uint64_t now=app_relative_now();relative_sensor_t *p=&inputs.sensor[i];
    const app_sensor_t *head=&g_app.sensor[i];p->address=head->addr;
    if(success){
        p->valid=true;p->fail_streak=0;p->last_error=0;p->good++;
        p->timestamp_us=now-(uint64_t)(uint32_t)(last_ms-rx_ms)*1000;
        p->decoded_x=head->raw_x;p->decoded_y=head->raw_y;
    }else{p->last_error=1;if(p->fail_streak<255)p->fail_streak++;}
    relative_result_t out;relative_observe(&g_relative,&inputs,now,&out);
}
void app_relative_units(bool mm) {g_app.level_units=mm?1:0;settings_touch(HAL_GetTick());}
