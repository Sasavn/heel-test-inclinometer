#include "tab5_relative.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static tab5_measure_snapshot_t sample(void){
 tab5_measure_snapshot_t s={0};
 for(unsigned i=0;i<2;i++)s.sensor[i]=(tab5_measure_sensor_t){.address=i+2,.valid=true,.good=1,
  .timestamp_us=1000000+i*16000,.decoded_x=i?1.25f:.25f,.decoded_y=i?-2.f:-1.f,
  .zero_x=75,.zero_y=-35};return s;
}
int main(void){
 tab5_relative_config_t c;tab5_relative_config_default(&c);assert(sizeof(c)==8&&tab5_relative_config_valid(&c));
 tab5_relative_t state;tab5_relative_init(&state,&c);assert(state.mode==TAB5_MODE_HEEL&&!state.offset_valid);
 tab5_measure_snapshot_t s=sample();tab5_relative_result_t r;
 tab5_relative_evaluate(&state,&s,1020000,&r);assert(!r.ready&&(r.quality&TAB5_PAIR_AXES)&&isnan(r.live[0]));
 c.axes_confirmed=1;c.startup_mode=TAB5_MODE_LEVEL;tab5_relative_init(&state,&c);
 tab5_relative_evaluate(&state,&s,1020000,&r);assert(r.ready&&r.live[0]==1&&r.live[1]==-1&&r.skew_us==16000);
 assert(r.source[0][0]==.25f&&r.sequence[1]==1&&r.age_us[0]==20000);
 tab5_relative_observe(&state,&s,1020000,&r);
 assert(tab5_relative_capture(&state,&s,1020000));tab5_relative_evaluate(&state,&s,1020000,&r);
 assert(r.offset_valid&&r.relative[0]==0&&r.live[0]==1);
 s.sensor[1].decoded_x+=.5f;tab5_relative_evaluate(&state,&s,1020000,&r);assert(r.relative[0]==.5f);
 tab5_relative_init(&state,&c);assert(!state.offset_valid&&state.mode==TAB5_MODE_LEVEL);
 tab5_relative_boundary(&state,&s);tab5_relative_evaluate(&state,&s,1020000,&r);assert(r.quality&TAB5_PAIR_WARMUP);
 s.sensor[0].good++;tab5_relative_evaluate(&state,&s,1020000,&r);assert(r.quality&TAB5_PAIR_WARMUP);
 s.sensor[1].good++;tab5_relative_evaluate(&state,&s,1020000,&r);assert(r.ready);
 s.sensor[0].fail_streak=1;tab5_relative_evaluate(&state,&s,1020000,&r);assert((r.quality&TAB5_PAIR_ERROR)&&!tab5_relative_capture(&state,&s,1020000));
 s.sensor[0].fail_streak=0;s.sensor[0].last_error=TAB5_MEASURE_TIMEOUT;
 tab5_relative_evaluate(&state,&s,1020000,&r);assert(r.quality&TAB5_PAIR_ERROR);s.sensor[0].last_error=TAB5_MEASURE_OK;
 tab5_relative_evaluate(&state,&s,1600001,&r);assert(r.quality&TAB5_PAIR_STALE);
 s.sensor[0].timestamp_us=1200000;tab5_relative_evaluate(&state,&s,1100000,&r);assert(r.quality&TAB5_PAIR_STALE);
 s=sample();s.sensor[0].good=s.sensor[1].good=3;s.sensor[1].timestamp_us=1050000;
 tab5_relative_evaluate(&state,&s,1050000,&r);assert(r.ready&&r.skew_us==50000);
 s.sensor[1].timestamp_us++;tab5_relative_evaluate(&state,&s,1050001,&r);assert(r.quality&TAB5_PAIR_SKEW);
 s=sample();s.sensor[0].good=s.sensor[1].good=3;s.sensor[0].decoded_x=NAN;
 tab5_relative_evaluate(&state,&s,1020000,&r);assert(r.quality&TAB5_PAIR_RANGE);
 s.sensor[0].decoded_x=90.01f;tab5_relative_evaluate(&state,&s,1020000,&r);assert(r.quality&TAB5_PAIR_RANGE);
 s.sensor[0].decoded_x=90;s.sensor[1].decoded_x=-90;tab5_relative_evaluate(&state,&s,1020000,&r);assert(r.ready&&r.live[0]==-180);
 c.reference_index=1;c.object_index=0;tab5_relative_init(&state,&c);
 tab5_relative_evaluate(&state,&s,1020000,&r);assert(r.ready&&r.live[0]==180);
 s.sensor[1].valid=false;tab5_relative_evaluate(&state,&s,1020000,&r);assert(r.quality&TAB5_PAIR_MISSING);
 c.object_index=c.reference_index;assert(!tab5_relative_config_valid(&c));tab5_relative_init(&state,&c);assert(state.mode==TAB5_MODE_HEEL);
 tab5_relative_config_default(&c);c.reserved[0]=1;assert(!tab5_relative_config_valid(&c));
 tab5_relative_evaluate(NULL,NULL,0,&r);assert(!r.ready&&isnan(r.live[0]));
 puts("PASS relative core: axes, real roles, raw vs zero, live vs REL, reboot, boundary, both-new, missing, fail, age, skew, future, range, corrupt config");
 return 0;
}
