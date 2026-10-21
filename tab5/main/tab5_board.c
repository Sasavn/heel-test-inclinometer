#include "tab5_board.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include <math.h>
#include <string.h>
#include <sys/time.h>
static i2c_master_dev_handle_t rtc, ina;
static bool time_valid, rtc_valid;
static int bcd(unsigned v) { return (v&15)<=9 && (v>>4)<=9 ? (int)((v>>4)*10+(v&15)) : -1; }
static uint8_t to_bcd(int v) { return (uint8_t)((v/10)*16+v%10); }
static bool read_reg(i2c_master_dev_handle_t d,uint8_t reg,void *out,size_t n) {
    return d && i2c_master_transmit_receive(d,&reg,1,out,n,30)==ESP_OK;
}
static bool write_reg(i2c_master_dev_handle_t d,uint8_t reg,const uint8_t *v,size_t n) {
    uint8_t buf[16];if(!d || n>15)return false;buf[0]=reg;memcpy(buf+1,v,n);
    return i2c_master_transmit(d,buf,n+1,30)==ESP_OK;
}
void tab5_board_init(void) {
    if(bsp_i2c_init()!=ESP_OK)return;
    i2c_device_config_t cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x32,.scl_speed_hz=100000};
    i2c_master_bus_add_device(bsp_i2c_get_handle(),&cfg,&rtc);
    cfg.device_address=0x41;i2c_master_bus_add_device(bsp_i2c_get_handle(),&cfg,&ina);
    uint8_t r[7],flags;
    /* RX8130: 0x10 seconds..year; flags VLF/VBLF invalidate retained clock.
       Never clear flags merely to make an invalid clock look valid. */
    if(!read_reg(rtc,0x1d,&flags,1) || (flags&0x82) || !read_reg(rtc,0x10,r,7))return;
    struct tm t={.tm_sec=bcd(r[0]&0x7f),.tm_min=bcd(r[1]&0x7f),.tm_hour=bcd(r[2]&0x3f),
                 .tm_mday=bcd(r[4]&0x3f),.tm_mon=bcd(r[5]&0x1f)-1,.tm_year=100+bcd(r[6])};
    if(t.tm_sec<0 || t.tm_sec>59 || t.tm_min<0 || t.tm_min>59 || t.tm_hour<0 || t.tm_hour>23 ||
       t.tm_mday<1 || t.tm_mday>31 || t.tm_mon<0 || t.tm_mon>11 || t.tm_year<100 || t.tm_year>199)return;
    struct tm requested=t;time_t epoch=mktime(&t);
    if(t.tm_mday!=requested.tm_mday || t.tm_mon!=requested.tm_mon)return;
    struct timeval tv={.tv_sec=epoch};settimeofday(&tv,NULL);time_valid=rtc_valid=true;
}
bool tab5_board_time_valid(void) { return time_valid; }
bool tab5_board_rtc_valid(void) { return rtc_valid; }
bool tab5_board_set_time(const struct tm *value) {
    struct tm t=*value,check=t;time_t epoch=mktime(&t);
    if(t.tm_year!=check.tm_year || t.tm_mon!=check.tm_mon || t.tm_mday!=check.tm_mday ||
       t.tm_hour!=check.tm_hour || t.tm_min!=check.tm_min || t.tm_sec!=check.tm_sec)return false;
    struct timeval tv={.tv_sec=epoch};settimeofday(&tv,NULL);time_valid=true;
    uint8_t raw[]={to_bcd(t.tm_sec),to_bcd(t.tm_min),to_bcd(t.tm_hour),(uint8_t)(1u<<t.tm_wday),
                   to_bcd(t.tm_mday),to_bcd(t.tm_mon+1),to_bcd(t.tm_year-100)};
    rtc_valid=write_reg(rtc,0x10,raw,sizeof(raw));
    if(rtc_valid){uint8_t flags;if(read_reg(rtc,0x1d,&flags,1)){flags&=(uint8_t)~0x82;write_reg(rtc,0x1d,&flags,1);}}
    return true;
}
float tab5_board_battery(void) {
    uint8_t id[2],v[2];if(!read_reg(ina,0xff,id,2) || id[0]!=0x22 || id[1]!=0x60 || !read_reg(ina,2,v,2))return NAN;
    return (float)((v[0]<<8)|v[1])*0.00125f;
}
