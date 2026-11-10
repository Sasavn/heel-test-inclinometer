#include "tab5_workspace.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static tab5_workspace_t w;
static tab5_workspace_view_t v;
static tab5_relative_config_t roles={.version=1,.reference_index=0,.object_index=1,.axes_confirmed=1};
static tab5_relative_result_t p={.ready=true,.generation=1,.sequence={1,1},.timestamp_us={1000000,1016000},.relative={.05f,-.2f}};
static void update(void){tab5_workspace_update(&w,&roles,&p,p.timestamp_us[1]+1000);tab5_workspace_snapshot(&w,&v);}
static void advance(void){for(unsigned i=0;i<2;i++){p.sequence[i]++;p.timestamp_us[i]+=100000;}}
static void production_pair_fixtures(void){
 tab5_relative_t relative;tab5_relative_init(&relative,&roles);
 tab5_measure_snapshot_t sample={0};tab5_relative_result_t pair;
 tab5_workspace_config_t cfg;tab5_workspace_config_default(&cfg);cfg.limits_enabled=1;cfg.limit_deg[0]=cfg.limit_deg[1]=.05f;
 tab5_workspace_init(&w,&cfg);
 for(unsigned i=0;i<2;i++)sample.sensor[i]=(tab5_measure_sensor_t){.address=i+2,.valid=true,.good=1,
     .timestamp_us=1000000+16000*i,.decoded_x=i?1.28f:1.20f,.decoded_y=i?-.44f:-.40f,
     .x=75,.y=-52,.zero_x=19,.zero_y=29,.filtered_x=35,.filtered_y=-29};
 tab5_relative_evaluate(&relative,&sample,1020000,&pair);C(pair.ready&&fabsf(pair.relative[0]-.08f)<1e-6f);
 tab5_workspace_update(&w,&roles,&pair,1020000);C(fabsf(w.live.value[1]+.04f)<1e-6f);
 tab5_relative_observe(&relative,&sample,1020000,&pair);
 C(tab5_relative_capture(&relative,&sample,1020000));
 sample.sensor[1].decoded_x=1.31f;sample.sensor[1].decoded_y=-.46f;
 sample.sensor[0].good++;sample.sensor[1].good++;sample.sensor[0].timestamp_us+=100000;sample.sensor[1].timestamp_us+=100000;
 tab5_relative_evaluate(&relative,&sample,1120000,&pair);tab5_workspace_update(&w,&roles,&pair,1120000);
 C(fabsf(w.live.value[0]-.03f)<1e-6f&&fabsf(w.live.value[1]+.02f)<1e-6f&&w.live.live_pairs==1);
 tab5_relative_init(&relative,&roles);tab5_workspace_init(&w,&cfg);
 const float values[3][2]={{-.04f,.02f},{.08f,-.03f},{.02f,.01f}};
 for(unsigned j=0;j<3;j++){
  for(unsigned i=0;i<2;i++){sample.sensor[i].decoded_x=i?values[j][0]:0;sample.sensor[i].decoded_y=i?values[j][1]:0;
   sample.sensor[i].timestamp_us=2000000+j*100000+i*16000;sample.sensor[i].good++;sample.sensor[i].last_error=TAB5_MEASURE_OK;}
  tab5_relative_evaluate(&relative,&sample,2020000+j*100000,&pair);
  for(unsigned repeat=0;repeat<20;repeat++)tab5_workspace_update(&w,&roles,&pair,2020000+j*100000);
  if(j==1){sample.sensor[1].decoded_x=sample.sensor[1].decoded_y=9;sample.sensor[1].last_error=TAB5_MEASURE_CRC;
   tab5_relative_evaluate(&relative,&sample,2120001,&pair);tab5_workspace_update(&w,&roles,&pair,2120001);}
 }
 C(w.live.live_pairs==3&&w.live.live_gaps==1);
 C(w.live.minimum[0]==-.04f&&w.live.maximum[0]==.08f&&w.live.minimum[1]==-.03f&&w.live.maximum[1]==.02f);
 pair.ready=true;pair.quality=0;pair.relative[0]=.05f;pair.relative[1]=-.05f;
 tab5_workspace_update(&w,&roles,&pair,2220001);C(w.live.within_limit[0]&&w.live.within_limit[1]);
 pair.relative[0]=nextafterf(.05f,INFINITY);tab5_workspace_update(&w,&roles,&pair,2220002);C(!w.live.within_limit[0]);
 sample.sensor[0].last_error=TAB5_MEASURE_OK;sample.sensor[1].last_error=TAB5_MEASURE_OK;
 sample.sensor[0].timestamp_us=3000000;sample.sensor[1].timestamp_us=3050000;
 tab5_relative_evaluate(&relative,&sample,3500000,&pair);C(pair.ready&&pair.age_us[0]==500000&&pair.skew_us==50000);
 tab5_relative_evaluate(&relative,&sample,3500001,&pair);C(!pair.ready&&(pair.quality&TAB5_PAIR_STALE));
}
int main(void){
 tab5_workspace_config_t cfg;tab5_workspace_config_default(&cfg);C(tab5_workspace_config_valid(&cfg));
 cfg.limit_deg[0]=NAN;C(!tab5_workspace_config_valid(&cfg));cfg.limit_deg[0]=0;C(!tab5_workspace_config_valid(&cfg));
 cfg.limit_deg[0]=.1f;cfg.limits_enabled=1;tab5_workspace_init(&w,&cfg);update();
 C(v.values_valid&&v.stats_valid&&v.count==1&&v.live_pairs==1);C(v.value[0]==.05f&&v.value[1]==-.2f);
 C(v.limit_known&&v.within_limit[0]&&!v.within_limit[1]);
 for(unsigned i=0;i<200;i++){update();}C(v.count==1&&v.live_pairs==1); /* snapshots not samples */
 p.timestamp_us[0]+=100000;p.timestamp_us[1]+=100000;
 p.sequence[0]++;update();C(v.live_pairs==1);p.sequence[1]++;update();C(v.live_pairs==2);
 C(tab5_workspace_hold(&w,true,1200000));tab5_workspace_snapshot(&w,&v);C(v.held&&v.held_us==1200000);
 float frozen=v.value[0];uint16_t count=v.count;
 advance();p.relative[0]=.3f;update();C(v.held&&v.value[0]==frozen&&v.count==count&&!v.within_limit[0]);
 p.ready=false;p.quality=TAB5_PAIR_ERROR|TAB5_PAIR_STALE;update();
 C(v.held&&v.values_valid&&!v.live_ready&&!v.limit_known&&v.live_gaps==1);C(v.live_quality==p.quality);
 for(unsigned i=0;i<20;i++){update();}C(v.live_gaps==1); /* one outage, no fake zeros */
 C(!tab5_workspace_hold(&w,true,1300000));C(tab5_workspace_hold(&w,false,1300000));
 tab5_workspace_snapshot(&w,&v);C(!v.values_valid&&v.count==4&&v.history[3].gap&&isnan(v.history[3].value[0]));
 p.ready=true;p.quality=0;p.sequence[0]++;update();C(!v.values_valid);
 p.sequence[1]++;p.timestamp_us[0]+=100000;p.timestamp_us[1]+=100000;update();C(v.values_valid&&v.live_pairs==4);
 C(v.minimum[0]==.05f&&v.maximum[0]==.3f);
 advance();p.timestamp_us[0]+=1000000;p.timestamp_us[1]+=1000000;update();C(v.live_gaps==2); /* skipped interval */
 cfg.filter_enabled=1;C(tab5_workspace_configure(&w,&cfg));update();C(!v.values_valid&&!v.stats_valid&&!v.held);
 for(unsigned i=0;i<5;i++){advance();p.relative[0]=i?1:0;update();C(v.filter_warm==(i==4));}
 C(v.value[0]>.5f&&v.value[0]<1);C(!v.within_limit[0]); /* filter does not hide raw limit violation */
 C(tab5_workspace_hold(&w,true,9999999));p.generation++;update();C(!v.held&&v.live_pairs==1&&v.count==1);
 roles.reference_index=1;roles.object_index=0;update();C(v.count==1&&v.live_pairs==1); /* independent of generation */
 for(unsigned i=0;i<1000;i++){advance();p.relative[0]=(float)(i%10);update();}
 C(v.count==TAB5_WORKSPACE_CAPACITY&&v.next<TAB5_WORKSPACE_CAPACITY&&v.live_pairs==1001);
 unsigned first=(v.next+TAB5_WORKSPACE_CAPACITY-v.count)%TAB5_WORKSPACE_CAPACITY;
 C(v.history[first].timestamp_us<v.history[(v.next+TAB5_WORKSPACE_CAPACITY-1)%TAB5_WORKSPACE_CAPACITY].timestamp_us);
 uint64_t last=w.last_point_us,total=v.live_pairs;
 p.sequence[0]++;p.sequence[1]++;p.timestamp_us[0]-=10;p.timestamp_us[1]-=10;
 tab5_workspace_update(&w,&roles,&p,w.last_now_us+1000);tab5_workspace_snapshot(&w,&v);
 C(v.live_pairs==total&&!v.values_valid&&w.last_point_us==last); /* no backwards point */
 p.relative[0]=77;tab5_workspace_update(&w,&roles,&p,w.last_now_us+1000);C(w.live.live_pairs==total);
 tab5_workspace_update(&w,&roles,&p,1);tab5_workspace_snapshot(&w,&v);C(v.count==0&&!v.stats_valid&&!v.held);
 tab5_workspace_clear(&w);update();C(v.count==0&&!v.held&&!v.stats_valid);advance();update();C(v.count==1);
 roles.reference_index=0;roles.object_index=1;production_pair_fixtures();
 printf("PASS workspace %u checks: pair dedup, both fresh, relative XY, gaps/no zeros, HOLD frozen/live-quality, raw operator limits, filter warm/reset, generation/roles, bounded ring\n",checks);
 return 0;
}
