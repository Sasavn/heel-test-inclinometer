/* Production driver types, defaults and init functions are inserted verbatim. */
typedef struct esp_lcd_panel_t {void *user_data;} esp_lcd_panel_t;
#define LCD_CMD_MADCTL 0x36
#define LCD_CMD_COLMOD 0x3a
#define LCD_CMD_SLPOUT 0x11
/* DRIVER_PRODUCTION_TYPE */
/* DRIVER_PRODUCTION_DEFAULT */
/* ILI_PRODUCTION */
static unsigned tx_index,tx_bounds_rejections,dpi_init_calls,preamble;
static int tx_board,rejected_cmd;
static size_t expected_length(unsigned i){
 if(tx_board==1)return disp_init_data_ili9881c[i].data_bytes;
 if(tx_board==3)return disp_init_data_st7121[i].data_bytes;
 int cmd=disp_init_data_st7123[i].cmd;
 return cmd==0xa3?39:cmd==0xe8?14:disp_init_data_st7123[i].data_bytes;
}
static void original_record(int board,unsigned i,int *cmd,const void **data,size_t *n,size_t *available){
 if(board==1){const ili9881c_lcd_init_cmd_t *a=&disp_init_data_ili9881c[i];*cmd=a->cmd;*data=a->data;*n=a->data_bytes;*available=sizes_ili[i];}
 else {const st7123_lcd_init_cmd_t *a=board==3?&disp_init_data_st7121[i]:&disp_init_data_st7123[i];*cmd=a->cmd;*data=a->data;*n=a->data_bytes;*available=board==3?sizes_st1[i]:sizes_st3[i];}
}
static void test_audit_tables(void){
 assert(sizeof(sizes_ili)/sizeof(*sizes_ili)==sizeof(disp_init_data_ili9881c)/sizeof(*disp_init_data_ili9881c));
 assert(sizeof(sizes_st3)/sizeof(*sizes_st3)==sizeof(disp_init_data_st7123)/sizeof(*disp_init_data_st7123));
 assert(sizeof(sizes_st1)/sizeof(*sizes_st1)==sizeof(disp_init_data_st7121)/sizeof(*disp_init_data_st7121));
 unsigned violations=0,a3=0,e8=0;
 for(int board=1;board<=3;board++){
  size_t count=board==1?sizeof(sizes_ili)/sizeof(*sizes_ili):board==2?sizeof(sizes_st3)/sizeof(*sizes_st3):sizeof(sizes_st1)/sizeof(*sizes_st1);
  for(unsigned i=0;i<count;i++){
   int cmd;const void *data;size_t n,available;original_record(board,i,&cmd,&data,&n,&available);
   if(n>available){
    fprintf(stderr,"RAW_BOUNDS board=%d index=%u cmd=%02x declared=%zu literal=%zu\n",board,i,cmd,n,available);
    violations++;if(board==2&&cmd==0xa3&&n==40&&available==39)a3++;if(board==2&&cmd==0xe8&&n==15&&available==14)e8++;
   }
  }
 }
 assert(violations==2&&a3==1&&e8==1);
}
static int esp_lcd_panel_io_tx_param(void *io,int cmd,const void *data,size_t bytes){
 assert(io);
 if(tx_board==1&&preamble<6){
  static const int commands[]={0xff,0xb7,0xff,0x11,0x36,0x3a};
  static const size_t lengths[]={3,1,3,0,1,1};
  static const unsigned char payload[][3]={{0x98,0x81,1},{3},{0x98,0x81,0},{0},{0},{0x55}};
  assert(cmd==commands[preamble]&&bytes==lengths[preamble]);
  if(bytes)assert(data&&!memcmp(data,payload[preamble],bytes));
  preamble++;
 }else{
  int original_cmd;const void *original_data;size_t original_n,available;
  original_record(tx_board,tx_index,&original_cmd,&original_data,&original_n,&available);
  assert(cmd==original_cmd&&data==original_data);
  if(bytes>available){rejected_cmd=cmd;tx_bounds_rejections++;return ESP_FAIL;}
  assert(bytes==expected_length(tx_index));
  tx_index++;
 }
 printf("TX:%08x:%08x:",(unsigned)cmd,(unsigned)bytes);
 for(size_t i=0;i<bytes;i++)printf("%02x",((const uint8_t*)data)[i]);puts("");
 return ESP_OK;
}
/* DRIVER_PRODUCTION_INIT */
/* ILI_PRODUCTION_INIT */
static int test_dpi_init(esp_lcd_panel_t *p){assert(p);dpi_init_calls++;return ESP_OK;}
static int test_driver_loop(const void *commands,int count,int board,const char *scenario){
 tx_board=board;
 bool raw=!strcmp(scenario,"drivere8raw")||!strcmp(scenario,"drivera3raw");
 int target=raw?(!strcmp(scenario,"drivera3raw")?0xa3:0xe8):0;
 if(raw){
  const st7123_lcd_init_cmd_t *a=commands;unsigned i=0;
  while(i<(unsigned)count&&a[i].cmd!=target)i++;assert(i<(unsigned)count);
  tx_index=i;commands=a+i;count=1;
 }
 unsigned start=tx_index;
 driver_active=1;int result;
 if(board==1){
  ili9881c_panel_t driver={.io=(void*)2,.init_cmds=commands,.init_cmds_size=count,.lane_num=2,.colmod_val=0x55,.init=test_dpi_init};
  esp_lcd_panel_t display={.user_data=&driver};result=panel_ili9881c_init(&display);
 }else{
  st7123_panel_t driver={.io=(void*)2,.init_cmds=commands,.init_cmds_size=count,.init=test_dpi_init};
  esp_lcd_panel_t display={.user_data=&driver};result=panel_st7123_init(&display);
 }
 driver_active=0;
 if(raw){assert(result==ESP_FAIL&&tx_bounds_rejections==1&&dpi_init_calls==0&&rejected_cmd==target);printf("BOUNDS_REJECT_%02X\n",target);}
 else {assert(result==ESP_OK&&tx_bounds_rejections==0&&dpi_init_calls==1);assert(tx_index==start+(unsigned)count);if(board==1)assert(preamble==6);}
 return 0;
}
