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
 for(unsigned w=0;w<3;w++)for(unsigned k=0;k<4;k++){int32_t out[1200];CHECK(read_series(windows[w],k,out)==sizes[w]);for(unsigned i=0;i<sizes[w];i++)CHECK(out[i]==TAB5_UI_GRAPH_RENDER_NONE);}
 s.now_known=true;tab5_ui_graph_sample(&g,&s);before=g;s.now_known=false;s.now_us+=999999999;tab5_ui_graph_sample(&g,&s);CHECK(!memcmp(&g,&before,sizeof(g)));
}
static void duplicate_timestamps(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,1,2,3,4);tab5_ui_graph_sample(&g,&s);
 s.now_us+=50000;s.measurement.sensor[0].x=99;tab5_ui_graph_sample(&g,&s);
 int32_t out[1200];read_series(30,0,out);CHECK(out[299]==100);
 s.now_us=1100000;tab5_ui_graph_sample(&g,&s);read_series(30,0,out);CHECK(out[298]==100&&out[299]==TAB5_UI_GRAPH_RENDER_NONE);
 s.now_us=1500000;tab5_ui_graph_sample(&g,&s);read_series(600,0,out);CHECK(out[1198]==100&&out[1199]==TAB5_UI_GRAPH_RENDER_NONE);
}
static void validity_and_axis_values(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,NAN,INFINITY,-INFINITY,327.67f);tab5_ui_graph_sample(&g,&s);
 int32_t out[1200];for(unsigned k=0;k<4;k++){read_series(30,k,out);CHECK(out[299]==TAB5_UI_GRAPH_RENDER_NONE);}
 s=state(1100000,-327.67f,327.66f,-327.66f,0.125f);tab5_ui_graph_sample(&g,&s);
 int expected[]={TAB5_UI_GRAPH_RENDER_NONE,32766,-32766,13};for(unsigned k=0;k<4;k++){read_series(30,k,out);CHECK(out[299]==expected[k]);}
 s=state(1200000,1,2,3,4);s.measurement.sensor[0].valid=false;s.measurement.sensor[1].good=0;tab5_ui_graph_sample(&g,&s);
 for(unsigned k=0;k<4;k++){read_series(30,k,out);CHECK(out[299]==TAB5_UI_GRAPH_RENDER_NONE);}
 s=state(1300000,1,2,3,4);s.measurement.sensor[0].timestamp_us=0;s.measurement.sensor[1].timestamp_us=1400000;tab5_ui_graph_sample(&g,&s);
 for(unsigned k=0;k<4;k++){read_series(30,k,out);CHECK(out[299]==TAB5_UI_GRAPH_RENDER_NONE);}
}
static void offline_and_stale_gaps(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,1,2,3,4);tab5_ui_graph_sample(&g,&s);
 s.measurement.sensor[0].valid=false;s.now_us+=10000;tab5_ui_graph_sample(&g,&s);
 int32_t out[1200];read_series(30,0,out);CHECK(out[299]==TAB5_UI_GRAPH_RENDER_NONE);read_series(30,2,out);CHECK(out[299]==300);
 s.now_us=1800000;tab5_ui_graph_sample(&g,&s);read_series(30,2,out);CHECK(out[291]==300);for(unsigned i=292;i<300;i++)CHECK(out[i]==TAB5_UI_GRAPH_RENDER_NONE);
 s=state(1900000,5,6,7,8);tab5_ui_graph_sample(&g,&s);read_series(30,0,out);CHECK(out[299]==500);
}
static void buckets_and_small_gaps(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(999999,1,2,3,4);tab5_ui_graph_sample(&g,&s);
 s=state(1000000,5,6,7,8);tab5_ui_graph_sample(&g,&s);int32_t out[1200];
 read_series(30,0,out);CHECK(out[298]==100&&out[299]==500);read_series(600,0,out);CHECK(out[1198]==100&&out[1199]==500);
 s=state(1300000,9,10,11,12);tab5_ui_graph_sample(&g,&s);read_series(30,0,out);CHECK(out[296]==500&&out[297]==TAB5_UI_GRAPH_RENDER_NONE&&out[298]==TAB5_UI_GRAPH_RENDER_NONE&&out[299]==900);
 read_series(600,0,out);CHECK(out[1199]==900); /* Latest measurement within current half-second slot. */
}
static void delayed_timestamp_fast(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1050000,1,2,3,4);
 for(unsigned i=0;i<2;i++)s.measurement.sensor[i].timestamp_us=990000;
 tab5_ui_graph_sample(&g,&s);int32_t out[1200];read_series(30,0,out);
 CHECK(out[298]==100);CHECK(out[299]==TAB5_UI_GRAPH_RENDER_NONE);
}
static void delayed_timestamp_slow(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1050000,1,2,3,4);
 for(unsigned i=0;i<2;i++)s.measurement.sensor[i].timestamp_us=990000;
 tab5_ui_graph_sample(&g,&s);int32_t out[1200];read_series(600,0,out);
 CHECK(out[1198]==100);CHECK(out[1199]==TAB5_UI_GRAPH_RENDER_NONE);
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
  read_series(30,k,out);for(unsigned i=0;i<300;i++)CHECK(out[i]==(i==99?(int32_t)(100*(k+1)):TAB5_UI_GRAPH_RENDER_NONE));
  read_series(120,k,out);for(unsigned i=0;i<240;i++)CHECK(out[i]==(i==199?(int32_t)(100*(k+1)):TAB5_UI_GRAPH_RENDER_NONE));
  read_series(600,k,out);for(unsigned i=0;i<1200;i++)CHECK(out[i]==(i==1159?(int32_t)(100*(k+1)):TAB5_UI_GRAPH_RENDER_NONE));
 }
 /* The oldest retained fast bucket is accepted; the next older one is expired. */
 tab5_ui_graph_reset(&g);s=state(100000000,1,2,3,4);
 s.measurement.sensor[0].timestamp_us=70100000;s.measurement.sensor[1].timestamp_us=70099999;
 tab5_ui_graph_sample(&g,&s);read_series(30,0,out);CHECK(out[0]==100);read_series(30,2,out);for(unsigned i=0;i<300;i++)CHECK(out[i]==TAB5_UI_GRAPH_RENDER_NONE);
 /* Same check for the 600-second history, independently of the fast ring. */
 tab5_ui_graph_reset(&g);s=state(1000000000,1,2,3,4);
 s.measurement.sensor[0].timestamp_us=400500000;s.measurement.sensor[1].timestamp_us=400499999;
 tab5_ui_graph_sample(&g,&s);read_series(600,0,out);CHECK(out[0]==100);read_series(600,2,out);for(unsigned i=0;i<1200;i++)CHECK(out[i]==TAB5_UI_GRAPH_RENDER_NONE);
 /* Exact step-age is still the previous bucket, not the current one. */
 tab5_ui_graph_reset(&g);s=state(1500000,1,2,3,4);
 s.measurement.sensor[0].timestamp_us=1400000;s.measurement.sensor[1].timestamp_us=1000000;
 tab5_ui_graph_sample(&g,&s);read_series(30,0,out);CHECK(out[298]==100&&out[299]==TAB5_UI_GRAPH_RENDER_NONE);
 read_series(600,2,out);CHECK(out[1198]==300&&out[1199]==TAB5_UI_GRAPH_RENDER_NONE);
}
static void delayed_after_existing_history(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(100000000,1,2,3,4);tab5_ui_graph_sample(&g,&s);
 s=state(110000000,5,6,7,8);for(unsigned k=0;k<2;k++)s.measurement.sensor[k].timestamp_us=105000000;
 tab5_ui_graph_sample(&g,&s);int32_t out[1200];read_series(30,0,out);
 CHECK(out[199]==100&&out[249]==500&&out[299]==TAB5_UI_GRAPH_RENDER_NONE);
 for(unsigned i=0;i<300;i++)if(i!=199&&i!=249)CHECK(out[i]==TAB5_UI_GRAPH_RENDER_NONE);
 s.now_us=110100000;s.measurement.sensor[0].x=99;tab5_ui_graph_sample(&g,&s);read_series(30,0,out);CHECK(out[248]==500&&out[299]==TAB5_UI_GRAPH_RENDER_NONE);
}
static void large_jump_and_restart(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,1,2,3,4);tab5_ui_graph_sample(&g,&s);
 s=state(4000000000ULL,5,6,7,8);tab5_ui_graph_sample(&g,&s);
 unsigned windows[]={30,120,600},sizes[]={300,240,1200};int32_t out[1200];
 for(unsigned w=0;w<3;w++){read_series(windows[w],0,out);for(unsigned i=0;i<sizes[w]-1;i++)CHECK(out[i]==TAB5_UI_GRAPH_RENDER_NONE);CHECK(out[sizes[w]-1]==500);}
 s=state(500000,9,10,11,12);tab5_ui_graph_sample(&g,&s);CHECK(g.slow_count==1&&g.fast_count==1);
 for(unsigned w=0;w<3;w++){read_series(windows[w],0,out);for(unsigned i=0;i<sizes[w]-1;i++)CHECK(out[i]==TAB5_UI_GRAPH_RENDER_NONE);CHECK(out[sizes[w]-1]==900);}
}
static void capacity_jump_boundaries(void){
 unsigned windows[]={30,600},caps[]={300,1200};uint64_t steps[]={100000,500000};
 for(unsigned w=0;w<2;w++)for(unsigned extra=0;extra<3;extra++){
  tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,1,2,3,4);tab5_ui_graph_sample(&g,&s);
  s=state(1000000ULL+(caps[w]-1+extra)*steps[w],5,6,7,8);tab5_ui_graph_sample(&g,&s);
  int32_t out[1200];CHECK(read_series(windows[w],0,out)==caps[w]);
  CHECK(out[caps[w]-1]==500);
  for(unsigned i=0;i<caps[w]-1;i++)CHECK(out[i]==(!extra&&!i?100:TAB5_UI_GRAPH_RENDER_NONE));
 }
}
static void ten_minutes_2hz_then_fast_10hz(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s;
 for(unsigned i=0;i<1200;i++){float v=(float)i/100;s=state(1000000ULL+i*500000ULL,v,v+20,v+40,v+60);tab5_ui_graph_sample(&g,&s);}
 CHECK(g.slow_count==1200&&g.fast_count==300);int32_t out[1200];
 for(unsigned k=0;k<4;k++){
  read_series(600,k,out);for(unsigned i=0;i<1200;i++)CHECK(out[i]==(int32_t)(i+k*2000));
  read_series(120,k,out);for(unsigned i=0;i<240;i++)CHECK(out[i]==(int32_t)(960+i+k*2000));
  read_series(30,k,out);for(unsigned i=0;i<300;i++)CHECK(out[i]==((i%5==4)?(int32_t)(1140+i/5+k*2000):TAB5_UI_GRAPH_RENDER_NONE));
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
static void render_cadence_and_wrap(void){
 unsigned hz[]={1,5,10,50};
 for(unsigned h=0;h<4;h++){
  tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,0,0,0,0);
  s.measurement.config.frequency_hz=hz[h];s.measurement.config.gap_ms=15;
  unsigned ticks=hz[h]==1?10:hz[h]==5?2:1;
  uint64_t good=0;
  for(unsigned t=0;t<=700;t++){
   s.now_us=1000000ULL+t*100000ULL;
   if(t%ticks==0){good++;for(unsigned k=0;k<2;k++){s.measurement.sensor[k].good=good;s.measurement.sensor[k].timestamp_us=s.now_us;s.measurement.sensor[k].x=(float)t/100;}}
   tab5_ui_graph_sample(&g,&s);
  }
  int32_t raw[1200],out[1200];tab5_ui_graph_read(&g,30,0,raw);CHECK(tab5_ui_graph_read_render(&g,30,0,out)==300);
  unsigned first=0;while(first<300&&raw[first]==TAB5_UI_GRAPH_RENDER_NONE)first++;
  for(unsigned j=0;j<first;j++)CHECK(out[j]==TAB5_UI_GRAPH_RENDER_NONE);
  for(unsigned j=first;j<300;j++)CHECK(out[j]==(int32_t)(401+j));
  if(hz[h]<10)CHECK(raw[298]==TAB5_UI_GRAPH_RENDER_NONE);
  tab5_ui_graph_read(&g,120,0,raw);tab5_ui_graph_read_render(&g,120,0,out);for(unsigned j=0;j<240;j++)if(raw[j]!=TAB5_UI_GRAPH_RENDER_NONE)CHECK(raw[j]==out[j]);
 }
}
static void render_gaps_and_tail(void){
 for(unsigned mode=0;mode<6;mode++){
  tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,0,0,0,0);
  s.measurement.config.frequency_hz=5;s.measurement.config.gap_ms=15;
  tab5_ui_graph_sample(&g,&s);
  s.now_us=1100000;
  if(mode==1)s.measurement.sensor[0].valid=false;
  if(mode==2)s.measurement.sensor[0].errors++;
  if(mode==3)s.measurement.sensor[0].fail_streak=1;
  tab5_ui_graph_sample(&g,&s);
  s.now_us=mode==0?1400000:1200000;
  s.measurement.sensor[0].valid=true;s.measurement.sensor[0].fail_streak=0;
  s.measurement.sensor[0].good=mode==4?1:2;s.measurement.sensor[0].timestamp_us=s.now_us;
  s.measurement.sensor[0].x=2;tab5_ui_graph_sample(&g,&s);
  if(mode==5){s.now_us=1250000;s.measurement.sensor[0].good++;s.measurement.sensor[0].timestamp_us=s.now_us;tab5_ui_graph_sample(&g,&s);}
  int32_t raw[1200],out[1200];tab5_ui_graph_read(&g,30,0,raw);tab5_ui_graph_read_render(&g,30,0,out);
  CHECK(raw[298]==TAB5_UI_GRAPH_RENDER_NONE);CHECK(out[298]==(mode==0?150:(mode==2||mode==3||mode==5)?100:TAB5_UI_GRAPH_RENDER_NONE));
  s.now_us+=300000;tab5_ui_graph_sample(&g,&s);tab5_ui_graph_read_render(&g,30,0,out);
  CHECK(out[297]==TAB5_UI_GRAPH_RENDER_NONE&&out[298]==TAB5_UI_GRAPH_RENDER_NONE&&out[299]==TAB5_UI_GRAPH_RENDER_NONE);
 }
}
static void render_jitter_and_invalid_axis(void){
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,0,0,0,0);s.measurement.config.frequency_hz=10;
 tab5_ui_graph_sample(&g,&s);s=state(1200000,2,2,2,2);s.measurement.config.frequency_hz=10;
 for(unsigned k=0;k<2;k++){s.measurement.sensor[k].timestamp_us=1140000;s.measurement.sensor[k].good=2;}
 tab5_ui_graph_sample(&g,&s);
 s=state(1300000,4,4,4,4);s.measurement.config.frequency_hz=10;
 for(unsigned k=0;k<2;k++){s.measurement.sensor[k].timestamp_us=1260000;s.measurement.sensor[k].good=3;}
 tab5_ui_graph_sample(&g,&s);int32_t out[1200];tab5_ui_graph_read_render(&g,30,0,out);
 CHECK(out[298]==400&&out[299]==TAB5_UI_GRAPH_RENDER_NONE);
 /* 10Hz jitter straddling a bucket boundary leaves one empty grid slot. */
 tab5_ui_graph_reset(&g);s=state(1090000,0,0,0,0);s.measurement.config.frequency_hz=10;tab5_ui_graph_sample(&g,&s);
 s.now_us=1210000;s.measurement.sensor[0].timestamp_us=s.now_us;s.measurement.sensor[0].good++;s.measurement.sensor[0].x=2;tab5_ui_graph_sample(&g,&s);
 tab5_ui_graph_read_render(&g,30,0,out);CHECK(out[297]==0&&out[298]==100&&out[299]==200);
 /* A second reply in a healthy recovery bucket preserves its short edge. */
 tab5_ui_graph_reset(&g);s=state(1000000,0,0,0,0);s.measurement.config.frequency_hz=5;tab5_ui_graph_sample(&g,&s);
 s.now_us=1400000;s.measurement.sensor[0].timestamp_us=s.now_us;s.measurement.sensor[0].good++;s.measurement.sensor[0].x=2;tab5_ui_graph_sample(&g,&s);
 s.now_us=1450000;s.measurement.sensor[0].timestamp_us=s.now_us;s.measurement.sensor[0].good++;s.measurement.sensor[0].x=3;tab5_ui_graph_sample(&g,&s);
 tab5_ui_graph_read_render(&g,30,0,out);CHECK(out[298]!=TAB5_UI_GRAPH_RENDER_NONE);
 /* Explicit nonfinite sample cannot be interpolated through. */
 tab5_ui_graph_reset(&g);s=state(1000000,0,0,0,0);s.measurement.config.frequency_hz=1;tab5_ui_graph_sample(&g,&s);
 s.now_us=1100000;s.measurement.sensor[0].timestamp_us=s.now_us;s.measurement.sensor[0].good++;s.measurement.sensor[0].x=NAN;tab5_ui_graph_sample(&g,&s);
 s.now_us=1200000;s.measurement.sensor[0].timestamp_us=s.now_us;s.measurement.sensor[0].good++;s.measurement.sensor[0].x=2;tab5_ui_graph_sample(&g,&s);
 tab5_ui_graph_read_render(&g,30,0,out);CHECK(out[298]==TAB5_UI_GRAPH_RENDER_NONE);
 printf("graph RAM: %zu bytes; render metadata: %zu bytes\n",sizeof(g),sizeof(g.fast_meta)+sizeof(g.slow_meta)+sizeof(g.render_good)+sizeof(g.render_blocked));
}
static void render_skipped_snapshots(void){
 for(unsigned mode=0;mode<7;mode++){
  tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,0,0,0,0);
  s.measurement.config.frequency_hz=10;s.measurement.config.gap_ms=15;
  tab5_ui_graph_sample(&g,&s);
  if(mode==4){s.now_us=1100000;s.measurement.sensor[0].valid=false;tab5_ui_graph_sample(&g,&s);}
  s.now_us=mode==3?11000000:1200000;
  s.measurement.sensor[0].valid=true;s.measurement.sensor[0].timestamp_us=s.now_us;
  s.measurement.sensor[0].good=mode==1?1:mode==3?101:3;
  s.measurement.sensor[0].x=2;
  if(mode==2)s.measurement.sensor[0].errors++;
  if(mode==5)s.measurement.sensor[0].fail_streak=1;
  if(mode==6)s.measurement.sensor[0].good=0;
  tab5_ui_graph_sample(&g,&s);
  int32_t raw[1200],out[1200];tab5_ui_graph_read(&g,30,0,raw);tab5_ui_graph_read_render(&g,30,0,out);
  CHECK(raw[298]==TAB5_UI_GRAPH_RENDER_NONE);
  CHECK(out[298]==((mode==0||mode==2||mode==5)?100:TAB5_UI_GRAPH_RENDER_NONE));
  /* A sensor with no errors remains independent of the failing peer. */
  CHECK(out[299]==(mode==6?TAB5_UI_GRAPH_RENDER_NONE:200));
 }
 /* 5Hz, UI snapshot skips one reply: interpolate two periods, not dropout. */
 tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,0,0,0,0);s.measurement.config.frequency_hz=5;
 tab5_ui_graph_sample(&g,&s);s.now_us=1400000;s.measurement.sensor[0].timestamp_us=s.now_us;s.measurement.sensor[0].good=3;s.measurement.sensor[0].x=4;tab5_ui_graph_sample(&g,&s);
 int32_t out[1200];tab5_ui_graph_read_render(&g,30,0,out);CHECK(out[295]==0&&out[296]==100&&out[297]==200&&out[298]==300&&out[299]==400);
}
static void render_short_failures_all_ranges(void){
 unsigned windows[]={30,120,600};
 for(unsigned w=0;w<3;w++)for(unsigned mode=0;mode<7;mode++){
  /* At 1Hz, both grids have interior holes. 1.5s nominal bound admits
   * isolated errors, never an observed offline/three-failure transition. */
  tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,0,0,0,0);
  s.measurement.config.frequency_hz=1;s.measurement.config.gap_ms=15;tab5_ui_graph_sample(&g,&s);
  s.now_us=1500000;
  if(mode==1)s.measurement.sensor[0].valid=false;
  if(mode==2)s.measurement.sensor[0].fail_streak=3;
  if(mode==3)s.measurement.sensor[0].fail_streak=2;
  if(mode==0||mode==3)s.measurement.sensor[0].errors++;
  if(mode==4){s.measurement.sensor[0].good++;s.measurement.sensor[0].timestamp_us=s.now_us;s.measurement.sensor[0].x=NAN;}
  tab5_ui_graph_sample(&g,&s);
  s.now_us=mode==5?3000000:2000000;s.measurement.sensor[0].valid=true;s.measurement.sensor[0].fail_streak=0;
  s.measurement.sensor[0].good=mode==6?1:3;s.measurement.sensor[0].timestamp_us=s.now_us;s.measurement.sensor[0].x=2;
  tab5_ui_graph_sample(&g,&s);
  int32_t raw[1200],out[1200],again[1200];unsigned n=tab5_ui_graph_read(&g,windows[w],0,raw);
  tab5_ui_graph_read_render(&g,windows[w],0,out);tab5_ui_graph_read(&g,windows[w],0,again);
  CHECK(!memcmp(raw,again,n*sizeof(*raw)));CHECK(raw[n-2]==TAB5_UI_GRAPH_RENDER_NONE);
  CHECK((out[n-2]!=TAB5_UI_GRAPH_RENDER_NONE)==(mode==0||mode==3));
  CHECK(out[0]==TAB5_UI_GRAPH_RENDER_NONE); /* Never fabricate leading history. */
  s.now_us+=1000000;tab5_ui_graph_sample(&g,&s);tab5_ui_graph_read_render(&g,windows[w],0,out);
  CHECK(out[n-1]==TAB5_UI_GRAPH_RENDER_NONE); /* Never extrapolate stale tail. */
 }
 /* Actual 10Hz isolated 100-300ms failures, inclusive 500ms threshold;
  * same bound rejects 600ms even if the good counter jumps. */
 for(unsigned dt=200000;dt<=600000;dt+=100000){
  tab5_ui_graph_reset(&g);tab5_ui_state_t s=state(1000000,0,0,0,0);s.measurement.config.frequency_hz=10;tab5_ui_graph_sample(&g,&s);
  s.now_us+=100000;s.measurement.sensor[0].errors++;s.measurement.sensor[0].fail_streak=1;tab5_ui_graph_sample(&g,&s);
  s.now_us=1000000+dt;s.measurement.sensor[0].timestamp_us=s.now_us;s.measurement.sensor[0].good=20;s.measurement.sensor[0].fail_streak=0;s.measurement.sensor[0].x=2;tab5_ui_graph_sample(&g,&s);
  int32_t out[1200];tab5_ui_graph_read_render(&g,30,0,out);CHECK((out[298]!=TAB5_UI_GRAPH_RENDER_NONE)==(dt<=500000));
 }
}
int main(void){
 _Static_assert(TAB5_UI_GRAPH_RENDER_NONE==INT32_MAX,"Presentation gap marker changed");
 struct {const char *name;void (*run)(void);} cases[]={
#define CASE(fn) {#fn,fn}
  CASE(empty_and_unknown),CASE(duplicate_timestamps),CASE(validity_and_axis_values),
  CASE(offline_and_stale_gaps),CASE(buckets_and_small_gaps),CASE(delayed_timestamp_fast),
  CASE(delayed_timestamp_slow),CASE(newest_measurement_per_bucket),CASE(delayed_retention_and_exact_edges),CASE(delayed_after_existing_history),CASE(large_jump_and_restart),
  CASE(capacity_jump_boundaries),CASE(ten_minutes_2hz_then_fast_10hz),CASE(repeated_wrap_all_windows),CASE(render_cadence_and_wrap),CASE(render_gaps_and_tail),CASE(render_jitter_and_invalid_axis),CASE(render_skipped_snapshots),CASE(render_short_failures_all_ranges)
#undef CASE
 };
 for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++){int before=failures;cases[i].run();printf("%s %s\n",before==failures?"PASS":"FAIL",cases[i].name);}
 printf("graph: %d checks, %d failed cases\n",checks,failures);return failures?1:0;
}
