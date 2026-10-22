#include "tab5_panel.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7123.h"
#include "esp_lcd_ili9881c.h"
#include "esp_lcd_touch_st7123.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* GNU ld wrappers observe the real BSP constructor boundary. Vendor files in
 * managed_components remain byte-for-byte unchanged. All writes/reset/init
 * remain in the official BSP/drivers; these wrappers only validate/copy config. */
static struct {
    const char *name;
    unsigned lane, lanes, pclk, hbp, hpw, hfp, vbp, vpw, vfp;
    bool display, reset, initialized, enabled, touch;
} panel;
static st7123_lcd_init_cmd_t st7123_commands[128];
static st7123_vendor_config_t st7123_config;
void tab5_panel_observer_reset(void){memset(&panel,0,sizeof(panel));}

esp_err_t __real_esp_lcd_new_dsi_bus(const esp_lcd_dsi_bus_config_t *, esp_lcd_dsi_bus_handle_t *);
esp_err_t __real_esp_lcd_new_panel_st7123(esp_lcd_panel_io_handle_t, const esp_lcd_panel_dev_config_t *, esp_lcd_panel_handle_t *);
esp_err_t __real_esp_lcd_new_panel_ili9881c(esp_lcd_panel_io_handle_t, const esp_lcd_panel_dev_config_t *, esp_lcd_panel_handle_t *);
esp_err_t __real_esp_lcd_touch_new_i2c_st7123(esp_lcd_panel_io_handle_t, const esp_lcd_touch_config_t *, esp_lcd_touch_handle_t *);
esp_err_t __real_esp_lcd_touch_new_i2c_gt911(esp_lcd_panel_io_handle_t, const esp_lcd_touch_config_t *, esp_lcd_touch_handle_t *);
esp_err_t __real_esp_lcd_panel_reset(esp_lcd_panel_handle_t);
esp_err_t __real_esp_lcd_panel_init(esp_lcd_panel_handle_t);
esp_err_t __real_esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t,bool);

esp_err_t __wrap_esp_lcd_new_dsi_bus(const esp_lcd_dsi_bus_config_t *cfg, esp_lcd_dsi_bus_handle_t *out) {
    if(!cfg)return ESP_ERR_INVALID_ARG;
    panel.lane=cfg->lane_bit_rate_mbps; panel.lanes=cfg->num_data_lanes;
    return __real_esp_lcd_new_dsi_bus(cfg,out);
}
static bool timing(const esp_lcd_dpi_panel_config_t *cfg, unsigned model) {
    if(!cfg)return false;
    const esp_lcd_video_timing_t *v=&cfg->video_timing;
    panel.pclk=cfg->dpi_clock_freq_mhz;
    panel.hbp=v->hsync_back_porch;panel.hpw=v->hsync_pulse_width;panel.hfp=v->hsync_front_porch;
    panel.vbp=v->vsync_back_porch;panel.vpw=v->vsync_pulse_width;panel.vfp=v->vsync_front_porch;
    if(v->h_size!=720 || v->v_size!=1280 || panel.lanes!=2)return false;
    if(model==9881)return panel.lane==1000 && panel.pclk==60 && panel.hbp==140 && panel.hpw==40 && panel.hfp==40 && panel.vbp==20 && panel.vpw==4 && panel.vfp==20;
    if(panel.pclk!=70 || panel.hbp!=40 || panel.hpw!=2 || panel.hfp!=40)return false;
    return model==7121 ? panel.lane==965 && panel.vbp==24 && panel.vpw==20 && panel.vfp==200 :
                        panel.lane==1000 && panel.vbp==8 && panel.vpw==2 && panel.vfp==220;
}
static void report(esp_err_t e) {
    panel.display=e==ESP_OK;
    ESP_LOGI("Tab5 panel", "%s DSI=%uMb/s PCLK=%uMHz H=%u/%u/%u V=%u/%u/%u: %s",
             panel.name?panel.name:"unknown",panel.lane,panel.pclk,panel.hbp,panel.hpw,panel.hfp,
             panel.vbp,panel.vpw,panel.vfp,esp_err_to_name(e));
}
esp_err_t __wrap_esp_lcd_new_panel_st7123(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *cfg, esp_lcd_panel_handle_t *out) {
    if(!cfg || !cfg->vendor_config)return ESP_ERR_INVALID_ARG;
    const st7123_vendor_config_t *v=cfg->vendor_config;
    if(!v->init_cmds || v->init_cmds_size<3)return ESP_ERR_INVALID_ARG;
    const uint8_t *id=v->init_cmds[0].data;
    if(v->init_cmds[0].cmd!=0x60 || v->init_cmds[0].data_bytes!=3 || !id || id[0]!=0x71 || (id[1]!=0x21 && id[1]!=0x23))return ESP_ERR_NOT_SUPPORTED;
    unsigned model=id[1]==0x21?7121:7123;panel.name=model==7121?"ST7121":"ST7123";
    if(!timing(v->mipi_config.dpi_config,model)){report(ESP_ERR_NOT_SUPPORTED);return ESP_ERR_NOT_SUPPORTED;}
    esp_lcd_panel_dev_config_t checked=*cfg;
    if(model==7123) {
        /* BSP 1.3.2 A3/E8 lengths include the command byte by mistake.
         * M5GFX len(cmd+parameters)=40/15, i.e. 39/14 parameters.
         * Correct only metadata in our tracked adapter, never registry files.
         * All payload bytes, other commands and timing remain untouched. */
        static const uint8_t e8[]={0x20,0x6f,0x04,0x97,0x97,0x3e,0x04,0xdc,0xdc,0x3e,0x06,0xfa,0x26,0x3e};
        static const uint8_t a3[]={0x80,0x01,0x88,0x30,0x05,0x00,0x00,0x00,0x00,0x00,
            0x46,0x00,0x00,0x1e,0x5c,0x1e,0x80,0x00,0x4f,0x05,0x00,0x00,0x00,0x00,
            0x00,0x46,0x00,0x00,0x1e,0x5c,0x1e,0x80,0x00,0x6f,0x58,0x00,0x00,0x00,0xff};
        if(v->init_cmds_size>128)return ESP_ERR_NOT_SUPPORTED;
        memcpy(st7123_commands,v->init_cmds,v->init_cmds_size*sizeof(*st7123_commands));
        for(unsigned i=0;i<v->init_cmds_size;i++)if(st7123_commands[i].cmd==0xe8 || st7123_commands[i].cmd==0xa3) {
            const uint8_t *expected=st7123_commands[i].cmd==0xe8?e8:a3;
            size_t count=st7123_commands[i].cmd==0xe8?sizeof(e8):sizeof(a3);
            if(st7123_commands[i].data_bytes!=count && st7123_commands[i].data_bytes!=count+1)return ESP_ERR_NOT_SUPPORTED;
            if(!st7123_commands[i].data || memcmp(st7123_commands[i].data,expected,count))return ESP_ERR_NOT_SUPPORTED;
            st7123_commands[i].data_bytes=count;
        }
        st7123_config=*v;st7123_config.init_cmds=st7123_commands;checked.vendor_config=&st7123_config;
    }
    esp_err_t e=__real_esp_lcd_new_panel_st7123(io,&checked,out);report(e);return e;
}
esp_err_t __wrap_esp_lcd_new_panel_ili9881c(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *cfg, esp_lcd_panel_handle_t *out) {
    if(!cfg || !cfg->vendor_config)return ESP_ERR_INVALID_ARG;
    const ili9881c_vendor_config_t *v=cfg->vendor_config;panel.name="ILI9881C";
    if(!timing(v->mipi_config.dpi_config,9881)){report(ESP_ERR_NOT_SUPPORTED);return ESP_ERR_NOT_SUPPORTED;}
    esp_err_t e=__real_esp_lcd_new_panel_ili9881c(io,cfg,out);report(e);return e;
}
static bool touch_config(const esp_lcd_touch_config_t *cfg, bool gt) {
    return panel.display && cfg && cfg->x_max==720 && cfg->y_max==1280 && cfg->rst_gpio_num==-1 &&
           !cfg->flags.swap_xy && !cfg->flags.mirror_x && !cfg->flags.mirror_y &&
           panel.name && (gt?!strcmp(panel.name,"ILI9881C"):!strncmp(panel.name,"ST712",5));
}
esp_err_t __wrap_esp_lcd_touch_new_i2c_st7123(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *cfg, esp_lcd_touch_handle_t *out) {
    if(!touch_config(cfg,false))return ESP_ERR_NOT_SUPPORTED;
    esp_err_t e=__real_esp_lcd_touch_new_i2c_st7123(io,cfg,out);panel.touch=e==ESP_OK;return e;
}
esp_err_t __wrap_esp_lcd_touch_new_i2c_gt911(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *cfg, esp_lcd_touch_handle_t *out) {
    if(!touch_config(cfg,true))return ESP_ERR_NOT_SUPPORTED;
    esp_err_t e=__real_esp_lcd_touch_new_i2c_gt911(io,cfg,out);panel.touch=e==ESP_OK;return e;
}
esp_err_t __wrap_esp_lcd_panel_reset(esp_lcd_panel_handle_t handle) {
    esp_err_t e=__real_esp_lcd_panel_reset(handle);panel.reset=e==ESP_OK;return e;
}
esp_err_t __wrap_esp_lcd_panel_init(esp_lcd_panel_handle_t handle) {
    esp_err_t e=__real_esp_lcd_panel_init(handle);panel.initialized=e==ESP_OK;return e;
}
esp_err_t __wrap_esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t handle,bool on) {
    esp_err_t e=__real_esp_lcd_panel_disp_on_off(handle,on);panel.enabled=on && e==ESP_OK;return e;
}
bool tab5_panel_ready(void){return panel.display && panel.reset && panel.initialized && panel.enabled && panel.touch;}
void tab5_panel_describe(char *dest,size_t capacity) {
    if(!panel.name){snprintf(dest,capacity,"Экран: профиль не определён");return;}
    snprintf(dest,capacity,"%s · DSI %u · %u МГц · V %u/%u/%u · touch %s",panel.name,panel.lane,panel.pclk,
             panel.vbp,panel.vpw,panel.vfp,panel.touch?"OK":"ошибка");
}
