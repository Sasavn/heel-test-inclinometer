#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x) do {if(!(x)){fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x);exit(91);}}while(0)
#define ESP_ERR_INVALID_ARG 0x102
typedef int esp_err_t;
typedef int mipi_dsi_phy_clock_source_t;
typedef void *esp_lcd_panel_handle_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef void *esp_lcd_dsi_bus_handle_t;
typedef void *esp_lcd_touch_handle_t;
typedef void lv_display_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_IDF_VERSION_VAL(a,b,c) ((a)*10000+(b)*100+(c))
#define ESP_IDF_VERSION 50500
#define CONFIG_BSP_LCD_USE_DMA2D 0
#define CONFIG_BSP_LCD_DPI_BUFFER_NUMS 2
#define LVGL_VERSION_MAJOR 9
#define MIPI_DSI_DPI_CLK_SRC_DEFAULT 0
#define LCD_COLOR_FMT_RGB565 16
#define LCD_RGB_ELEMENT_ORDER_RGB 0
#define GPIO_NUM_NC -1
#define BSP_LCD_RST -1
#define BSP_LCD_TOUCH_INT 23
#define BSP_FEATURE_TOUCH 1
#define BSP_FEATURE_LCD 2
#define GPIO_MODE_OUTPUT 1
#define GPIO_INTR_DISABLE 0
#define BIT64(x) (1ULL << (x))
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_RETURN_ON_ERROR(x,...) do { int e=(x); if(e) return e; } while(0)
#define BSP_ERROR_CHECK_RETURN_ERR(x) ESP_RETURN_ON_ERROR(x)
#define BSP_ERROR_CHECK_RETURN_NULL(x) do {if((x)!=0)return NULL;}while(0)
#define ESP_GOTO_ON_ERROR(x,label,...) do {ret=(x);if(ret)goto label;}while(0)
#define ESP_GOTO_ON_FALSE(x,e,label,...) do {if(!(x)){ret=(e);goto label;}}while(0)
typedef struct {int h_size,v_size,hsync_back_porch,hsync_pulse_width,hsync_front_porch,vsync_back_porch,vsync_pulse_width,vsync_front_porch;} timing_t;
typedef timing_t esp_lcd_video_timing_t;
typedef struct {int virtual_channel,dpi_clk_src,dpi_clock_freq_mhz,in_color_format,num_fbs; timing_t video_timing; struct {bool use_dma2d;} flags;} esp_lcd_dpi_panel_config_t;
typedef struct {int bus_id,num_data_lanes,phy_clk_src,lane_bit_rate_mbps;} esp_lcd_dsi_bus_config_t;
typedef struct {int virtual_channel,lcd_cmd_bits,lcd_param_bits;} esp_lcd_dbi_io_config_t;
typedef struct {int reset_gpio_num; struct {bool reset_active_high;} flags; int rgb_ele_order,bits_per_pixel;void *vendor_config;} esp_lcd_panel_dev_config_t;
typedef struct {int dev_addr;} esp_lcd_panel_io_i2c_config_t;
#define ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS 0x55
#define ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP 0x5d
#define ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG() { .dev_addr=0x55 }
#define ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() { .dev_addr=0x14 }
typedef struct {int x_max,y_max,rst_gpio_num,int_gpio_num;struct {int reset,interrupt;} levels;struct {bool swap_xy,mirror_x,mirror_y;} flags;} esp_lcd_touch_config_t;
typedef struct {int unused;} bsp_touch_config_t;
typedef struct {int mode,intr_type,pull_down_en,pull_up_en;uint64_t pin_bit_mask;} gpio_config_t;
typedef struct {int buffer_size;bool double_buffer;struct {bool buff_dma,buff_spiram,sw_rotate;} flags;} bsp_display_cfg_t;
typedef struct {void *io_handle,*panel_handle;int buffer_size,hres,vres;bool double_buffer,monochrome;struct {bool swap_xy,mirror_x,mirror_y;} rotation;struct {bool buff_dma,buff_spiram,swap_bytes,sw_rotate,full_refresh,direct_mode;} flags;} lvgl_port_display_cfg_t;
typedef struct {struct {bool avoid_tearing;} flags;} lvgl_port_display_dsi_cfg_t;
static int driver_active;
static int resetfail,initfail,onfail;
static int fw,gt,readfail,iofail,touchfail,probes,reads,ios,dels,panels,touches,gpio_calls,low_calls,features,delays,lane;
static int panel_kind,touch_kind,last_addr;
static esp_lcd_touch_config_t touch_cfg;
static esp_lcd_dpi_panel_config_t captured_dpi;
static const void *selected_commands;
static int selected_count;
static int bsp_feature_enable(int f,bool b){(void)f;(void)b;features++;return 0;}
static void vTaskDelay(int t){if(driver_active){assert(t>=0);return;}assert(t==500);delays++;}
static void *bsp_i2c_get_handle(void){return (void*)1;}
static int bsp_i2c_init(void){return 0;}
static int i2c_master_probe(void *b,int a,int t){assert(b&&t==100);probes++;return a==0x55?(gt?-1:0):(gt?0:-1);}
static int esp_lcd_new_panel_io_i2c(void *b,const esp_lcd_panel_io_i2c_config_t *c,void **o){assert(b);ios++;last_addr=c->dev_addr;if(iofail)return -1;*o=(void*)2;return 0;}
static int esp_lcd_panel_io_rx_param(void *h,int r,void *d,size_t n){if(driver_active){assert(h&&r>=0&&r<=2&&n==1);*(uint8_t*)d=0;return ESP_OK;}assert(h&&r==0&&n==1);reads++;*(uint8_t*)d=fw;return readfail?-1:0;}
static int esp_lcd_panel_io_del(void *h){assert(h);dels++;return 0;}
static int bsp_display_brightness_init(void){return 0;}
static int bsp_enable_dsi_phy_power(void){return 0;}
static int esp_lcd_new_dsi_bus(const esp_lcd_dsi_bus_config_t *c,void **o){lane=c->lane_bit_rate_mbps;assert(c->num_data_lanes==2);*o=(void*)3;return 0;}
static int esp_lcd_new_panel_io_dbi(void *b,const esp_lcd_dbi_io_config_t *c,void **o){assert(b&&c->lcd_cmd_bits==8&&c->lcd_param_bits==8);*o=(void*)4;return 0;}
static int esp_lcd_panel_reset(void *p){assert(p);return resetfail?-1:0;}
static int esp_lcd_panel_init(void *p){assert(p);return initfail?-1:0;}
static int esp_lcd_panel_invert_color(void *p,bool b){assert(p&&!b);return 0;}
static int esp_lcd_panel_mirror(void *p,bool x,bool y){assert(p&&!x&&!y);return 0;}
static int esp_lcd_panel_disp_on_off(void *p,bool b){assert(p&&b);return onfail?-1:0;}
static void bsp_display_delete(void){}
static int gpio_config(const gpio_config_t *c){assert(c->mode==GPIO_MODE_OUTPUT&&c->pull_up_en==1&&c->pin_bit_mask==BIT64(23));gpio_calls++;return 0;}
static int gpio_set_level(int p,int l){assert(p==23&&l==0);low_calls++;return 0;}
static int touch_capture(int k,void *io,const esp_lcd_touch_config_t *c,void **o){assert(io);touches++;touch_kind=k;touch_cfg=*c;if(touchfail)return -1;*o=(void*)5;return 0;}
static int esp_lcd_touch_new_i2c_gt911(void *i,const esp_lcd_touch_config_t*c,void**o){return touch_capture(1,i,c,o);}
static int esp_lcd_touch_new_i2c_st7123(void *i,const esp_lcd_touch_config_t*c,void**o){return touch_capture(2,i,c,o);}
static void *lvgl_port_add_disp_dsi(const lvgl_port_display_cfg_t*c,const lvgl_port_display_dsi_cfg_t*d){assert(c->hres==720&&c->vres==1280);(void)d;return (void*)6;}
