#include "relative.h"
#include <math.h>
#include <string.h>

void relative_config_default(relative_config_t *out) {
    if(out)*out=(relative_config_t){.version=1,.startup_mode=APP_DEFAULT_INSTRUMENT,
        .reference_index=0,.object_index=1};
}
bool relative_config_valid(const relative_config_t *c) {
    return c && c->version==1 && c->startup_mode<=REL_MODE_LEVEL &&
        c->reference_index<REL_SENSOR_COUNT && c->object_index<REL_SENSOR_COUNT &&
        c->reference_index!=c->object_index && c->axes_confirmed<=1 &&
        !c->reserved[0] && !c->reserved[1] && !c->reserved[2];
}
void relative_init(relative_t *s,const relative_config_t *c) {
    if(!s)return;
    memset(s,0,sizeof(*s));relative_config_default(&s->config);
    if(relative_config_valid(c))s->config=*c;
    s->mode=(relative_mode_t)s->config.startup_mode;s->generation=1;
}
void relative_boundary(relative_t *s,const relative_snapshot_t *sample) {
    if(!s)return;
    s->offset_valid=false;s->offset[0]=s->offset[1]=0;s->generation++;
    for(unsigned i=0;i<2;i++)s->floor_good[i]=sample?sample->sensor[i].good:0;
    relative_pair_reset(s);
}
void relative_evaluate(const relative_t *s,const relative_snapshot_t *sample,
                            uint64_t now,relative_result_t *out) {
    if(!out)return;
    memset(out,0,sizeof(*out));
    for(unsigned i=0;i<2;i++){
        out->source[i][0]=out->source[i][1]=NAN;
        out->live[i]=out->relative[i]=NAN;out->age_us[i]=UINT64_MAX;
    }
    if(!s || !sample || !relative_config_valid(&s->config)){
        out->quality=REL_PAIR_MISSING;return;
    }
    out->offset_valid=s->offset_valid;out->generation=s->generation;
    out->offset[0]=s->offset[0];out->offset[1]=s->offset[1];
    if(!s->config.axes_confirmed)out->quality|=REL_PAIR_AXES;
    unsigned index[2]={s->config.reference_index,s->config.object_index};
    for(unsigned role=0;role<2;role++){
        unsigned i=index[role];const relative_sensor_t *p=&sample->sensor[i];
        out->timestamp_us[role]=p->timestamp_us;out->sequence[role]=p->good;
        if(!p->valid || !p->good || p->address!=i+2)out->quality|=REL_PAIR_MISSING;
        if(p->fail_streak || p->last_error!=REL_MEASURE_OK)out->quality|=REL_PAIR_ERROR;
        if(p->good<=s->floor_good[i])out->quality|=REL_PAIR_WARMUP;
        if(p->good && p->timestamp_us<=now)out->age_us[role]=now-p->timestamp_us;
        if(out->age_us[role]>RELATIVE_MAX_AGE_US)out->quality|=REL_PAIR_STALE;
        if(p->good){out->source[role][0]=p->decoded_x;out->source[role][1]=p->decoded_y;}
        if(p->good&&(!isfinite(p->decoded_x)||!isfinite(p->decoded_y)||fabsf(p->decoded_x)>90||fabsf(p->decoded_y)>90))
            out->quality|=REL_PAIR_RANGE;
    }
    uint64_t a=out->timestamp_us[0],b=out->timestamp_us[1];
    out->skew_us=a>b?a-b:b-a;
    if(out->skew_us>RELATIVE_MAX_SKEW_US)out->quality|=REL_PAIR_SKEW;
    out->ready=out->quality==0;
    if(out->ready)for(unsigned axis=0;axis<2;axis++){
        out->live[axis]=out->source[1][axis]-out->source[0][axis];
        out->relative[axis]=out->live[axis]-(s->offset_valid?s->offset[axis]:0);
    }
}
void relative_pair_reset(relative_t *s) {
    if(!s)return;
    s->pair_known=false;memset(&s->pair,0,sizeof(s->pair));
    s->pair_used[0]=s->floor_good[s->config.reference_index<REL_SENSOR_COUNT?s->config.reference_index:0];
    s->pair_used[1]=s->floor_good[s->config.object_index<REL_SENSOR_COUNT?s->config.object_index:1];
}
static void pair_update(relative_t *s,const relative_snapshot_t *sample,
                         uint64_t now,relative_result_t *out,bool feed) {
    if(!out)return;
    relative_result_t raw;relative_evaluate(s,sample,now,&raw);
    if(!s||!sample||!relative_config_valid(&s->config)){*out=raw;return;}
    /* Startup/boundary warmup must not consume the first head while the other
     * is still pending; BOTH post-boundary replies must be able to complete. */
    if((raw.quality&REL_PAIR_WARMUP)||(!s->pair_known&&
       (raw.quality&REL_PAIR_MISSING)&&(!raw.sequence[0]||!raw.sequence[1])&&
       !(raw.quality&(REL_PAIR_ERROR|REL_PAIR_RANGE)))){*out=raw;return;}
    bool future=(raw.sequence[0]&&raw.timestamp_us[0]>now)||
                (raw.sequence[1]&&raw.timestamp_us[1]>now);
    bool fault=(raw.quality&(REL_PAIR_MISSING|REL_PAIR_ERROR|REL_PAIR_AXES|REL_PAIR_RANGE))||future;
    bool both=raw.sequence[0]>s->pair_used[0]&&raw.sequence[1]>s->pair_used[1];
    if(fault||(feed&&both)){
        /* A completed BAD pair is a publication too. Never hide it behind an
         * older green pair. Actual faults consume the recovery boundary now. */
        s->pair=raw;s->pair_known=true;
        for(unsigned i=0;i<2;i++)if(raw.sequence[i]>s->pair_used[i])s->pair_used[i]=raw.sequence[i];
    }
    if(!s->pair_known){
        *out=raw;out->quality|=REL_PAIR_WARMUP;out->ready=false;
        out->live[0]=out->live[1]=out->relative[0]=out->relative[1]=NAN;return;
    }
    *out=s->pair;out->generation=s->generation;out->offset_valid=s->offset_valid;
    bool expired=false;
    for(unsigned i=0;i<2;i++){
        out->offset[i]=s->offset[i];
        out->age_us[i]=out->sequence[i]&&out->timestamp_us[i]<=now?now-out->timestamp_us[i]:UINT64_MAX;
        if(out->age_us[i]>RELATIVE_MAX_AGE_US)expired=true;
    }
    if(expired){
        out->quality|=REL_PAIR_STALE;
        if(!(s->pair.quality&REL_PAIR_STALE)){
            s->pair.quality|=REL_PAIR_STALE;
            for(unsigned i=0;i<2;i++)if(raw.sequence[i]>s->pair_used[i])s->pair_used[i]=raw.sequence[i];
        }
    }
    out->ready=out->quality==0;
    for(unsigned i=0;i<2;i++){
        out->live[i]=out->ready?out->source[1][i]-out->source[0][i]:NAN;
        out->relative[i]=out->ready?out->live[i]-(s->offset_valid?s->offset[i]:0):NAN;
    }
}
void relative_observe(relative_t *s,const relative_snapshot_t *sample,
                           uint64_t now,relative_result_t *out) {
    pair_update(s,sample,now,out,true);
}
void relative_read(relative_t *s,const relative_snapshot_t *sample,
                        uint64_t now,relative_result_t *out) {
    pair_update(s,sample,now,out,false);
}
bool relative_capture(relative_t *s,const relative_snapshot_t *sample,uint64_t now) {
    relative_result_t r;relative_read(s,sample,now,&r);
    if(!r.ready)return false;
    s->offset[0]=r.live[0];s->offset[1]=r.live[1];s->offset_valid=true;s->generation++;
    for(unsigned i=0;i<2;i++)s->floor_good[i]=sample->sensor[i].good;
    relative_pair_reset(s);
    return true;
}

#include <stdio.h>
void relative_u64(char out[21],uint64_t value) {
    char rev[20];unsigned n=0;do{rev[n++]=(char)('0'+value%10);value/=10;}while(value);
    unsigned i=0;while(n)out[i++]=rev[--n];out[i]=0;
}
void relative_number(char *out,size_t cap,float value) {
    if(!isfinite(value)||fabsf(value)>1000000){if(cap)out[0]=0;return;}
    double a=fabs((double)value)*1000;uint32_t q=(uint32_t)a;double f=a-q;
    if(f>0.5||(f==0.5&&(q&1)))q++;
    snprintf(out,cap,"%s%lu.%03lu",value<0&&q?"-":"",(unsigned long)(q/1000),(unsigned long)(q%1000));
}
float relative_mm_per_m(float deg) {
    return isfinite(deg)&&fabsf(deg)<89.9f ? 1000.f*tanf(deg*(3.14159265358979323846f/180.f)):NAN;
}
int relative_format_csv(char *out,size_t cap,uint32_t ms,unsigned sensor,
    const relative_config_t *cfg,const relative_result_t *r,const char *event,uint64_t now) {
    if(!out||!cfg||!r||!event||!relative_config_valid(cfg))return -1;
    char v[10][24];float values[]={r->source[0][0],r->source[0][1],r->source[1][0],r->source[1][1],
        r->ready?r->live[0]:NAN,r->ready?r->live[1]:NAN,r->offset[0],r->offset[1],
        r->ready?r->relative[0]:NAN,r->ready?r->relative[1]:NAN};
    for(unsigned i=0;i<10;i++)relative_number(v[i],sizeof(v[i]),values[i]);
    /* No %s/%f: the target's nano printf need not implement C99 long long/float. */
    char u[8][21];uint64_t integer[]={r->timestamp_us[0],r->timestamp_us[1],r->sequence[0],r->sequence[1],
        r->age_us[0],r->age_us[1],r->skew_us,now};
    for(unsigned i=0;i<8;i++)relative_u64(u[i],integer[i]);
    int n=snprintf(out,cap,"%lu;%u;%u;%u;%s;%s;%s;%s;%s;%s;%s;%lu;%u;%u;%s;%s;%s;%s;%s;%s;%u;%s;%s;%s;%s;%lu;%s;%s\n",
        (unsigned long)ms,sensor,cfg->reference_index+2,cfg->object_index+2,
        u[0],u[1],u[2],u[3],u[4],u[5],u[6],
        (unsigned long)r->quality,r->ready?1:0,cfg->axes_confirmed,
        v[0],v[1],v[2],v[3],v[4],v[5],r->offset_valid?1:0,v[6],v[7],v[8],v[9],
        (unsigned long)r->generation,event,u[7]);
    return n<0||(size_t)n>=cap?-1:n;
}
