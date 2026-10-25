#include "tab5_board.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include <math.h>
#include <string.h>
#include <sys/time.h>
static i2c_master_dev_handle_t rtc, ina;
static bool time_valid, rtc_valid, backup_ready;
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
    time_valid=rtc_valid=backup_ready=false;
    if(bsp_i2c_init()!=ESP_OK)return;
    i2c_device_config_t cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x32,.scl_speed_hz=100000};
    i2c_master_bus_add_device(bsp_i2c_get_handle(),&cfg,&rtc);
    cfg.device_address=0x41;i2c_master_bus_add_device(bsp_i2c_get_handle(),&cfg,&ina);
    /* Official Tab5 BT1: 0.07 F / 3.3 V supercap on RX8130 VBAT.
     * Epson ETM50E-10 section 15.5: CHGEN bit5, INIEN bit4. INIEN must
     * be set at least once after POR to enable backup switching. M5Unified
     * RX8130_Class::begin uses bitOn(0x1f,0x30). Force BFVSEL=00 (3.02 V,
     * max 3.10 V, table 5); never inherit BFVSEL=11 unlimited charging.
     * Configure before invalid-clock early returns, without clearing flags.
     * https://download.epsondevice.com/td/pdf/app/RX8130CE_en.pdf */
    uint8_t control,verify;
    if(read_reg(rtc,0x1f,&control,1)){
        control=(uint8_t)((control&~0x03u)|0x30u);
        backup_ready=write_reg(rtc,0x1f,&control,1)&&read_reg(rtc,0x1f,&verify,1)&&((verify&0x33)==0x30);
    }
    uint8_t r[7],flags;
    /* RX8130: 0x10 seconds..year; flags VLF/VBLF invalidate retained clock.
       Never clear flags merely to make an invalid clock look valid. */
    if(!read_reg(rtc,0x1d,&flags,1) || (flags&0x82) || !read_reg(rtc,0x10,r,7))return;
    struct tm t={.tm_sec=bcd(r[0]&0x7f),.tm_min=bcd(r[1]&0x7f),.tm_hour=bcd(r[2]&0x3f),
                 .tm_mday=bcd(r[4]&0x3f),.tm_mon=bcd(r[5]&0x1f)-1,.tm_year=100+bcd(r[6])};
    int64_t epoch;if(!tab5_time_local_to_utc(&t,tab5_time_offset(),&epoch))return;
    struct timeval tv={.tv_sec=(time_t)epoch};
    if(settimeofday(&tv,NULL)==0)time_valid=rtc_valid=true;
}
bool tab5_board_time_valid(void) { return time_valid; }
bool tab5_board_rtc_valid(void) { return rtc_valid; }
bool tab5_board_backup_ready(void){return backup_ready;}
static bool system_set(void *ctx,int64_t epoch){
    (void)ctx;struct timeval tv={.tv_sec=(time_t)epoch};
    bool ok=settimeofday(&tv,NULL)==0;if(ok)time_valid=true;return ok;
}
static bool rtc_set(void *ctx,const struct tm *t){
    (void)ctx;rtc_valid=false;
    uint8_t raw[]={to_bcd(t->tm_sec),to_bcd(t->tm_min),to_bcd(t->tm_hour),(uint8_t)(1u<<t->tm_wday),
                   to_bcd(t->tm_mday),to_bcd(t->tm_mon+1),to_bcd(t->tm_year-100)};
    uint8_t flags,verify;
    if(!write_reg(rtc,0x10,raw,sizeof(raw))||!read_reg(rtc,0x1d,&flags,1))return false;
    flags&=(uint8_t)~0x82;
    if(!write_reg(rtc,0x1d,&flags,1)||!read_reg(rtc,0x1d,&verify,1)||(verify&0x82))return false;
    rtc_valid=true;return true;
}
tab5_time_apply_result_t tab5_board_set_utc(int64_t utc,int offset){
    return tab5_time_apply(utc,offset,system_set,rtc_set,NULL);
}
bool tab5_board_set_time(const struct tm *value) {
    int64_t utc;if(!tab5_time_local_to_utc(value,tab5_time_offset(),&utc))return false;
    tab5_time_apply_result_t r=tab5_board_set_utc(utc,tab5_time_offset());
    return r.valid&&r.system_ok&&r.rtc_ok;
}
float tab5_board_battery(void) {
    uint8_t id[2],v[2];if(!read_reg(ina,0xff,id,2) || id[0]!=0x22 || id[1]!=0x60 || !read_reg(ina,2,v,2))return NAN;
    return (float)((v[0]<<8)|v[1])*0.00125f;
}
