/* Native-large v2: production sources, deterministic v1 numeric inputs.
 * Geometry/input routes differ from old LVGL: no cross-layout speed ratio.
 * All fixture generation, counters, equality checks and file IO are untimed. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "tab5_native_port.h"
#include "native/native_ui.h"
#include "native_baseline_contract.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {ANGLES,ROLL,GRAPH,RECORD,SETTINGS,NETWORK,IDLE,IDLE_UPDATE,FOCUS_IDLE,
      FOCUS_MOVE,QUEUE_IDLE,EMPTY_TOUCH,TIME_KEYS,TIME_TZ,SYNC_IDLE,TRANSITION,FULL,SCENES};
static const char *names[]={"live_angles","live_rolling","live_graph","recording",
 "settings_updates","network_updates","idle_poll","idle_update","focus_idle",
 "focus_move","queue_idle_event","queue_empty_touch","time_keyboard","time_timezone",
 "time_sync_caret_idle","six_page_transition","angles_force_full"};
static uint16_t fb[NB_WIDTH*NB_HEIGHT],proof[NB_WIDTH*NB_HEIGHT];
static uint32_t sink[NB_WIDTH*NB_HEIGHT],ms;
static LARGE_INTEGER frequency;
static tab5_ui_state_t state;
static unsigned callbacks;
static double copy_us;
static uint64_t flush_calls,copy_pixels;
static uint64_t qpc_us(void){LARGE_INTEGER t;QueryPerformanceCounter(&t);return (uint64_t)((t.QuadPart/frequency.QuadPart)*1000000ULL+(t.QuadPart%frequency.QuadPart)*1000000ULL/frequency.QuadPart);}
static double clock_us(void){LARGE_INTEGER t;QueryPerformanceCounter(&t);return (double)t.QuadPart*1000000.0/(double)frequency.QuadPart;}
static void flush(const nui_area_t *a,const uint16_t *data,size_t stride,void *user){
    (void)user;double start=clock_us();
    for(int y=a->y1;y<=a->y2;y++){
        const uint16_t *row=data+(size_t)(y-a->y1)*stride;
        for(int x=a->x1;x<=a->x2;x++)sink[y*NB_WIDTH+x]=nb_rgb565_expand(row[x-a->x1]);
    }
    copy_us+=clock_us()-start;flush_calls++;copy_pixels+=(uint64_t)(a->x2-a->x1+1)*(a->y2-a->y1+1);
}
static void action(const tab5_ui_action_t *a,void *user){(void)a;(void)user;callbacks++;}
static void event(tab5_ui_input_event_t kind,int delta){assert(tab5_native_port_post_input(kind,delta,ms));}
static void drain(void){tab5_native_port_input_dispatch(ms);}
static void press(void){ms+=100;event(TAB5_UI_INPUT_PRESS,0);drain();}
static void rot(int delta){ms+=100;event(TAB5_UI_INPUT_ROT,delta);drain();}
static void tap(int x,int y){
    assert(tab5_native_port_post_touch(x,y,true,ms));drain();
    assert(tab5_native_port_post_touch(x,y,false,ms));drain();
}
static void page(unsigned p){tap(76,(int)p*120+90);nui_native_render();}
static void apply(const nb_frame_t *f){
    state.now_us=f->now_us;state.light_theme=f->light;
    for(unsigned j=0;j<2;j++)state.measurement.sensor[j]=(tab5_measure_sensor_t){
        .valid=true,.good=f->good,.timestamp_us=f->now_us,.decoded_x=f->x[j],.decoded_y=f->y[j],
        .filtered_x=f->x[j],.filtered_y=f->y[j],.x=f->x[j],.y=f->y[j],
        .roll_x=f->roll_x[j],.roll_y=f->roll_y[j],.calm_x=true,.calm_y=true,.roll_fill_s=20};
}
static nui_obj_t *keyboard(nui_obj_t *o){
    if(nui_obj_has_flag(o,NUI_OBJ_FLAG_HIDDEN))return NULL;
    if(nui_obj_check_type(o,&nui_keyboard_class))return o;
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++){nui_obj_t *k=keyboard(nui_obj_get_child(o,i));if(k)return k;}
    return NULL;
}
static void time_window(void){page(4);press();press();rot(9);press();nui_native_render();}
static void init(unsigned scene,bool light){
    callbacks=0;ms=1000;memset(&state,0,sizeof(state));
    assert(nui_native_init(NB_WIDTH,NB_HEIGHT,fb));nui_native_set_clock_cb(qpc_us);nui_native_set_flush_cb(flush,NULL);
    tab5_ui_init(action,NULL);tab5_native_port_input_reset();
    tab5_measure_config_default(&state.measurement.config);state.measurement.config.frequency_hz=10;
    state.light_theme=light;state.live_unfiltered=true;state.now_known=true;state.measurement.stable=true;
    state.brightness_pct=60;state.datetime_known=true;state.datetime=(tab5_ui_datetime_t){2026,10,22,14,32,5};
    state.timezone_known=true;state.timezone_minutes=180;state.clock.known=state.clock.time_valid=true;
    strcpy(state.clock.datetime,"22.10.2026 · 14:32:05");strcpy(state.firmware,"1.7.0-host-fixture");
    state.sd=(tab5_ui_medium_t){.known=true,.mounted=true};state.usb.known=true;
    state.network.ap_enabled=true;strcpy(state.network.ap_ssid,"KRENOMER-7F3A");strcpy(state.network.ap_password,"fixture123");
    nb_frame_t f=nb_frame(NB_IDLE,light,0);apply(&f);tab5_ui_update(&state);nui_native_render();
    if(scene==GRAPH){
        for(unsigned i=0;i<1200;i++){f=nb_frame(NB_GRAPH,light,i);f.now_us=880000000ULL+(uint64_t)i*100000;f.good=i+1;apply(&f);tab5_ui_update(&state);}
        page(2);tap(860,108); /* production 30 s selector */
    }else if(scene>=ROLL&&scene<=NETWORK)page(scene);
    if(scene==RECORD){state.recording=true;state.rec_known=true;state.rec_duration_known=true;state.sd.recording=true;strcpy(state.rec_file,"BENCH_001");tab5_ui_update(&state);}
    if(scene==FOCUS_IDLE||scene==FOCUS_MOVE){press();press();}
    if(scene==TRANSITION)press();
    if(scene==TIME_KEYS||scene==TIME_TZ||scene==SYNC_IDLE){
        time_window();
        if(scene==TIME_TZ){for(unsigned i=0;i<6;i++)press();}
        if(scene==SYNC_IDLE){state.time_sync_busy=true;strcpy(state.time_sync_status,"Синхронизация по WiFi…");tab5_ui_update(&state);}
    }
    nui_native_render();
}
typedef struct {double total,update,input,render,copy,render_copy;uint64_t pixels,rects,flushes,copied;unsigned renders,render_calls,update_calls;uint64_t prim_calls;} sample_t;
static sample_t samples[4000];
static int cmp(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
static void distribution(FILE *f,const char *key,const double *values,unsigned count,bool comma){
    double sorted[4000],sum=0;for(unsigned i=0;i<count;i++){sorted[i]=values[i];sum+=values[i];}qsort(sorted,count,sizeof(double),cmp);
    fprintf(f,"%s\"%s\":{\"p50\":%.6f,\"p95\":%.6f,\"max\":%.6f,\"mean\":%.6f}",comma?",":"",key,sorted[(count*50+99)/100-1],sorted[(count*95+99)/100-1],sorted[count-1],sum/count);
}
static void run(unsigned scene,bool light,unsigned warmup,unsigned frames,const char *out){
    double init_at=clock_us();init(scene,light);double setup_us=clock_us()-init_at;
    int key_x[2]={0},key_y[2]={0};
    if(scene==TIME_KEYS){
        nui_obj_t *k=keyboard(nui_screen_active());assert(k);nui_area_t areas[128];const char *texts[128];
        size_t count=nui_native_keyboard_get_keys(k,areas,texts,128);bool found[2]={false,false};
        for(unsigned i=0;i<count&&i<128;i++){
            int index=!strcmp(texts[i],NUI_SYMBOL_BACKSPACE)?0:!strcmp(texts[i],"6")?1:-1;
            if(index>=0){key_x[index]=(areas[i].x1+areas[i].x2)/2;key_y[index]=(areas[i].y1+areas[i].y2)/2;found[index]=true;}
        }
        assert(found[0]&&found[1]);
    }
    bool idle=scene==IDLE||scene==IDLE_UPDATE||scene==FOCUS_IDLE||scene==QUEUE_IDLE||scene==EMPTY_TOUCH;
    unsigned step_ms=scene==FOCUS_IDLE?10:100; /* focus poll stays below 15s timeout */
    uint64_t sum_pixels=0,sum_rects=0,sum_flushes=0,sum_copy=0,sum_renders=0,sum_prims=0;
    for(unsigned i=0;i<warmup+frames;i++){
        /* Numeric v1 input contract is reused; explicit rolling/record additions
         * and new geometry prevent declaring LVGL timing equivalence. */
        nb_frame_t f=nb_frame(scene==IDLE_UPDATE?NB_IDLE:scene==GRAPH?NB_GRAPH:NB_ANGLES,light,i);
        if(scene==ROLL)for(unsigned j=0;j<2;j++){f.roll_x[j]=.1f+.07f*sinf(i*.07f+j);f.roll_y[j]=.1f+.07f*cosf(i*.09f+j);}
        bool update=scene<=NETWORK||scene==IDLE_UPDATE||scene==FULL;
        if(update)apply(&f);
        if(scene==RECORD){state.rec_duration_s=i/10;state.sd.written=(uint64_t)i*100;}
        copy_us=0;flush_calls=copy_pixels=0;
        nui_native_stats_t before,after;nui_native_profile_snapshot(&before);
        double start=clock_us();
        if(update)tab5_ui_update(&state);
        double updated=clock_us();
        ms+=step_ms;nui_native_tick(step_ms);
        if(scene==FOCUS_MOVE||scene==TIME_TZ)event(TAB5_UI_INPUT_ROT,(i&1)?-1:1);
        if(scene==QUEUE_IDLE)event(TAB5_UI_INPUT_IDLE,0);
        if(scene==EMPTY_TOUCH||scene==TIME_KEYS){
            int x=scene==EMPTY_TOUCH?1278:key_x[i&1],y=scene==EMPTY_TOUCH?718:key_y[i&1];
            assert(tab5_native_port_post_touch(x,y,true,ms));assert(tab5_native_port_post_touch(x,y,false,ms));
        }
        if(scene==TRANSITION)event(TAB5_UI_INPUT_ROT,i%6==5?-5:1);
        drain();
        if(scene==FULL){nui_area_t a={0,0,NB_WIDTH-1,NB_HEIGHT-1};nui_native_invalidate(&a);}
        double input_at=clock_us();size_t dirty=nui_native_render();double rendered=clock_us();
        nui_native_profile_snapshot(&after);
        if(i>=warmup){
            sample_t *s=&samples[i-warmup];*s=(sample_t){.total=rendered-start,.update=updated-start,.input=input_at-updated,
                .render=rendered-input_at-copy_us,.copy=copy_us,.render_copy=rendered-input_at,
                .pixels=after.dirty_pixels-before.dirty_pixels,.rects=after.dirty_rects-before.dirty_rects,
                .flushes=flush_calls,.copied=copy_pixels,.renders=dirty!=0,.render_calls=1,.update_calls=update};
            for(unsigned p=0;p<NUI_PRIM_COUNT;p++)s->prim_calls+=after.primitive[p].calls-before.primitive[p].calls;
            sum_pixels+=s->pixels;sum_rects+=s->rects;sum_flushes+=s->flushes;sum_copy+=s->copied;sum_renders+=s->renders;sum_prims+=s->prim_calls;
            assert(s->pixels==s->copied);if(idle)assert(!dirty&&!s->pixels&&!s->flushes);
        }
    }
    if(scene==SYNC_IDLE)assert(sum_renders<=(frames+3)/4+1); /* 400ms caret blink */
    /* Verify the measured partial framebuffer against a forced full redraw,
     * and exact shared-contract RGB565 expansion. All outside timing. */
    memcpy(proof,fb,sizeof(fb));nui_area_t full={0,0,NB_WIDTH-1,NB_HEIGHT-1};nui_native_invalidate(&full);assert(nui_native_render());assert(!memcmp(proof,fb,sizeof(fb)));
    uint64_t checksum=0;for(unsigned i=0;i<NB_WIDTH*NB_HEIGHT;i++){assert(sink[i]==nb_rgb565_expand(fb[i]));checksum=checksum*33+sink[i];}
    assert(!callbacks);
    char path[2048];snprintf(path,sizeof(path),"%s/%s_%s.csv",out,names[scene],light?"light":"dark");FILE *csv=fopen(path,"w");assert(csv);
    fprintf(csv,"frame,total_us,update_us,input_us,render_us,copy_us,render_including_copy_us,dirty_pixels,dirty_rects,flush_calls,render_calls,rendered_frames,update_calls,primitive_calls\n");
    for(unsigned i=0;i<frames;i++){sample_t *s=&samples[i];fprintf(csv,"%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%llu,%llu,%llu,%u,%u,%u,%llu\n",i,s->total,s->update,s->input,s->render,s->copy,s->render_copy,(unsigned long long)s->pixels,(unsigned long long)s->rects,(unsigned long long)s->flushes,s->render_calls,s->renders,s->update_calls,(unsigned long long)s->prim_calls);}fclose(csv);
    snprintf(path,sizeof(path),"%s/%s_%s.json",out,names[scene],light?"light":"dark");FILE *json=fopen(path,"w");assert(json);
    fprintf(json,"{\"scene\":\"%s\",\"theme\":\"%s\",\"frames\":%u,\"warmup\":%u,\"ui_step_ms\":%u,\"qpc_frequency\":%llu,\"fine_counters\":%d,\"setup_us\":%.6f,\"idle_asserted\":%s,\"partial_equals_full\":true,\"checksum\":\"%llu\",\"timing_us\":{",names[scene],light?"light":"dark",frames,warmup,step_ms,(unsigned long long)frequency.QuadPart,NUI_PROFILE_FINE_COUNTERS,setup_us,idle?"true":"false",(unsigned long long)checksum);
    double values[4000];
    for(unsigned j=0;j<6;j++){
        for(unsigned i=0;i<frames;i++){sample_t *s=&samples[i];values[i]=j==0?s->total:j==1?s->update:j==2?s->input:j==3?s->render:j==4?s->copy:s->render_copy;}
        const char *keys[]={"total","update","input_queue_tick","render_excluding_copy","mock_copy","render_including_copy"};distribution(json,keys[j],values,frames,j!=0);
    }
    for(unsigned i=0;i<frames;i++)values[i]=(double)samples[i].pixels;
    fprintf(json,"},\"dirty_pixels\":{");distribution(json,"per_sample",values,frames,false);
    fprintf(json,"},\"totals\":{\"dirty_pixels\":%llu,\"dirty_rects\":%llu,\"flush_calls\":%llu,\"copied_pixels\":%llu,\"render_calls\":%u,\"rendered_frames\":%llu,\"update_calls\":%u,\"primitive_calls\":%llu}}\n",(unsigned long long)sum_pixels,(unsigned long long)sum_rects,(unsigned long long)sum_flushes,(unsigned long long)sum_copy,frames,(unsigned long long)sum_renders,(scene<=NETWORK||scene==IDLE_UPDATE||scene==FULL)?frames:0,(unsigned long long)sum_prims);fclose(json);
    printf("PASS %s %s frames=%u rendered=%llu dirty=%llu\n",names[scene],light?"light":"dark",frames,(unsigned long long)sum_renders,(unsigned long long)sum_pixels);
    tab5_native_port_input_close();tab5_ui_destroy();nui_native_deinit();nui_native_stats_t end;nui_native_profile_snapshot(&end);assert(!end.allocation_count&&!end.allocation_bytes);
}
int main(int argc,char **argv){
    assert(argc==4);QueryPerformanceFrequency(&frequency);unsigned warmup=strtoul(argv[1],NULL,10),frames=strtoul(argv[2],NULL,10);
    assert(frames>=100&&frames<=1000&&warmup<=100);setbuf(stdout,NULL);
    for(unsigned theme=0;theme<2;theme++)for(unsigned s=0;s<SCENES;s++)run(s,theme!=0,warmup,frames,argv[3]);
    return 0;
}
