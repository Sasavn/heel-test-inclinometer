/* Native LVGL screenshot regression; no production UI integration needed. */
#define main existing_ui_suite_main
#include "test_ui.c"
#undef main
#include "tab5_ui_graph.h"
static tab5_ui_graph_t graph_fixture;
static int32_t raw_points[1200],render_points[1200];
static void save_pixels(const char *name){
 char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",output,name);
 FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1280 720\n255\n");
 for(unsigned i=0;i<1280*720;i++){uint8_t rgb[3]={pixels[i]>>16,pixels[i]>>8,pixels[i]};fwrite(rgb,1,3,f);}fclose(f);
}
static unsigned blue_pixels(void){unsigned n=0;for(unsigned i=0;i<1280*720;i++)if((pixels[i]&255)>180&&((pixels[i]>>16)&255)<100)n++;return n;}
int main(int argc,char **argv){
 assert(argc==2);output=argv[1];lv_init();display=lv_display_create(1280,720);
 lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);lv_display_set_flush_cb(display,flush);
 lv_display_set_buffers(display,drawbuf,NULL,sizeof(drawbuf),LV_DISPLAY_RENDER_MODE_PARTIAL);
 tab5_ui_state_t s={.now_known=true};s.measurement.config.frequency_hz=5;s.measurement.config.gap_ms=15;
 for(unsigned tick=0;tick<300;tick++){
  s.now_us=1000000ULL+tick*100000ULL;
  for(unsigned k=0;k<2;k++)if(!(tick%2)){
   s.measurement.sensor[k].valid=true;s.measurement.sensor[k].good++;
   s.measurement.sensor[k].timestamp_us=s.now_us;
   s.measurement.sensor[k].x=2*sinf(tick*.05f);
  }
  tab5_ui_graph_sample(&graph_fixture,&s);
 }
 tab5_ui_graph_read(&graph_fixture,30,0,raw_points);
 tab5_ui_graph_read_render(&graph_fixture,30,0,render_points);
 for(unsigned i=1;i<299;i++)assert(render_points[i]!=LV_CHART_POINT_NONE);
 assert(render_points[299]==LV_CHART_POINT_NONE); /* Unmeasured tail stays empty. */
 lv_obj_t *chart=lv_chart_create(lv_screen_active());lv_obj_set_pos(chart,100,100);lv_obj_set_size(chart,1080,520);
 lv_chart_set_type(chart,LV_CHART_TYPE_LINE);lv_chart_set_point_count(chart,300);
 lv_chart_set_axis_range(chart,LV_CHART_AXIS_PRIMARY_Y,-250,250);
 lv_obj_set_style_size(chart,0,0,LV_PART_INDICATOR);lv_obj_set_style_line_width(chart,3,LV_PART_ITEMS);
 lv_chart_series_t *series=lv_chart_add_series(chart,lv_color_hex(0x0066FF),LV_CHART_AXIS_PRIMARY_Y);
 lv_chart_set_series_ext_y_array(chart,series,raw_points);lv_chart_refresh(chart);pump();
 unsigned sparse=blue_pixels();save_pixels("graph_5hz_raw");
 lv_chart_set_series_ext_y_array(chart,series,render_points);lv_chart_refresh(chart);pump();
 unsigned smooth=blue_pixels();save_pixels("graph_5hz_render");assert(smooth>sparse+1000);
 printf("5Hz native LVGL: raw blue pixels=%u render blue pixels=%u; bounded interiors filled, tail empty: PASS\n",sparse,smooth);
 lv_display_delete(display);lv_deinit();return 0;
}
