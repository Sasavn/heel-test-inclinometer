
/* DRIVER_FIXTURE */
static int esp_lcd_new_panel_st7123(void *io,const esp_lcd_panel_dev_config_t *c,void **out){
 const st7123_vendor_config_t *v=c->vendor_config; assert(io&&v->mipi_config.dsi_bus);
 captured_dpi=*v->mipi_config.dpi_config;selected_commands=v->init_cmds;selected_count=v->init_cmds_size;panel_kind=2;panels++;*out=(void*)7;return 0;
}
static int esp_lcd_new_panel_ili9881c(void *io,const esp_lcd_panel_dev_config_t *c,void **out){
 const ili9881c_vendor_config_t *v=c->vendor_config;assert(io&&v->mipi_config.dsi_bus&&v->mipi_config.lane_num==2);
 captured_dpi=*v->mipi_config.dpi_config;selected_commands=v->init_cmds;selected_count=v->init_cmds_size;panel_kind=1;panels++;*out=(void*)7;return 0;
}
static bsp_lcd_handles_t disp_handles;
esp_err_t bsp_display_new_with_handles(const bsp_display_config_t*,bsp_lcd_handles_t*);
/* PRODUCTION_FUNCTIONS */
static void stream(int cmd,const void *data,size_t n,unsigned delay,size_t available){
 printf("%08x:%08x:%08x:%08x:",(unsigned)cmd,(unsigned)n,delay,(unsigned)available);
 for(size_t j=0;j<available;j++)printf("%02x",((const unsigned char*)data)[j]);puts("");
}
int main(int argc,char **argv){
 assert(argc==2);const char *s=argv[1];fw=1;
 test_audit_tables();
 int driver_case=!strncmp(s,"driver",6);
 int bad_e8=!strcmp(s,"e8changed")||!strcmp(s,"a3changed");
 if(driver_case||bad_e8)fw=3;
 if(!strcmp(s,"driverst1"))fw=1;
 resetfail=!strcmp(s,"resetfail");initfail=!strcmp(s,"initfail");onfail=!strcmp(s,"onfail");
 gt=!strcmp(s,"gt911")||!strcmp(s,"driverili");if(!strcmp(s,"fw3"))fw=3;
 readfail=!strcmp(s,"readfail");iofail=!strcmp(s,"iofail");if(!strcmp(s,"unknown"))fw=9;
 touchfail=!strcmp(s,"touchfail")||!strcmp(s,"gtfail");if(!strcmp(s,"gtfail"))gt=1;
 int invalid=readfail||iofail||fw==9;
 int board=bsp_get_board_version();assert(board==(invalid?0:gt?1:fw==1?3:2));
 int np=probes,nr=reads,ni=ios,nd=delays;
 if(!invalid){fw=99;gt=!gt;assert(bsp_get_board_version()==board);assert(probes==np&&reads==nr&&ios==ni&&delays==nd);}
 if(invalid){
  bsp_display_config_t c={.dsi_bus={.lane_bit_rate_mbps=1000}};bsp_lcd_handles_t h={0};
  assert(bsp_display_new_with_handles(&c,&h)==ESP_ERR_NOT_SUPPORTED);assert(panels==0&&lane==0);
  void *t=NULL;assert(bsp_touch_new(NULL,&t)==ESP_ERR_NOT_SUPPORTED);assert(touches==0);assert(last_addr==0x55);puts("CLOSED");return 0;
 }
 bsp_display_cfg_t c={.buffer_size=720};
 if(bad_e8){assert(!bsp_display_lcd_init(&c));assert(panels==0);assert(!test_panel_ready());puts("REJECTED");return 0;}
 assert(bsp_display_lcd_init(&c));
 assert(probes==np&&reads==nr);assert(lane==(board==3?965:1000));assert(panels==1&&panel_kind==(board==1?1:2));
 const timing_t *v=&captured_dpi.video_timing;
 assert(captured_dpi.dpi_clock_freq_mhz==(board==1?60:70));assert(v->h_size==720&&v->v_size==1280);
 assert(v->hsync_back_porch==(board==1?140:40)&&v->hsync_pulse_width==(board==1?40:2)&&v->hsync_front_porch==40);
 assert(v->vsync_back_porch==(board==1?20:board==3?24:8));assert(v->vsync_pulse_width==(board==1?4:board==3?20:2));assert(v->vsync_front_porch==(board==1?20:board==3?200:220));
 if(PANEL_WRAPPED && board==2){
  assert(selected_commands!=(const void*)disp_init_data_st7123);
  const st7123_lcd_init_cmd_t *actual=selected_commands;
  assert(selected_count==sizeof(disp_init_data_st7123)/sizeof(disp_init_data_st7123[0]));
  int corrected=0;
  for(int i=0;i<selected_count;i++){
   const st7123_lcd_init_cmd_t *original=&disp_init_data_st7123[i];
   assert(actual[i].cmd==original->cmd && actual[i].data==original->data && actual[i].delay_ms==original->delay_ms);
   if(original->cmd==0xe8){assert(original->data_bytes==15&&actual[i].data_bytes==14&&sizes_st3[i]==14);corrected++;}
   else if(original->cmd==0xa3){assert(original->data_bytes==40&&actual[i].data_bytes==39&&sizes_st3[i]==39);corrected++;}
   else assert(actual[i].data_bytes==original->data_bytes);
   assert(actual[i].data_bytes<=sizes_st3[i]);
  }
  assert(corrected==2);
 }else assert(selected_commands==(board==1?(const void*)disp_init_data_ili9881c:board==3?(const void*)disp_init_data_st7121:(const void*)disp_init_data_st7123));
 void *t=NULL;assert(bsp_touch_new(NULL,&t)==(touchfail?-1:0));assert(touches==1&&touch_kind==(board==1?1:2));
 assert(touch_cfg.x_max==720&&touch_cfg.y_max==1280&&touch_cfg.rst_gpio_num==GPIO_NUM_NC);
 assert(!touch_cfg.flags.swap_xy&&!touch_cfg.flags.mirror_x&&!touch_cfg.flags.mirror_y);
 assert(touch_cfg.int_gpio_num==(board==1?GPIO_NUM_NC:23));assert(gpio_calls==(board==1)&&low_calls==(board==1));
 assert(last_addr==(board==1?0x5d:0x55));assert(probes==np&&reads==nr&&delays==nd);assert(dels==(board==1?0:1));
 if(driver_case)return test_driver_loop(selected_commands,selected_count,board,s);
 for(int i=0;i<selected_count;i++){
  if(board==1){const ili9881c_lcd_init_cmd_t*a=selected_commands;stream(a[i].cmd,a[i].data,a[i].data_bytes,a[i].delay_ms,sizes_ili[i]);}
  else {const st7123_lcd_init_cmd_t*a=selected_commands;stream(a[i].cmd,a[i].data,a[i].data_bytes,a[i].delay_ms,board==3?sizes_st1[i]:sizes_st3[i]);}
 }
 return 0;
}
