/* Host-QPC probe; links actual production RGB565 UI. No device or OS window. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../main/tab5_ui.c"
static uint16_t fb[1280*720],proof[1280*720];
static LARGE_INTEGER freq;
static unsigned checks;
#define CHECK(c) do {checks++;if(!(c)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#c);exit(2);}}while(0)
static double qpc(void){LARGE_INTEGER t;QueryPerformanceCounter(&t);return (double)t.QuadPart*1e6/freq.QuadPart;}
static void callback(const tab5_ui_action_t*a,void*p){(void)a;(void)p;}
static int cmp(const void*a,const void*b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
static void equality(void){memcpy(proof,fb,sizeof(fb));nui_obj_invalidate(u.root);nui_native_render();CHECK(!memcmp(proof,fb,sizeof(fb)));}
static void frame(tab5_ui_state_t*s){tab5_ui_update(s);nui_obj_update_layout(nui_screen_active());nui_native_render();}
int main(int argc,char**argv){CHECK(argc==2);QueryPerformanceFrequency(&freq);FILE*f=fopen(argv[1],"w");CHECK(f);fprintf(f,"{\"kind\":\"host-only QPC, production native RGB565, no hardware FPS claim\",\"qpc_frequency\":%lld,\"scenes\":[",(long long)freq.QuadPart);unsigned scenes=0;
for(unsigned theme=0;theme<2;theme++){
CHECK(nui_native_init(1280,720,fb));tab5_ui_init(callback,NULL);
tab5_ui_state_t s={.light_theme=theme,.dual_mode_known=true,.now_known=true,.now_us=1000000,.relative_config={.version=1,.reference_index=0,.object_index=1,.axes_confirmed=1}};tab5_measure_config_default(&s.measurement.config);s.measurement.sensor[0].address=2;s.measurement.sensor[1].address=3;
for(unsigned scene=0;scene<4;scene++){
s.mode=scene?TAB5_MODE_LEVEL:TAB5_MODE_HEEL;s.relative=(tab5_relative_result_t){.ready=scene!=2,.offset_valid=scene==3,.source={{1.2f,-.4f},{1.28f,-.44f}},.live={.08f,-.04f},.relative={.08f,-.04f},.offset={.02f,0},.timestamp_us={900000,920000},.sequence={10,11},.age_us={100000,80000},.skew_us=20000};if(scene==2){s.relative.quality=TAB5_PAIR_STALE;s.relative.live[0]=s.relative.live[1]=s.relative.relative[0]=s.relative.relative[1]=NAN;s.relative.age_us[0]=2500000;}
for(unsigned k=0;k<20;k++)frame(&s);equality();
nui_native_stats_t a,b;nui_native_profile_snapshot(&a);unsigned rendered=0,pending=0;double samples[300];for(unsigned k=0;k<300;k++){double start=qpc();tab5_ui_update(&s);nui_obj_update_layout(nui_screen_active());pending+=(unsigned)nui_native_get_pending_dirty(NULL,0);rendered+=nui_native_render()!=0;samples[k]=qpc()-start;}nui_native_profile_snapshot(&b);/* Report idle churn without aborting before allocation/transition coverage. */CHECK(b.allocation_count==a.allocation_count);CHECK(b.allocation_bytes==a.allocation_bytes);qsort(samples,300,sizeof(double),cmp);
if(scenes++)fprintf(f,",");fprintf(f,"{\"theme\":%u,\"scene\":\"%s\",\"samples\":300,\"identical_snapshot\":true,\"rendered_frames\":%u,\"pending_dirty\":%u,\"allocation_bytes\":%llu,\"allocation_count\":%llu,\"p50_us\":%.3f,\"p95_us\":%.3f,\"max_us\":%.3f}",theme,scene==0?"heel":scene==1?"level-live":scene==2?"level-stale":"level-REL",rendered,pending,(unsigned long long)b.allocation_bytes,(unsigned long long)b.allocation_count,samples[150],samples[285],samples[299]);
}
/* Warm caches for complete mode/modal lifetimes, then check all 200 cycles. */
size_t live_bytes=0,live_count=0,peak=0;double transitions[200];unsigned transitions_rendered=0;
for(unsigned k=0;k<240;k++){double start=qpc();s.mode=TAB5_MODE_HEEL;frame(&s);s.mode=TAB5_MODE_LEVEL;frame(&s);dual_open();nui_obj_update_layout(nui_screen_active());transitions_rendered+=nui_native_render()!=0;close_modal();nui_obj_update_layout(nui_screen_active());nui_native_render();double elapsed=qpc()-start;nui_native_stats_t b;nui_native_profile_snapshot(&b);if(k==39){live_bytes=b.allocation_bytes;live_count=b.allocation_count;}if(k>=40){transitions[k-40]=elapsed;CHECK(b.allocation_bytes==live_bytes);CHECK(b.allocation_count==live_count);}if(b.allocation_bytes>peak)peak=b.allocation_bytes;}
qsort(transitions,200,sizeof(double),cmp);fprintf(f,"],\"transition_theme_%u\":{\"warmup\":40,\"cycles\":200,\"post_close_live_bytes\":%llu,\"post_close_live_count\":%llu,\"no_growth\":true,\"modal_rendered_frames\":%u,\"p50_us\":%.3f,\"p95_us\":%.3f,\"max_us\":%.3f},\"theme_%u_followup_scenes\":[",theme,(unsigned long long)live_bytes,(unsigned long long)live_count,transitions_rendered,transitions[100],transitions[190],transitions[199],theme);
equality();tab5_ui_destroy();nui_native_deinit();scenes=0;
}
fprintf(f,"],\"checks\":%u,\"status\":\"PASS\"}\n",checks);fclose(f);printf("dual performance: %u checks PASS\n",checks);return 0;}
