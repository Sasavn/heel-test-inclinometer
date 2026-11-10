#include "tab5_time_sync.h"
#include "tab5_time.h"
#include "tab5_board.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
/* All clocks and RTC I/O are fakes. Never call host settimeofday or real UDP. */
typedef struct {int64_t mono,wall,answer;int mode,exchanges;bool system_fail,rtc_fail;unsigned sys_calls,rtc_calls;struct tm rtc;} fake_t;
static void put32(uint8_t *p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static int64_t now(void *p){return ((fake_t*)p)->mono;}
static bool nonce(void *p,uint8_t out[8]){(void)p;memset(out,0xa5,8);return true;}
static tab5_time_sync_code_t exchange(void *p,const uint8_t req[48],uint8_t *out,size_t cap,size_t *len,int64_t deadline,int64_t *received){
    fake_t *f=p;f->exchanges++;assert(cap>=48&&deadline>f->mono);f->mono+=200000;
    if(f->mode==1)return TAB5_TIME_SYNC_DNS;
    if(f->mode==2)return TAB5_TIME_SYNC_TIMEOUT;
    if(f->mode==3)return TAB5_TIME_SYNC_IO;
    memset(out,0,48);out[0]=0x24;out[1]=2;memcpy(out+24,req+40,8);
    uint32_t wire=(uint32_t)(f->answer+2208988800LL);put32(out+32,wire);put32(out+40,wire);
    out[47]=1;*len=48;*received=f->mono;
    if(f->mode==4)*len=47;
    if(f->mode==5)out[0]|=0xc0;
    if(f->mode==6)out[1]=0;
    if(f->mode==7)out[0]=0x23;
    if(f->mode==8)out[24]^=0xff;
    if(f->mode==9)out[1]=16;
    if(f->mode==10){f->mono=deadline+1;*received=f->mono;}
    if(f->mode==11)memset(out+32,0,16);
    if(f->mode==12)out[0]=0x0c; /* version 1 */
    return TAB5_TIME_SYNC_OK;
}
static bool sys(void *p,int64_t utc){fake_t *f=p;f->sys_calls++;if(f->system_fail)return false;f->wall=utc;return true;}
static bool rtc(void *p,const struct tm *t){fake_t *f=p;f->rtc_calls++;if(f->rtc_fail)return false;f->rtc=*t;return true;}
static int64_t epoch(int y,int m,int d,int h,int mi,int s,int offset){
    struct tm t={.tm_year=y-1900,.tm_mon=m-1,.tm_mday=d,.tm_hour=h,.tm_min=mi,.tm_sec=s};int64_t v;
    assert(tab5_time_local_to_utc(&t,offset,&v));return v;
}
static void core_tests(void){
    fake_t f={.mono=1000000,.answer=1793000000};tab5_time_sync_transport_t io={&f,now,nonce,exchange};
    assert(tab5_time_sync_fetch(&io,false).code==TAB5_TIME_SYNC_NO_STA&&f.exchanges==0);
    for(int i=1;i<=12;i++){
        f.mode=i;int64_t old=f.wall;tab5_time_sync_result_t r=tab5_time_sync_fetch(&io,true);
        assert(r.code!=TAB5_TIME_SYNC_OK&&f.wall==old&&f.sys_calls==0&&f.rtc_calls==0);
    }
    f.mode=0;
    const int64_t dates[]={946684800LL,2085978495LL,2085978496LL,2147483648LL,4102444799LL};
    for(unsigned i=0;i<sizeof(dates)/sizeof(*dates);i++){
        f.answer=dates[i];tab5_time_sync_result_t r=tab5_time_sync_fetch(&io,true);
        assert(r.code==TAB5_TIME_SYNC_OK&&r.utc_seconds==dates[i]);
        assert(r.received_us==f.mono);
    }
    f.answer=4102444800LL;assert(tab5_time_sync_fetch(&io,true).code==TAB5_TIME_SYNC_RANGE);
    f.answer=946684799LL;assert(tab5_time_sync_fetch(&io,true).code==TAB5_TIME_SYNC_RANGE);
    for(int offset=-720;offset<=840;offset+=15){
        struct tm t;int64_t v=epoch(2028,2,29,23,59,59,offset),back;
        assert(tab5_time_utc_to_local(v,offset,&t)&&t.tm_mday==29&&t.tm_mon==1&&t.tm_year==128);
        assert(tab5_time_local_to_utc(&t,offset,&back)&&v==back);
    }
    struct tm t;int64_t v=epoch(2026,10,26,0,30,0,180);
    assert(v==epoch(2026,10,25,21,30,0,0));
    assert(tab5_time_utc_to_local(v,180,&t)&&t.tm_hour==0&&t.tm_mday==26);
    assert(tab5_time_utc_to_local(v,0,&t)&&t.tm_hour==21&&t.tm_mday==25);
    assert(!tab5_time_utc_to_local(INT64_MAX,840,&t));
    assert(!tab5_time_utc_to_local(epoch(2099,12,31,23,0,0,0),180,&t));
    assert(!tab5_time_fields_valid(2100,2,29,0,0,0));
    assert(!tab5_time_fields_valid(2026,2,29,0,0,0));
    assert(tab5_time_fields_valid(2000,2,29,0,0,0));
    assert(!tab5_time_offset_valid(-721)&&!tab5_time_offset_valid(841)&&!tab5_time_offset_valid(303));
    int parsed;bool explicit_offset;
    assert(tab5_time_parse_command("2026-10-26 00:30:00",180,&t,&parsed,&explicit_offset)&&parsed==180&&!explicit_offset);
    assert(tab5_time_parse_command("2026-10-26 00:30:00 tz=0",180,&t,&parsed,&explicit_offset)&&parsed==0&&explicit_offset);
    assert(tab5_time_parse_command("2026-10-26 00:30:00 tz=+840",180,&t,&parsed,&explicit_offset)&&parsed==840);
    assert(tab5_time_parse_command("2026-10-26 00:30:00 tz=-720",180,&t,&parsed,&explicit_offset)&&parsed==-720);
    const char *bad[]={"2026-02-29 00:30:00","2026-10-26 00:30:00 junk","2026-10-26 00:30:00 tz=303",
        "2026-10-26 00:30:00 tz=841","2026-10-26 00:30:00 tz=","2026-10-26 00:30:00 tz=999999999999999999",
        "2026-10-26 00:30:00 tz=180 extra","2026-10-26 00:30:00 tz=-721","2026-10-26 00:30:00 tz=15x"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!tab5_time_parse_command(bad[i],180,&t,&parsed,&explicit_offset));
    /* Apply rejects unsupported local date before touching either clock. */
    f=(fake_t){0};tab5_time_apply_result_t a=tab5_time_apply(4102444799LL,180,sys,rtc,&f);
    assert(!a.valid&&f.sys_calls==0&&f.rtc_calls==0);
    f.system_fail=true;a=tab5_time_apply(v,180,sys,rtc,&f);assert(a.valid&&!a.system_ok&&!a.rtc_ok&&f.rtc_calls==0);
    f.system_fail=false;f.rtc_fail=true;a=tab5_time_apply(v,180,sys,rtc,&f);assert(a.valid&&a.system_ok&&!a.rtc_ok);
    f.rtc_fail=false;a=tab5_time_apply(v,180,sys,rtc,&f);assert(a.system_ok&&a.rtc_ok&&f.rtc.tm_hour==0);
    /* Simulated active recording: corrections in both directions only change
     * wall date. Monotonic sample Ms, duration and open file identity survive. */
    struct {int64_t start,last_ms;unsigned rows;char file[32];} recording={.start=5000000,.file="KR_existing.csv"};
    for(int i=0;i<4;i++){
        f.mono=6000000+i*100000;
        int64_t correction=v+(i%2?7200:-7200);
        a=tab5_time_apply(correction,180,sys,rtc,&f);assert(a.system_ok&&a.rtc_ok);
        assert(f.mono/1000>recording.last_ms);recording.last_ms=f.mono/1000;recording.rows++;
        assert(f.mono-recording.start==1000000+i*100000);
        assert(!strcmp(recording.file,"KR_existing.csv"));
    }
    assert(recording.rows==4);
    puts("PASS: bounded transport errors, NTP era2036/2038/2099, invalid packets, UTC/local/leap/calendar, partial apply, monotonic recording model");
}
/* Implemented by the host runner's board I/O shim. The actual board.c is linked. */
void test_board_io(void);
int main(void){
    bool attempted=false;
    assert(!tab5_time_sync_auto_due(NULL,true,false,false));
    assert(!tab5_time_sync_auto_due(&attempted,false,false,false)&&!attempted);
    assert(!tab5_time_sync_auto_due(&attempted,true,true,false)&&!attempted);
    assert(!tab5_time_sync_auto_due(&attempted,true,false,true)&&!attempted);
    assert(tab5_time_sync_auto_due(&attempted,true,false,false)&&attempted);
    for(unsigned i=0;i<100;i++)assert(!tab5_time_sync_auto_due(&attempted,true,false,false));
    assert(!tab5_time_sync_auto_due(&attempted,false,false,false));
    assert(!tab5_time_sync_auto_due(&attempted,true,false,false));
    puts("PASS: automatic NTP waits for STA/idle/no recording, one attempt per boot, no retry loop");
    core_tests();test_board_io();return 0;
}
