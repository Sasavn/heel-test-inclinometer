#include "tab5_workspace.h"
#include <math.h>
#include <string.h>
void tab5_workspace_config_default(tab5_workspace_config_t *c){
    if(c)*c=(tab5_workspace_config_t){.version=1,.limit_deg={0.10f,0.10f}};
}
bool tab5_workspace_config_valid(const tab5_workspace_config_t *c){
    return c&&c->version==1&&c->limits_enabled<=1&&c->filter_enabled<=1&&!c->reserved&&
        isfinite(c->limit_deg[0])&&isfinite(c->limit_deg[1])&&
        c->limit_deg[0]>=0.01f&&c->limit_deg[0]<=90&&c->limit_deg[1]>=0.01f&&c->limit_deg[1]<=90;
}
void tab5_workspace_init(tab5_workspace_t *s,const tab5_workspace_config_t *c){
    if(!s)return;
    memset(s,0,sizeof(*s));tab5_workspace_config_default(&s->live.config);
    if(tab5_workspace_config_valid(c))s->live.config=*c;
    s->live.known=true;s->broken=true;
}
void tab5_workspace_clear(tab5_workspace_t *s){
    if(!s)return;
    tab5_workspace_config_t c=s->live.config;uint32_t rev=s->live.revision+1;
    uint32_t gen=s->live.generation;uint8_t ref=s->reference_index,obj=s->object_index;
    bool id=s->identity_known;uint64_t seq[2]={s->used_sequence[0],s->used_sequence[1]};
    tab5_workspace_init(s,&c);s->live.revision=rev;s->live.generation=gen;
    s->reference_index=ref;s->object_index=obj;s->identity_known=id;
    memcpy(s->used_sequence,seq,sizeof(seq));
}
bool tab5_workspace_configure(tab5_workspace_t *s,const tab5_workspace_config_t *c){
    if(!s||!tab5_workspace_config_valid(c))return false;
    if(memcmp(&s->live.config,c,sizeof(*c))){s->live.config=*c;tab5_workspace_clear(s);}
    return true;
}
static void point(tab5_workspace_view_t *v,uint64_t us,float x,float y,bool gap){
    v->history[v->next]=(tab5_workspace_point_t){.timestamp_us=us,.value={x,y},.gap=gap};
    v->next=(v->next+1)%TAB5_WORKSPACE_CAPACITY;
    if(v->count<TAB5_WORKSPACE_CAPACITY)v->count++;
    v->revision++;
}
void tab5_workspace_update(tab5_workspace_t *s,const tab5_relative_config_t *roles,
                           const tab5_relative_result_t *p,uint64_t now){
    if(!s||!roles||!p)return;
    if(!s->identity_known||s->live.generation!=p->generation||
       s->reference_index!=roles->reference_index||s->object_index!=roles->object_index){
        tab5_workspace_clear(s);s->identity_known=true;s->live.generation=p->generation;
        s->reference_index=roles->reference_index;s->object_index=roles->object_index;
        s->used_sequence[0]=s->used_sequence[1]=0;
    }
    tab5_workspace_view_t *v=&s->live;
    if(s->last_now_us&&now<s->last_now_us){
        tab5_workspace_clear(s);memcpy(s->used_sequence,p->sequence,sizeof(s->used_sequence));
        s->last_now_us=now;return; /* New segment, require both NEW replies. */
    }
    s->last_now_us=now;
    v->live_ready=p->ready;v->live_quality=p->quality;
    v->limit_known=p->ready&&v->config.limits_enabled;
    for(unsigned a=0;a<2;a++)v->within_limit[a]=v->limit_known&&fabsf(p->relative[a])<=v->config.limit_deg[a];
    if(!p->ready){
        if(!s->broken){point(v,now,NAN,NAN,true);v->live_gaps++;}
        s->broken=true;v->values_valid=false;v->filter_warm=false;s->filter_count=0;
        /* Require both sources to advance after ANY observed quality failure. */
        for(unsigned i=0;i<2;i++)if(p->sequence[i]>s->used_sequence[i])s->used_sequence[i]=p->sequence[i];
        return;
    }
    if(p->sequence[0]<=s->used_sequence[0]||p->sequence[1]<=s->used_sequence[1])return;
    uint64_t stamp=p->timestamp_us[0]>p->timestamp_us[1]?p->timestamp_us[0]:p->timestamp_us[1];
    if(stamp>now||!isfinite(p->relative[0])||!isfinite(p->relative[1]))return;
    if(s->last_point_us&&stamp<=s->last_point_us){
        if(!s->broken){point(v,now,NAN,NAN,true);v->live_gaps++;}
        s->broken=true;v->values_valid=false;v->filter_warm=false;s->filter_count=0;
        memcpy(s->used_sequence,p->sequence,sizeof(s->used_sequence));return;
    }
    if(s->last_point_us&&(!s->broken)&&
       stamp-s->last_point_us>TAB5_RELATIVE_MAX_AGE_US){
        point(v,stamp,NAN,NAN,true);v->live_gaps++;s->broken=true;s->filter_count=0;
    }
    for(unsigned a=0;a<2;a++){
        s->ema[a]=s->broken||!s->filter_count?p->relative[a]:s->ema[a]+0.2f*(p->relative[a]-s->ema[a]);
        v->value[a]=v->config.filter_enabled?s->ema[a]:p->relative[a];
        if(!v->stats_valid)v->minimum[a]=v->maximum[a]=v->value[a];
        else{if(v->value[a]<v->minimum[a])v->minimum[a]=v->value[a];if(v->value[a]>v->maximum[a])v->maximum[a]=v->value[a];}
    }
    if(s->filter_count<5)s->filter_count++;
    v->filter_warm=!v->config.filter_enabled||s->filter_count>=5;
    v->stats_valid=v->values_valid=true;v->values_us=stamp;v->live_pairs++;
    point(v,stamp,v->value[0],v->value[1],false);
    memcpy(s->used_sequence,p->sequence,sizeof(s->used_sequence));s->last_point_us=stamp;s->broken=false;
}
bool tab5_workspace_hold(tab5_workspace_t *s,bool hold,uint64_t now){
    if(!s)return false;
    if(hold){if(!s->live.live_ready||!s->live.values_valid)return false;
        if(!s->held){s->frozen=s->live;s->frozen.held_us=now;s->held=true;}}
    else s->held=false;
    return true;
}
void tab5_workspace_snapshot(const tab5_workspace_t *s,tab5_workspace_view_t *v){
    if(!s||!v)return;
    *v=s->held?s->frozen:s->live;v->held=s->held;
    /* HOLD freezes values/history, NEVER live validity or operator-limit status. */
    v->live_ready=s->live.live_ready;v->live_quality=s->live.live_quality;
    v->limit_known=s->live.limit_known;memcpy(v->within_limit,s->live.within_limit,sizeof(v->within_limit));
    v->live_pairs=s->live.live_pairs;v->live_gaps=s->live.live_gaps;
}
