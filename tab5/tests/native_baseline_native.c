/* Native adapter over explicit Hooke helper source; shared deterministic contract. */
#define main frozen_ui_suite_main
#ifndef NB_NATIVE_TEST
#error NB_NATIVE_TEST must name the explicit Hooke native test helper source
#endif
#include NB_NATIVE_TEST
#undef main
#include "native_baseline_contract.h"
static const char *artifact_dir;
/* Identical integer RGB565 expansion for both adapters; completion is timed. */
static void bench_flush_complete(const nui_area_t *a,const uint16_t *data,size_t stride,void *user){
    (void)user;
    double start=bench_now_ms();
    flushes++;flushed_pixels+=(uint64_t)nui_area_get_width(a)*nui_area_get_height(a);
    const uint16_t *row=data;
    for(int y=a->y1;y<=a->y2;y++){
        const uint16_t *src=row;
        for(int x=a->x1;x<=a->x2;x++)pixels[y*NB_WIDTH+x]=nb_rgb565_expand(*src++);
        row+=stride;
    }
    flush_time_ms+=bench_now_ms()-start;
}
#define NB_MAX_FRAMES 20000
static double totals[NB_MAX_FRAMES],updates[NB_MAX_FRAMES],renders[NB_MAX_FRAMES],copies[NB_MAX_FRAMES];
static uint64_t sample_flushes[NB_MAX_FRAMES],sample_pixels[NB_MAX_FRAMES],sample_refreshes[NB_MAX_FRAMES];
static int compare_double(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
static double percentile(double *a,unsigned n,unsigned numerator){
    qsort(a,n,sizeof(*a),compare_double);unsigned rank=(n*numerator+99)/100;return a[rank?rank-1:0];
}
static void apply_frame(tab5_ui_state_t *s,const nb_frame_t *f){
    s->now_us=f->now_us;s->light_theme=f->light;
    for(unsigned j=0;j<2;j++)s->measurement.sensor[j]=(tab5_measure_sensor_t){
        .valid=true,.good=f->good,.timestamp_us=f->now_us,.decoded_x=f->x[j],.decoded_y=f->y[j],
        .x=f->x[j],.y=f->y[j],.roll_x=f->roll_x[j],.roll_y=f->roll_y[j],.calm_x=true,.calm_y=true,.roll_fill_s=20};
}
static void run_scene(enum nb_scene scene,unsigned theme,unsigned warmup,unsigned frames){
    tab5_ui_init(action,NULL);
    tab5_ui_state_t s={.light_theme=theme,.live_unfiltered=true,.now_known=true,.measurement.stable=true};
    tab5_measure_config_default(&s.measurement.config);s.measurement.config.frequency_hz=10;
    s.datetime_known=true;s.datetime=(tab5_ui_datetime_t){2026,10,22,14,32,5};
    s.clock=(tab5_ui_clock_t){.known=true,.time_valid=true};strcpy(s.clock.datetime,"22.10.2026 · 14:32:05");
    strcpy(s.firmware,"1.6.1-esp32");strcpy(s.panel,"ST7123 · определён автоматически");
    s.sd=(tab5_ui_medium_t){.known=true,.mounted=true};s.usb.known=true;
    s.network.ap_enabled=true;strcpy(s.network.ap_ssid,"KRENOMER-7F3A");strcpy(s.network.ap_password,"test-pass-123");
    nb_frame_t f=nb_frame(scene,theme,0);apply_frame(&s,&f);tab5_ui_update(&s);pump();
    if(scene==NB_GRAPH){
        for(unsigned i=0;i<1200;i++){
            f=nb_frame(NB_GRAPH,theme,i);f.now_us=880000000ULL+(uint64_t)i*100000;f.good=i+1;
            apply_frame(&s,&f);tab5_ui_update(&s);
        }
        nav(1);click("30 с");pump();
    }
    if(scene==NB_MODAL){nav(3);tap(535,200);assert(find_label(nui_screen_active(),"Готово"));}
    uint64_t sum_flushes=0,sum_pixels=0,sum_refresh=0;
    for(unsigned i=0;i<warmup+frames;i++){
        if(i==warmup)nui_native_profile_reset();
        f=nb_frame(scene,theme,i);apply_frame(&s,&f);
        flushes=flushed_pixels=refreshes=0;flush_time_ms=0;
        double start=bench_now_ms();tab5_ui_update(&s);double middle=bench_now_ms();
        if(f.touch_x>=0)tap(f.touch_x,f.touch_y);else pump();
        double end=bench_now_ms();
        if(i>=warmup){unsigned k=i-warmup;updates[k]=middle-start;renders[k]=end-middle;totals[k]=end-start;copies[k]=flush_time_ms;
            sample_flushes[k]=flushes;sample_pixels[k]=flushed_pixels;sample_refreshes[k]=refreshes;
            sum_flushes+=flushes;sum_pixels+=flushed_pixels;sum_refresh+=refreshes;}
    }
    /* Raw frame samples are written only after the timed loop, before sorting. */
    char csv_path[1024];snprintf(csv_path,sizeof(csv_path),"%s/%s_%s_samples.csv",artifact_dir,nb_names[scene],theme?"light":"dark");
    FILE *csv=fopen(csv_path,"w");assert(csv);
    fprintf(csv,"frame,snapshot_us,total_ms,update_ms,pump_ms,flush_copy_ms,flushes,pixels,refreshes\n");
    for(unsigned k=0;k<frames;k++)fprintf(csv,"%u,%llu,%.9f,%.9f,%.9f,%.9f,%llu,%llu,%llu\n",k,
        (unsigned long long)(1000000000ULL+(uint64_t)(warmup+k)*100000),totals[k],updates[k],renders[k],copies[k],
        (unsigned long long)sample_flushes[k],(unsigned long long)sample_pixels[k],(unsigned long long)sample_refreshes[k]);
    fclose(csv);
    double total50=percentile(totals,frames,50),total95=percentile(totals,frames,95);
    double update50=percentile(updates,frames,50),update95=percentile(updates,frames,95);
    double render50=percentile(renders,frames,50),render95=percentile(renders,frames,95);
    double copy50=percentile(copies,frames,50),copy95=percentile(copies,frames,95);
    printf("{\"scene\":\"%s\",\"theme\":\"%s\",\"frames\":%u,\"warmup\":%u,\"total_median_ms\":%.6f,\"total_p95_ms\":%.6f,\"update_median_ms\":%.6f,\"update_p95_ms\":%.6f,\"render_median_ms\":%.6f,\"render_p95_ms\":%.6f,\"flush_copy_median_ms\":%.6f,\"flush_copy_p95_ms\":%.6f,\"flushes\":%llu,\"pixels\":%llu,\"refreshes\":%llu}\n",
           nb_names[scene],theme?"light":"dark",frames,warmup,total50,total95,update50,update95,render50,render95,copy50,copy95,
           (unsigned long long)sum_flushes,(unsigned long long)sum_pixels,(unsigned long long)sum_refresh);
    /* allocation_count is CURRENT LIVE blocks, not cumulative malloc calls.
     * These gauges cannot prove zero allocations per frame. Payload excludes
     * allocator headers/CRT overhead and the external display/plot/gauges. */
    nui_native_stats_t stats;nui_native_profile_snapshot(&stats);
    printf("MEMORY {\"scene\":\"%s\",\"theme\":\"%s\",\"native_logical_payload_current_bytes\":%llu,\"native_logical_payload_process_peak_bytes\":%llu,\"allocation_live_blocks\":%llu,\"native_allocation_failures_process\":%llu,\"allocator_headers_crt_overhead_bytes\":null,\"external_plot_payload_bytes\":757120,\"external_gauges_payload_bytes\":160000,\"external_framebuffer_payload_bytes\":%llu,\"host_rgb888_mock_sink_bytes\":%llu}\n",
           nb_names[scene],theme?"light":"dark",(unsigned long long)stats.allocation_bytes,
           (unsigned long long)stats.allocation_peak,(unsigned long long)stats.allocation_count,
           (unsigned long long)stats.allocation_failures,(unsigned long long)sizeof(framebuffer),(unsigned long long)sizeof(pixels));
    printf("PROFILE {\"scene\":\"%s\",\"theme\":\"%s\",\"frames\":%llu,\"dirty_rects\":%llu,\"dirty_pixels\":%llu,\"engine_render_us_excluding_flush\":%llu,\"primitives\":[",nb_names[scene],theme?"light":"dark",(unsigned long long)stats.frames,(unsigned long long)stats.dirty_rects,(unsigned long long)stats.dirty_pixels,(unsigned long long)stats.render_us);
    for(unsigned k=0;k<NUI_PRIM_COUNT;k++)printf("%s{\"id\":%u,\"calls\":%llu,\"us\":%llu,\"pixels\":%llu}",k?",":"",k,(unsigned long long)stats.primitive[k].calls,(unsigned long long)stats.primitive[k].us,(unsigned long long)stats.primitive[k].pixels);
    printf("]}\n");
    char path[1024];snprintf(path,sizeof(path),"%s/%s_%s.ppm",artifact_dir,nb_names[scene],theme?"light":"dark");
    FILE *file=fopen(path,"wb");assert(file);fprintf(file,"P6\n1280 720\n255\n");
    for(unsigned i=0;i<1280*720;i++){uint8_t rgb[3]={pixels[i]>>16,pixels[i]>>8,pixels[i]};assert(fwrite(rgb,1,3,file)==3);}fclose(file);
    tab5_ui_destroy();
}
static uint64_t qpc_us(void){
#ifdef _WIN32
    LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);
    return (uint64_t)((t.QuadPart/f.QuadPart)*1000000ULL+(t.QuadPart%f.QuadPart)*1000000ULL/f.QuadPart);
#else
    struct timespec t;timespec_get(&t,TIME_UTC);return (uint64_t)t.tv_sec*1000000ULL+t.tv_nsec/1000;
#endif
}
int main(int argc,char **argv){
    assert(argc==4);artifact_dir=argv[3];unsigned warmup=(unsigned)strtoul(argv[1],NULL,10),frames=(unsigned)strtoul(argv[2],NULL,10);
    assert(frames>=100&&frames<=NB_MAX_FRAMES&&warmup<=NB_MAX_FRAMES);
    setbuf(stdout,NULL);rgb565_mode=true;
    assert(nui_native_init(1280,720,framebuffer));
    nui_native_set_clock_cb(qpc_us);nui_native_set_flush_cb(bench_flush_complete,NULL);
    for(unsigned theme=0;theme<2;theme++)for(unsigned scene=0;scene<NB_SCENES;scene++)run_scene(scene,theme,warmup,frames);
    nui_native_deinit();return 0;
}
