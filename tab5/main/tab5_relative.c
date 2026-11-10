#include "tab5_relative.h"
#include <math.h>
#include <string.h>

void tab5_relative_config_default(tab5_relative_config_t *out) {
    if(out)*out=(tab5_relative_config_t){.version=1,.startup_mode=TAB5_MODE_HEEL,
        .reference_index=0,.object_index=1};
}
bool tab5_relative_config_valid(const tab5_relative_config_t *c) {
    return c && c->version==1 && c->startup_mode<=TAB5_MODE_LEVEL &&
        c->reference_index<TAB5_SENSOR_COUNT && c->object_index<TAB5_SENSOR_COUNT &&
        c->reference_index!=c->object_index && c->axes_confirmed<=1 &&
        !c->reserved[0] && !c->reserved[1] && !c->reserved[2];
}
void tab5_relative_init(tab5_relative_t *s,const tab5_relative_config_t *c) {
    if(!s)return;
    memset(s,0,sizeof(*s));tab5_relative_config_default(&s->config);
    if(tab5_relative_config_valid(c))s->config=*c;
    s->mode=(tab5_mode_t)s->config.startup_mode;s->generation=1;
}
void tab5_relative_boundary(tab5_relative_t *s,const tab5_measure_snapshot_t *sample) {
    if(!s)return;
    s->offset_valid=false;s->offset[0]=s->offset[1]=0;s->generation++;
    for(unsigned i=0;i<2;i++)s->floor_good[i]=sample?sample->sensor[i].good:0;
    tab5_relative_pair_reset(s);
}
void tab5_relative_evaluate(const tab5_relative_t *s,const tab5_measure_snapshot_t *sample,
                            uint64_t now,tab5_relative_result_t *out) {
    if(!out)return;
    memset(out,0,sizeof(*out));
    for(unsigned i=0;i<2;i++){
        out->source[i][0]=out->source[i][1]=NAN;
        out->live[i]=out->relative[i]=NAN;out->age_us[i]=UINT64_MAX;
    }
    if(!s || !sample || !tab5_relative_config_valid(&s->config)){
        out->quality=TAB5_PAIR_MISSING;return;
    }
    out->offset_valid=s->offset_valid;out->generation=s->generation;
    out->offset[0]=s->offset[0];out->offset[1]=s->offset[1];
    if(!s->config.axes_confirmed)out->quality|=TAB5_PAIR_AXES;
    unsigned index[2]={s->config.reference_index,s->config.object_index};
    for(unsigned role=0;role<2;role++){
        unsigned i=index[role];const tab5_measure_sensor_t *p=&sample->sensor[i];
        out->timestamp_us[role]=p->timestamp_us;out->sequence[role]=p->good;
        if(!p->valid || !p->good || p->address!=i+2)out->quality|=TAB5_PAIR_MISSING;
        if(p->fail_streak || p->last_error!=TAB5_MEASURE_OK)out->quality|=TAB5_PAIR_ERROR;
        if(p->good<=s->floor_good[i])out->quality|=TAB5_PAIR_WARMUP;
        if(p->good && p->timestamp_us<=now)out->age_us[role]=now-p->timestamp_us;
        if(out->age_us[role]>TAB5_RELATIVE_MAX_AGE_US)out->quality|=TAB5_PAIR_STALE;
        if(p->good){out->source[role][0]=p->decoded_x;out->source[role][1]=p->decoded_y;}
        if(p->good&&(!isfinite(p->decoded_x)||!isfinite(p->decoded_y)||fabsf(p->decoded_x)>90||fabsf(p->decoded_y)>90))
            out->quality|=TAB5_PAIR_RANGE;
    }
    uint64_t a=out->timestamp_us[0],b=out->timestamp_us[1];
    out->skew_us=a>b?a-b:b-a;
    if(out->skew_us>TAB5_RELATIVE_MAX_SKEW_US)out->quality|=TAB5_PAIR_SKEW;
    out->ready=out->quality==0;
    if(out->ready)for(unsigned axis=0;axis<2;axis++){
        out->live[axis]=out->source[1][axis]-out->source[0][axis];
        out->relative[axis]=out->live[axis]-(s->offset_valid?s->offset[axis]:0);
    }
}
void tab5_relative_pair_reset(tab5_relative_t *s) {
    if(!s)return;
    s->pair_known=false;memset(&s->pair,0,sizeof(s->pair));
    s->pair_used[0]=s->floor_good[s->config.reference_index<TAB5_SENSOR_COUNT?s->config.reference_index:0];
    s->pair_used[1]=s->floor_good[s->config.object_index<TAB5_SENSOR_COUNT?s->config.object_index:1];
}
static void pair_update(tab5_relative_t *s,const tab5_measure_snapshot_t *sample,
                         uint64_t now,tab5_relative_result_t *out,bool feed) {
    if(!out)return;
    tab5_relative_result_t raw;tab5_relative_evaluate(s,sample,now,&raw);
    if(!s||!sample||!tab5_relative_config_valid(&s->config)){*out=raw;return;}
    /* Startup/boundary warmup must not consume the first head while the other
     * is still pending; BOTH post-boundary replies must be able to complete. */
    if((raw.quality&TAB5_PAIR_WARMUP)||(!s->pair_known&&
       (raw.quality&TAB5_PAIR_MISSING)&&(!raw.sequence[0]||!raw.sequence[1])&&
       !(raw.quality&(TAB5_PAIR_ERROR|TAB5_PAIR_RANGE)))){*out=raw;return;}
    bool future=(raw.sequence[0]&&raw.timestamp_us[0]>now)||
                (raw.sequence[1]&&raw.timestamp_us[1]>now);
    bool fault=(raw.quality&(TAB5_PAIR_MISSING|TAB5_PAIR_ERROR|TAB5_PAIR_AXES|TAB5_PAIR_RANGE))||future;
    bool both=raw.sequence[0]>s->pair_used[0]&&raw.sequence[1]>s->pair_used[1];
    if(fault||(feed&&both)){
        /* A completed BAD pair is a publication too. Never hide it behind an
         * older green pair. Actual faults consume the recovery boundary now. */
        s->pair=raw;s->pair_known=true;
        for(unsigned i=0;i<2;i++)if(raw.sequence[i]>s->pair_used[i])s->pair_used[i]=raw.sequence[i];
    }
    if(!s->pair_known){
        *out=raw;out->quality|=TAB5_PAIR_WARMUP;out->ready=false;
        out->live[0]=out->live[1]=out->relative[0]=out->relative[1]=NAN;return;
    }
    *out=s->pair;out->generation=s->generation;out->offset_valid=s->offset_valid;
    bool expired=false;
    for(unsigned i=0;i<2;i++){
        out->offset[i]=s->offset[i];
        out->age_us[i]=out->sequence[i]&&out->timestamp_us[i]<=now?now-out->timestamp_us[i]:UINT64_MAX;
        if(out->age_us[i]>TAB5_RELATIVE_MAX_AGE_US)expired=true;
    }
    if(expired){
        out->quality|=TAB5_PAIR_STALE;
        if(!(s->pair.quality&TAB5_PAIR_STALE)){
            s->pair.quality|=TAB5_PAIR_STALE;
            for(unsigned i=0;i<2;i++)if(raw.sequence[i]>s->pair_used[i])s->pair_used[i]=raw.sequence[i];
        }
    }
    out->ready=out->quality==0;
    for(unsigned i=0;i<2;i++){
        out->live[i]=out->ready?out->source[1][i]-out->source[0][i]:NAN;
        out->relative[i]=out->ready?out->live[i]-(s->offset_valid?s->offset[i]:0):NAN;
    }
}
void tab5_relative_observe(tab5_relative_t *s,const tab5_measure_snapshot_t *sample,
                           uint64_t now,tab5_relative_result_t *out) {
    pair_update(s,sample,now,out,true);
}
void tab5_relative_read(tab5_relative_t *s,const tab5_measure_snapshot_t *sample,
                        uint64_t now,tab5_relative_result_t *out) {
    pair_update(s,sample,now,out,false);
}
bool tab5_relative_capture(tab5_relative_t *s,const tab5_measure_snapshot_t *sample,uint64_t now) {
    tab5_relative_result_t r;tab5_relative_read(s,sample,now,&r);
    if(!r.ready)return false;
    s->offset[0]=r.live[0];s->offset[1]=r.live[1];s->offset_valid=true;s->generation++;
    return true;
}
