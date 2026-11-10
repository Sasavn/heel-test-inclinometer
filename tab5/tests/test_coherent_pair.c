#include "tab5_relative.h"
#include "tab5_workspace.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#ifdef OLD_PAIR
#define tab5_relative_observe tab5_relative_evaluate
#define tab5_relative_read tab5_relative_evaluate
#endif
static tab5_relative_t state;
static tab5_measure_snapshot_t sample;
static tab5_relative_result_t pair;
static tab5_workspace_t workspace;
static void reset(void){
 tab5_relative_config_t cfg;tab5_relative_config_default(&cfg);cfg.axes_confirmed=1;cfg.startup_mode=TAB5_MODE_LEVEL;
 tab5_relative_init(&state,&cfg);memset(&sample,0,sizeof(sample));tab5_workspace_init(&workspace,NULL);
 for(unsigned i=0;i<2;i++)sample.sensor[i]=(tab5_measure_sensor_t){.address=i+2,.valid=true,.decoded_y=-(float)i};
}
static void feed(unsigned i,uint64_t stamp,float x,uint64_t now){
 sample.sensor[i].timestamp_us=stamp;sample.sensor[i].decoded_x=x;sample.sensor[i].good++;
 sample.sensor[i].valid=true;sample.sensor[i].last_error=TAB5_MEASURE_OK;sample.sensor[i].fail_streak=0;
 tab5_relative_observe(&state,&sample,now,&pair);
}
static void read_pair(uint64_t now){tab5_relative_read(&state,&sample,now,&pair);}
static void first(void){reset();feed(0,1000000,.25f,1000000);C(!pair.ready);feed(1,1033333,1.25f,1046666);C(pair.ready);}
static void video(void){
 first();C(pair.live[0]==1);tab5_workspace_update(&workspace,&state.config,&pair,1046666);
 feed(0,1100000,10,1113333);read_pair(1113333);
 printf("video_partial ready=%u quality=%u seq=%llu/%llu age=%llu/%llu skew=%llu live=%g basis=%g/%g\n",pair.ready,pair.quality,
  (unsigned long long)pair.sequence[0],(unsigned long long)pair.sequence[1],(unsigned long long)pair.age_us[0],
  (unsigned long long)pair.age_us[1],(unsigned long long)pair.skew_us,(double)pair.live[0],(double)pair.source[0][0],(double)pair.source[1][0]);
 C(pair.ready&&pair.quality==0&&pair.live[0]==1);
 C(pair.sequence[0]==1&&pair.sequence[1]==1&&pair.source[0][0]==.25f&&pair.source[1][0]==1.25f);
 C(pair.timestamp_us[0]==1000000&&pair.timestamp_us[1]==1033333&&pair.age_us[0]==113333&&pair.age_us[1]==80000);
 tab5_workspace_update(&workspace,&state.config,&pair,1113333);C(workspace.live.live_pairs==1&&workspace.live.live_gaps==0);
 feed(1,1133333,11,1146666);read_pair(1146666);C(pair.ready&&pair.sequence[0]==2&&pair.sequence[1]==2&&pair.source[0][0]==10&&pair.source[1][0]==11);
 tab5_workspace_update(&workspace,&state.config,&pair,1146666);tab5_workspace_update(&workspace,&state.config,&pair,1146666);
 C(workspace.live.live_pairs==2&&workspace.live.live_gaps==0&&workspace.live.count==2);
}
#ifndef OLD_PAIR
static void expiry_and_faults(void){
 first();read_pair(1500000);C(pair.ready&&pair.age_us[0]==500000);
 read_pair(1500001);C(!pair.ready&&(pair.quality&TAB5_PAIR_STALE));
 feed(0,1510000,2,1510000);C(!pair.ready);read_pair(1511000);C(!pair.ready);
 feed(1,1543333,3,1543333);C(pair.ready);
 first();feed(0,1100000,10,1113333);read_pair(1500001);C(!pair.ready&&(pair.quality&TAB5_PAIR_STALE));
 feed(1,1533333,11,1533333);C(!pair.ready); /* A predates the true expiry boundary. */
 feed(0,1550000,10,1550000);C(pair.ready);
 first();sample.sensor[0].fail_streak=1;sample.sensor[0].last_error=TAB5_MEASURE_TIMEOUT;
 read_pair(1050000);C(!pair.ready&&(pair.quality&TAB5_PAIR_ERROR));
 sample.sensor[0].fail_streak=0;sample.sensor[0].last_error=TAB5_MEASURE_OK;
 read_pair(1050001);C(!pair.ready);feed(0,1100000,2,1100000);C(!pair.ready);feed(1,1133333,3,1133333);C(pair.ready);
 first();sample.sensor[1].valid=false;read_pair(1046667);C(!pair.ready&&(pair.quality&TAB5_PAIR_MISSING));
 sample.sensor[1].valid=true;read_pair(1046668);C(!pair.ready);
 first();feed(0,1100000,91,1100000);C(!pair.ready&&(pair.quality&TAB5_PAIR_RANGE));
 feed(0,1200000,2,1200000);C(!pair.ready);feed(1,1233333,3,1233333);C(pair.ready);
 first();feed(0,1200000,2,1100000);C(!pair.ready&&(pair.quality&TAB5_PAIR_STALE));
 read_pair(1200001);C(!pair.ready);feed(1,1233333,3,1233333);C(!pair.ready);feed(0,1250000,2,1250000);C(pair.ready);
 first();feed(0,1100000,2,1100000);feed(1,1150000,3,1150000);C(pair.ready&&pair.skew_us==50000);
 feed(0,1200000,4,1200000);feed(1,1250001,5,1250001);C(!pair.ready&&pair.quality==TAB5_PAIR_SKEW&&pair.sequence[0]==3&&pair.sequence[1]==3);
 feed(0,1300000,6,1300000);C(!pair.ready&&pair.quality==TAB5_PAIR_SKEW);feed(1,1333333,7,1333333);C(pair.ready);
}
static void boundaries(void){
 first();feed(0,1100000,10,1113333);C(tab5_relative_capture(&state,&sample,1113333));
 C(state.offset[0]==1);read_pair(1113333);C(pair.ready&&pair.relative[0]==0&&pair.source[0][0]==.25f);
 tab5_relative_boundary(&state,&sample);read_pair(1113333);C(!pair.ready&&(pair.quality&TAB5_PAIR_WARMUP));
 feed(1,1133333,11,1133333);C(!pair.ready);feed(0,1150000,10,1150000);C(pair.ready&&!pair.offset_valid);
 state.mode=TAB5_MODE_HEEL;tab5_relative_boundary(&state,&sample);read_pair(1150001);C(!pair.ready);
 state.mode=TAB5_MODE_LEVEL;tab5_relative_boundary(&state,&sample);feed(0,1200000,2,1200000);C(!pair.ready);feed(1,1233333,3,1233333);C(pair.ready);
 state.config.reference_index=1;state.config.object_index=0;tab5_relative_boundary(&state,&sample);
 feed(0,1300000,4,1300000);C(!pair.ready);feed(1,1333333,5,1333333);C(pair.ready&&pair.live[0]==-1);
 state.config.axes_confirmed=0;read_pair(1333334);C(!pair.ready&&(pair.quality&TAB5_PAIR_AXES));
 state.config.axes_confirmed=1;tab5_relative_boundary(&state,&sample);read_pair(1333335);C(!pair.ready);
 /* Healthy current snapshot is not an acquisition feed, even both-new. */
 first();sample.sensor[0].good++;sample.sensor[1].good++;sample.sensor[0].timestamp_us=1100000;sample.sensor[1].timestamp_us=1133333;
 sample.sensor[0].decoded_x=10;sample.sensor[1].decoded_x=12;read_pair(1146666);C(pair.sequence[0]==1&&pair.live[0]==1&&pair.source[0][0]==.25f);
 tab5_relative_pair_reset(&state);read_pair(1146666);C(!pair.ready); /* Session copy cannot leak pre-session publication. */
}
static void real_startup(void){
 static tab5_measure_core_t core;tab5_measure_core_init(&core,NULL,0);
 reset();tab5_relative_read(&state,&core.state,1000000,&pair);C(!pair.ready);
 const uint16_t a[2]={10025,10000},b[2]={10125,10000};
 C(tab5_measure_core_sample(&core,0,a,1000000));
 tab5_relative_observe(&state,&core.state,1000000,&pair);C(!pair.ready);
 C(tab5_measure_core_sample(&core,1,b,1033333));
 tab5_relative_observe(&state,&core.state,1033333,&pair);C(pair.ready&&pair.sequence[0]==1&&pair.sequence[1]==1);
 for(unsigned n=1;n<5;n++){
  C(tab5_measure_core_sample(&core,0,a,1000000+n*100000));
  tab5_relative_observe(&state,&core.state,1000000+n*100000,&pair);
  tab5_relative_read(&state,&core.state,1013333+n*100000,&pair);
  C(pair.ready&&pair.sequence[0]==n&&pair.sequence[1]==n);
  C(tab5_measure_core_sample(&core,1,b,1033333+n*100000));
  tab5_relative_observe(&state,&core.state,1033333+n*100000,&pair);
 }
}
#endif
int main(void){video();
#ifndef OLD_PAIR
 expiry_and_faults();boundaries();real_startup();
#endif
 printf("PASS coherent publication %u checks: original basis, exact bounds, faults/recovery, completed bad pair, boundaries, history dedup\n",checks);return 0;
}
