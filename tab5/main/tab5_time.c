#include "tab5_time.h"
#include <string.h>
static int offset_minutes=180;
bool tab5_time_offset_valid(int m){return m>=-720&&m<=840&&m%15==0;}
void tab5_time_set_offset(int m){if(tab5_time_offset_valid(m))__atomic_store_n(&offset_minutes,m,__ATOMIC_RELEASE);}
int tab5_time_offset(void){return __atomic_load_n(&offset_minutes,__ATOMIC_ACQUIRE);}
static int leap(int y){return y%4==0&&(y%100!=0||y%400==0);}
static int month_days(int y,int m){static const int d[]={31,28,31,30,31,30,31,31,30,31,30,31};return d[m-1]+(m==2&&leap(y));}
bool tab5_time_fields_valid(int y,int m,int d,int h,int mi,int s) {
    static const unsigned char days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(y<2000 || y>2099 || m<1 || m>12 || d<1 || h<0 || h>23 || mi<0 || mi>59 || s<0 || s>59)return false;
    unsigned limit=days[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0));
    return (unsigned)d<=limit;
}
bool tab5_time_local_to_utc(const struct tm *t,int offset,int64_t *utc){
    if(!t||!utc||!tab5_time_offset_valid(offset)||t->tm_year<100||t->tm_year>199||t->tm_mon<0||t->tm_mon>11)return false;
    int y=t->tm_year+1900,m=t->tm_mon+1;
    if(!tab5_time_fields_valid(y,m,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec))return false;
    int64_t days=0;for(int i=1970;i<y;i++)days+=365+leap(i);
    for(int i=1;i<m;i++)days+=month_days(y,i);
    days+=t->tm_mday-1;
    *utc=days*86400+t->tm_hour*3600+t->tm_min*60+t->tm_sec-(int64_t)offset*60;return true;
}
bool tab5_time_utc_to_local(int64_t utc,int offset,struct tm *t){
    /* Bounds checked before addition; allow UTC adjacent to RTC calendar edges. */
    if(!t||!tab5_time_offset_valid(offset)||utc<946598400LL||utc>4102531200LL)return false;
    int64_t wall=utc+(int64_t)offset*60,days=wall/86400,sec=wall%86400;
    int weekday=(int)((days+4)%7),y=1970;while(days>=365+leap(y)){days-=365+leap(y);y++;}
    if(y<2000||y>2099)return false;
    int yd=(int)days,m=1;while(days>=month_days(y,m)){days-=month_days(y,m);m++;}
    *t=(struct tm){.tm_year=y-1900,.tm_mon=m-1,.tm_mday=(int)days+1,.tm_hour=(int)(sec/3600),
        .tm_min=(int)(sec/60%60),.tm_sec=(int)(sec%60),.tm_wday=weekday,.tm_yday=yd,.tm_isdst=0};return true;
}
bool tab5_time_local_now(time_t utc,struct tm *t){
    if(tab5_time_utc_to_local((int64_t)utc,tab5_time_offset(),t))return true;
    /* Deterministic invalid-clock display; caller must still consult time_valid. */
    if(t){memset(t,0,sizeof(*t));t->tm_year=100;t->tm_mday=1;}return false;
}
bool tab5_time_parse_command(const char *s,int current,struct tm *local,int *offset,bool *set){
    if(!s||!local||!offset||!set||!tab5_time_offset_valid(current)||strlen(s)<19)return false;
    const char pattern[]="dddd-dd-dd dd:dd:dd";int values[6]={0},field=0;
    for(int i=0;i<19;i++){
        if(pattern[i]=='d'){
            if(s[i]<'0'||s[i]>'9')return false;
            values[field]=values[field]*10+s[i]-'0';
        }else{if(s[i]!=pattern[i])return false;field++;}
    }
    int candidate=current;bool explicit_offset=s[19]!=0;
    if(explicit_offset){
        if(strncmp(s+19," tz=",4))return false;
        const char *p=s+23;int sign=1,value=0,digits=0;
        if(*p=='-'||*p=='+'){if(*p=='-')sign=-1;p++;}
        for(;*p;p++){
            if(*p<'0'||*p>'9'||++digits>4)return false;
            value=value*10+*p-'0';
        }
        if(!digits)return false;
        candidate=value*sign;if(!tab5_time_offset_valid(candidate))return false;
    }
    if(!tab5_time_fields_valid(values[0],values[1],values[2],values[3],values[4],values[5]))return false;
    *local=(struct tm){.tm_year=values[0]-1900,.tm_mon=values[1]-1,.tm_mday=values[2],
        .tm_hour=values[3],.tm_min=values[4],.tm_sec=values[5]};
    *offset=candidate;*set=explicit_offset;return true;
}
tab5_time_apply_result_t tab5_time_apply(int64_t utc,int minutes,tab5_time_system_fn system,tab5_time_rtc_fn rtc,void *ctx){
    tab5_time_apply_result_t r={0};struct tm local;
    if(!system||!rtc||!tab5_time_utc_to_local(utc,minutes,&local))return r;
    r.valid=true;r.system_ok=system(ctx,utc);if(r.system_ok)r.rtc_ok=rtc(ctx,&local);return r;
}
