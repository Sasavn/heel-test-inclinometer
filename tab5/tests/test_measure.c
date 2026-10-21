#include "../main/tab5_measure.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static tab5_measure_core_t c;
static void near(float a,float b){assert(fabsf(a-b)<.001f);}
static void frame_crc(uint8_t *p,size_t n){uint16_t v=tab5_measure_crc16(p,n-2);p[n-2]=(uint8_t)v;p[n-1]=(uint8_t)(v>>8);}
static void sample(float x,float y,uint64_t now){uint16_t r[2]={(uint16_t)lroundf(x*100+10000),(uint16_t)lroundf(y*100+10000)};assert(tab5_measure_core_sample(&c,0,r,now));}
static void protocol(void){
    near(tab5_measure_decode(10000),0);near(tab5_measure_decode(9000),-10);near(tab5_measure_decode(19000),90);
    assert(tab5_measure_crc16((const uint8_t*)"123456789",9)==0x4b37);
    uint8_t request[8];tab5_measure_request(2,request);assert(request[0]==2 && request[1]==3 && request[3]==1 && request[5]==2);
    assert(tab5_measure_crc16(request,8)==0);
    uint8_t p[9]={2,3,4,0x27,0x10,0x23,0x28,0,0};uint16_t raw[2]={0};frame_crc(p,9);
    assert(tab5_measure_parse(2,p,9,raw)==TAB5_MEASURE_OK);assert(raw[0]==10000 && raw[1]==9000);
    assert(tab5_measure_parse(3,p,9,raw)==TAB5_MEASURE_FRAME);
    p[4]^=1;assert(tab5_measure_parse(2,p,9,raw)==TAB5_MEASURE_CRC);p[4]^=1;
    p[2]=2;frame_crc(p,9);assert(tab5_measure_parse(2,p,9,raw)==TAB5_MEASURE_FRAME);
    uint8_t ex[5]={2,0x83,2,0,0};frame_crc(ex,5);assert(tab5_measure_parse(2,ex,5,raw)==TAB5_MEASURE_EXCEPTION);
    for(size_t n=0;n<5;n++)assert(tab5_measure_parse(2,p,n,raw)==TAB5_MEASURE_FRAME);
}
static void filtering(void){
    tab5_measure_config_t cfg;tab5_measure_config_default(&cfg);cfg.alpha=.5f;tab5_measure_core_init(&c,&cfg,0);
    sample(0,1,0);sample(20,30,200000);near(c.state.sensor[0].filtered_x,0);near(c.state.sensor[0].filtered_y,1);
    sample(2,3,400000);near(c.state.sensor[0].filtered_x,1);near(c.state.sensor[0].filtered_y,2);
    tab5_measure_core_zero(&c,false);near(c.state.sensor[0].x,0);near(c.state.sensor[0].zero_x,1);
    uint64_t good=c.state.sensor[0].good,ts=c.state.sensor[0].timestamp_us;
    tab5_measure_core_error(&c,0,TAB5_MEASURE_TIMEOUT);assert(c.state.sensor[0].valid);
    assert(c.state.sensor[0].good==good && c.state.sensor[0].timestamp_us==ts && c.state.sensor[0].errors==1);
    assert(c.filter[0].initialized);
    tab5_measure_core_error(&c,0,TAB5_MEASURE_TIMEOUT);assert(c.state.sensor[0].valid);
    tab5_measure_core_error(&c,0,TAB5_MEASURE_TIMEOUT);assert(!c.state.sensor[0].valid && !c.filter[0].initialized);
    tab5_measure_core_zero(&c,false);near(c.state.sensor[0].zero_x,1);
    sample(10,20,800000);near(c.state.sensor[0].filtered_x,10);near(c.state.sensor[0].x,9);assert(!c.state.sensor[0].calm_x);
    tab5_measure_core_zero(&c,true);near(c.state.sensor[0].x,10);near(c.state.sensor[0].zero_x,0);
    assert(!tab5_measure_core_sample(&c,2,NULL,0));
}
static void schedule(void){
    tab5_measure_config_t cfg;tab5_measure_config_default(&cfg);cfg.frequency_hz=3;
    tab5_measure_core_init(&c,&cfg,0);assert(!tab5_measure_cycle_due(&c,333333));assert(tab5_measure_cycle_due(&c,333334));
    assert(!tab5_measure_cycle_due(&c,333335));assert(tab5_measure_cycle_due(&c,900000));assert(!tab5_measure_cycle_due(&c,900001));
    cfg.frequency_hz=5;tab5_measure_core_configure(&c,&cfg,1000000);assert(!tab5_measure_cycle_due(&c,1199999));assert(tab5_measure_cycle_due(&c,1200000));
    cfg.alpha=NAN;cfg.gap_ms=0;cfg.frequency_hz=0;tab5_measure_core_configure(&c,&cfg,0);
    assert(c.state.config.frequency_hz==1 && c.state.config.gap_ms==15);near(c.state.config.alpha,.15f);
}
static void rolling(void){
    tab5_measure_core_init(&c,NULL,0);
    for(unsigned k=0;k<100;k++)sample(0,0,(uint64_t)k*200000);
    assert(c.state.sensor[0].calm_x && c.state.sensor[0].calm_y && c.state.stable);
    uint16_t count=c.filter[0].count;
    tab5_measure_core_error(&c,0,TAB5_MEASURE_TIMEOUT);
    tab5_measure_core_error(&c,0,TAB5_MEASURE_TIMEOUT);
    assert(c.state.sensor[0].valid && c.filter[0].count==count && c.state.sensor[0].calm_x);
    sample(0,0,19900000);assert(c.state.sensor[0].fail_streak==0);
    /* Seed filter at exact new level; rolling math remains driven by samples. */
    c.filter[0].initialized=false;sample(1.6f,0,20000000);assert(c.state.sensor[0].calm_x);near(c.state.sensor[0].roll_x,1.6f);
    c.filter[0].initialized=false;sample(1.7f,0,20200000);assert(c.state.sensor[0].calm_x);
    c.filter[0].initialized=false;sample(1.71f,0,20400000);assert(!c.state.sensor[0].calm_x && c.state.sensor[0].calm_y && !c.state.stable);
    tab5_measure_core_zero(&c,false);near(c.state.sensor[0].roll_x,1.71f);
    for(unsigned k=103;k<205;k++)sample(1.71f,0,(uint64_t)k*200000);
    assert(c.state.sensor[0].calm_x);near(c.state.sensor[0].roll_x,0);
    tab5_measure_config_t cfg=c.state.config;cfg.roll_window_s=10;tab5_measure_core_configure(&c,&cfg,41000000);
    assert(!c.state.sensor[0].calm_x && !c.state.stable && c.filter[0].count==0);
    for(unsigned i=0;i<3;i++){tab5_measure_core_error(&c,0,TAB5_MEASURE_CRC);}
    assert(!c.state.stable);
    /* A long gap does not retain a filled window or insert synthetic readings. */
    sample(0,0,100000000);assert(c.state.sensor[0].roll_fill_s==0 && !c.state.sensor[0].calm_x);
}
int main(void){protocol();filtering();schedule();rolling();puts("tab5 measurement tests: PASS");return 0;}
