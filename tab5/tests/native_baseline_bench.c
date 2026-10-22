/* Adapter to immutable 369c7fc test helpers; only frozen include paths used. */
#define main frozen_ui_suite_main
#ifndef NB_FROZEN_TEST
#error NB_FROZEN_TEST must name the immutable 369c7fc test source
#endif
#include NB_FROZEN_TEST
#undef main
#include "native_baseline_contract.h"
static const char *artifact_dir;
/* Identical integer RGB565 expansion for both adapters; completion is timed. */
static void bench_flush_complete(lv_display_t *d,const lv_area_t *a,uint8_t *data){
    double start=bench_now_ms();
    flushes++;flushed_pixels+=(uint64_t)lv_area_get_width(a)*lv_area_get_height(a);
    if(lv_display_flush_is_last(d))refreshes++;
    const uint16_t *src=(const uint16_t*)data;
    for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)
        pixels[y*NB_WIDTH+x]=nb_rgb565_expand(*src++);
    lv_display_flush_ready(d);
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
    if(scene==NB_MODAL){nav(3);tap(535,200);assert(find_label(lv_screen_active(),"Готово"));}
    uint64_t sum_flushes=0,sum_pixels=0,sum_refresh=0;
    for(unsigned i=0;i<warmup+frames;i++){
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
    /* Outside timing. TLSF monitor counts usable block sizes, not exact
     * requested payload. max_used is process-wide since lv_init, not per scene. */
    lv_mem_monitor_t mem;lv_mem_monitor(&mem);
    printf("MEMORY {\"scene\":\"%s\",\"theme\":\"%s\",\"lvgl_arena_reserved_bytes\":8388608,\"lvgl_current_used_block_bytes\":%llu,\"lvgl_process_peak_used_block_bytes\":%llu,\"lvgl_live_blocks\":%llu,\"external_plot_payload_bytes\":757120,\"external_gauges_payload_bytes\":160000,\"native_display_framebuffer_payload_bytes\":1843200,\"host_rgb888_mock_sink_bytes\":%llu,\"host_draw_buffer_static_bytes\":%llu,\"host_draw_buffer_active_rgb565_bytes\":%llu}\n",
           nb_names[scene],theme?"light":"dark",(unsigned long long)(mem.total_size-mem.free_size),
           (unsigned long long)mem.max_used,(unsigned long long)mem.used_cnt,
           (unsigned long long)sizeof(pixels),(unsigned long long)sizeof(drawbuf),(unsigned long long)sizeof(drawbuf)/2);
    char path[1024];snprintf(path,sizeof(path),"%s/%s_%s.ppm",artifact_dir,nb_names[scene],theme?"light":"dark");
    FILE *file=fopen(path,"wb");assert(file);fprintf(file,"P6\n1280 720\n255\n");
    for(unsigned i=0;i<1280*720;i++){uint8_t rgb[3]={pixels[i]>>16,pixels[i]>>8,pixels[i]};assert(fwrite(rgb,1,3,file)==3);}fclose(file);
    tab5_ui_destroy();
}
int main(int argc,char **argv){
    assert(argc==4);artifact_dir=argv[3];unsigned warmup=(unsigned)strtoul(argv[1],NULL,10),frames=(unsigned)strtoul(argv[2],NULL,10);
    assert(frames>=100&&frames<=NB_MAX_FRAMES&&warmup<=NB_MAX_FRAMES);
    setbuf(stdout,NULL);rgb565_mode=true;lv_init();display=lv_display_create(1280,720);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_flush_cb(display,bench_flush_complete);
    lv_display_set_buffers(display,drawbuf,NULL,sizeof(drawbuf)/2,LV_DISPLAY_RENDER_MODE_PARTIAL);
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(touch,read_touch);
    for(unsigned theme=0;theme<2;theme++)for(unsigned scene=0;scene<NB_SCENES;scene++)run_scene(scene,theme,warmup,frames);
    lv_display_delete(display);lv_deinit();return 0;
}
