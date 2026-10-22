/* Host regression of actual tab5_ui_graph.c. No display or LVGL runtime. */
#include "tab5_ui_graph.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static tab5_ui_graph_t g;
static int failures,checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__func__,__LINE__,#x);failures++;return;}}while(0)
static tab5_ui_state_t state(uint64_t now,float a,float b,float c,float d){
 tab5_ui_state_t s={0};s.now_known=true;s.now_us=now;
 for(unsigned i=0;i<2;i++){s.measurement.sensor[i].valid=true;s.measurement.sensor[i].good=1;s.measurement.sensor[i].timestamp_us=now;}
 s.measurement.sensor[0].x=a;s.measurement.sensor[0].y=b;s.measurement.sensor[1].x=c;s.measurement.sensor[1].y=d;return s;
}
static unsigned read_series(unsigned seconds,unsigned series,int32_t out[1200]){
 return tab5_ui_graph_read(&g,seconds,series,out);
}
static void empty_and_unknown(void){
 tab5_ui_graph_reset(&g);tab5_ui_graph_t before=g;
 tab5_ui_state_t s=state(1000000,1,2,3,4);s.now_known=false;tab5_ui_graph_sample(&g,&s);
 CHECK(!memcmp(&g,&before,sizeof(g)));
 unsigned windows[]={30,120,600},sizes[]={300,240,1200};
 for(unsigned w=0;w<3;w++)for(unsigned k=0;k<4;k++){int32_t out[1200];CHECK(read_series(windows[w],k,out)==sizes[w]);for(unsigned i=0;i<sizes[w];i++)CHECK(out[i]==LV_CHART_POINT_NONE);}
 s.now_known=true;tab5_ui_graph_sample(&g,&s);before=g;s.now_known=false;s.now_us+=999999999;tab5_ui_graph_sample(&g,&s);CHECK(!memcmp(&g,&before,sizeof(g)));
}
static void duplicate_timestamps(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,1,2,3,4);tab5_ui_graph_sample(&g,&s);
 s.now_us+=50000;s.measurement.sensor[0].x=99;tab5_ui_graph_sample(&g,&s);
 int32_t out[1200];read_series(30,0,out);CHECK(out[299]==100);
 s.now_us=1100000;tab5_ui_graph_sample(&g,&s);read_series(30,0,out);CHECK(out[298]==100&&out[299]==LV_CHART_POINT_NONE);
 s.now_us=1500000;tab5_ui_graph_sample(&g,&s);read_series(600,0,out);CHECK(out[1198]==100&&out[1199]==LV_CHART_POINT_NONE);
}
static void validity_and_axis_values(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,NAN,INFINITY,-INFINITY,327.67f);tab5_ui_graph_sample(&g,&s);
 int32_t out[1200];for(unsigned k=0;k<4;k++){read_series(30,k,out);CHECK(out[299]==LV_CHART_POINT_NONE);}
 s=state(1100000,-327.67f,327.66f,-327.66f,0.125f);tab5_ui_graph_sample(&g,&s);
 int expected[]={LV_CHART_POINT_NONE,32766,-32766,13};for(unsigned k=0;k<4;k++){read_series(30,k,out);CHECK(out[299]==expected[k]);}
 s=state(1200000,1,2,3,4);s.measurement.sensor[0].valid=false;s.measurement.sensor[1].good=0;tab5_ui_graph_sample(&g,&s);
 for(unsigned k=0;k<4;k++){read_series(30,k,out);CHECK(out[299]==LV_CHART_POINT_NONE);}
 s=state(1300000,1,2,3,4);s.measurement.sensor[0].timestamp_us=0;s.measurement.sensor[1].timestamp_us=1400000;tab5_ui_graph_sample(&g,&s);
 for(unsigned k=0;k<4;k++){read_series(30,k,out);CHECK(out[299]==LV_CHART_POINT_NONE);}
}
static void offline_and_stale_gaps(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,1,2,3,4);tab5_ui_graph_sample(&g,&s);
 s.measurement.sensor[0].valid=false;s.now_us+=10000;tab5_ui_graph_sample(&g,&s);
 int32_t out[1200];read_series(30,0,out);CHECK(out[299]==LV_CHART_POINT_NONE);read_series(30,2,out);CHECK(out[299]==300);
 s.now_us=1800000;tab5_ui_graph_sample(&g,&s);read_series(30,2,out);CHECK(out[291]==300);for(unsigned i=292;i<300;i++)CHECK(out[i]==LV_CHART_POINT_NONE);
 s=state(1900000,5,6,7,8);tab5_ui_graph_sample(&g,&s);read_series(30,0,out);CHECK(out[299]==500);
}
static void buckets_and_small_gaps(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(999999,1,2,3,4);tab5_ui_graph_sample(&g,&s);
 s=state(1000000,5,6,7,8);tab5_ui_graph_sample(&g,&s);int32_t out[1200];
 read_series(30,0,out);CHECK(out[298]==100&&out[299]==500);read_series(600,0,out);CHECK(out[1198]==100&&out[1199]==500);
 s=state(1300000,9,10,11,12);tab5_ui_graph_sample(&g,&s);read_series(30,0,out);CHECK(out[296]==500&&out[297]==LV_CHART_POINT_NONE&&out[298]==LV_CHART_POINT_NONE&&out[299]==900);
 read_series(600,0,out);CHECK(out[1199]==900); /* Latest measurement within current half-second slot. */
}
static void delayed_timestamp_fast(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1050000,1,2,3,4);
 for(unsigned i=0;i<2;i++)s.measurement.sensor[i].timestamp_us=990000;
 tab5_ui_graph_sample(&g,&s);int32_t out[1200];read_series(30,0,out);
 CHECK(out[298]==100);CHECK(out[299]==LV_CHART_POINT_NONE);
}
static void delayed_timestamp_slow(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1050000,1,2,3,4);
 for(unsigned i=0;i<2;i++)s.measurement.sensor[i].timestamp_us=990000;
 tab5_ui_graph_sample(&g,&s);int32_t out[1200];read_series(600,0,out);
 CHECK(out[1198]==100);CHECK(out[1199]==LV_CHART_POINT_NONE);
}
static void newest_measurement_per_bucket(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000040,1,2,3,4);tab5_ui_graph_sample(&g,&s);
 s.now_us=1000050;s.measurement.sensor[0].timestamp_us=1000020;s.measurement.sensor[0].x=9;
 s.measurement.sensor[1].timestamp_us=1000050;s.measurement.sensor[1].x=7;
 tab5_ui_graph_sample(&g,&s);int32_t out[1200];
 read_series(30,2,out);CHECK(out[299]==700);
 read_series(30,0,out);CHECK(out[299]==100);
 read_series(600,0,out);CHECK(out[1199]==100);
}
static void delayed_retention_and_exact_edges(void){
 int32_t out[1200];
 /* A 20-second-old first measurement belongs at t=80s, never at arrival 100s. */
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(100000000,1,2,3,4);
 for(unsigned k=0;k<2;k++)s.measurement.sensor[k].timestamp_us=80000000;
 tab5_ui_graph_sample(&g,&s);
 for(unsigned k=0;k<4;k++){
  read_series(30,k,out);for(unsigned i=0;i<300;i++)CHECK(out[i]==(i==99?(int32_t)(100*(k+1)):LV_CHART_POINT_NONE));
  read_series(120,k,out);for(unsigned i=0;i<240;i++)CHECK(out[i]==(i==199?(int32_t)(100*(k+1)):LV_CHART_POINT_NONE));
  read_series(600,k,out);for(unsigned i=0;i<1200;i++)CHECK(out[i]==(i==1159?(int32_t)(100*(k+1)):LV_CHART_POINT_NONE));
 }
 /* The oldest retained fast bucket is accepted; the next older one is expired. */
 tab5_ui_graph_reset(&g);s=state(100000000,1,2,3,4);
 s.measurement.sensor[0].timestamp_us=70100000;s.measurement.sensor[1].timestamp_us=70099999;
 tab5_ui_graph_sample(&g,&s);read_series(30,0,out);CHECK(out[0]==100);read_series(30,2,out);for(unsigned i=0;i<300;i++)CHECK(out[i]==LV_CHART_POINT_NONE);
 /* Same check for the 600-second history, independently of the fast ring. */
 tab5_ui_graph_reset(&g);s=state(1000000000,1,2,3,4);
 s.measurement.sensor[0].timestamp_us=400500000;s.measurement.sensor[1].timestamp_us=400499999;
 tab5_ui_graph_sample(&g,&s);read_series(600,0,out);CHECK(out[0]==100);read_series(600,2,out);for(unsigned i=0;i<1200;i++)CHECK(out[i]==LV_CHART_POINT_NONE);
 /* Exact step-age is still the previous bucket, not the current one. */
 tab5_ui_graph_reset(&g);s=state(1500000,1,2,3,4);
 s.measurement.sensor[0].timestamp_us=1400000;s.measurement.sensor[1].timestamp_us=1000000;
 tab5_ui_graph_sample(&g,&s);read_series(30,0,out);CHECK(out[298]==100&&out[299]==LV_CHART_POINT_NONE);
 read_series(600,2,out);CHECK(out[1198]==300&&out[1199]==LV_CHART_POINT_NONE);
}
static void delayed_after_existing_history(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(100000000,1,2,3,4);tab5_ui_graph_sample(&g,&s);
 s=state(110000000,5,6,7,8);for(unsigned k=0;k<2;k++)s.measurement.sensor[k].timestamp_us=105000000;
 tab5_ui_graph_sample(&g,&s);int32_t out[1200];read_series(30,0,out);
 CHECK(out[199]==100&&out[249]==500&&out[299]==LV_CHART_POINT_NONE);
 for(unsigned i=0;i<300;i++)if(i!=199&&i!=249)CHECK(out[i]==LV_CHART_POINT_NONE);
 s.now_us=110100000;s.measurement.sensor[0].x=99;tab5_ui_graph_sample(&g,&s);read_series(30,0,out);CHECK(out[248]==500&&out[299]==LV_CHART_POINT_NONE);
}
static void large_jump_and_restart(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,1,2,3,4);tab5_ui_graph_sample(&g,&s);
 s=state(4000000000ULL,5,6,7,8);tab5_ui_graph_sample(&g,&s);
 unsigned windows[]={30,120,600},sizes[]={300,240,1200};int32_t out[1200];
 for(unsigned w=0;w<3;w++){read_series(windows[w],0,out);for(unsigned i=0;i<sizes[w]-1;i++)CHECK(out[i]==LV_CHART_POINT_NONE);CHECK(out[sizes[w]-1]==500);}
 s=state(500000,9,10,11,12);tab5_ui_graph_sample(&g,&s);CHECK(g.slow_count==1&&g.fast_count==1);
 for(unsigned w=0;w<3;w++){read_series(windows[w],0,out);for(unsigned i=0;i<sizes[w]-1;i++)CHECK(out[i]==LV_CHART_POINT_NONE);CHECK(out[sizes[w]-1]==900);}
}
static void capacity_jump_boundaries(void){
 unsigned windows[]={30,600},caps[]={300,1200};uint64_t steps[]={100000,500000};
 for(unsigned w=0;w<2;w++)for(unsigned extra=0;extra<3;extra++){
  tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,1,2,3,4);tab5_ui_graph_sample(&g,&s);
  s=state(1000000ULL+(caps[w]-1+extra)*steps[w],5,6,7,8);tab5_ui_graph_sample(&g,&s);
  int32_t out[1200];CHECK(read_series(windows[w],0,out)==caps[w]);
  CHECK(out[caps[w]-1]==500);
  for(unsigned i=0;i<caps[w]-1;i++)CHECK(out[i]==(!extra&&!i?100:LV_CHART_POINT_NONE));
 }
}
static void ten_minutes_2hz_then_fast_10hz(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s;
 for(unsigned i=0;i<1200;i++){float v=(float)i/100;s=state(1000000ULL+i*500000ULL,v,v+20,v+40,v+60);tab5_ui_graph_sample(&g,&s);}
 CHECK(g.slow_count==1200&&g.fast_count==300);int32_t out[1200];
 for(unsigned k=0;k<4;k++){
  read_series(600,k,out);for(unsigned i=0;i<1200;i++)CHECK(out[i]==(int32_t)(i+k*2000));
  read_series(120,k,out);for(unsigned i=0;i<240;i++)CHECK(out[i]==(int32_t)(960+i+k*2000));
  read_series(30,k,out);for(unsigned i=0;i<300;i++)CHECK(out[i]==((i%5==4)?(int32_t)(1140+i/5+k*2000):LV_CHART_POINT_NONE));
 }
 for(unsigned i=0;i<300;i++){float v=(float)i/100;s=state(601000000ULL+i*100000ULL,v,v+20,v+40,v+60);tab5_ui_graph_sample(&g,&s);}
 for(unsigned k=0;k<4;k++){
  read_series(30,k,out);for(unsigned i=0;i<300;i++)CHECK(out[i]==(int32_t)(i+k*2000));
  read_series(600,k,out);for(unsigned i=0;i<1200;i++)CHECK(out[i]==(int32_t)((i<1140?i+60:4+(i-1140)*5)+k*2000));
  read_series(120,k,out);for(unsigned i=0;i<240;i++)CHECK(out[i]==(int32_t)((i<180?i+1020:4+(i-180)*5)+k*2000));
 }
}
static void repeated_wrap_all_windows(void){
 tab5_ui_graph_reset(&g);
 /* 1800 seconds at 10Hz wraps both rings repeatedly. */
 for(unsigned i=0;i<18000;i++){float v=(float)(i%10000)/100;tab5_ui_state_t s=state(1000000ULL+i*100000ULL,v,v+10,v+20,v+30);tab5_ui_graph_sample(&g,&s);}
 CHECK(g.slow_count==1200&&g.fast_count==300);unsigned windows[]={30,120,600},sizes[]={300,240,1200};int32_t out[1200];
 for(unsigned w=0;w<3;w++)for(unsigned k=0;k<4;k++){
  CHECK(read_series(windows[w],k,out)==sizes[w]);
  for(unsigned i=0;i<sizes[w];i++){
   unsigned sample=w==0?17700+i:17999-(sizes[w]-1-i)*5;
   CHECK(out[i]==(int32_t)(sample%10000+k*1000));
  }
 }
}
int main(void){
 _Static_assert(LV_CHART_POINT_NONE==INT32_MAX,"Actual LVGL chart gap marker changed");
 struct {const char *name;void (*run)(void);} cases[]={
#define CASE(fn) {#fn,fn}
  CASE(empty_and_unknown),CASE(duplicate_timestamps),CASE(validity_and_axis_values),
  CASE(offline_and_stale_gaps),CASE(buckets_and_small_gaps),CASE(delayed_timestamp_fast),
  CASE(delayed_timestamp_slow),CASE(newest_measurement_per_bucket),CASE(delayed_retention_and_exact_edges),CASE(delayed_after_existing_history),CASE(large_jump_and_restart),
  CASE(capacity_jump_boundaries),CASE(ten_minutes_2hz_then_fast_10hz),CASE(repeated_wrap_all_windows)
#undef CASE
 };
 for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++){int before=failures;cases[i].run();printf("%s %s\n",before==failures?"PASS":"FAIL",cases[i].name);}
 printf("graph: %d checks, %d failed cases\n",checks,failures);return failures?1:0;
}
