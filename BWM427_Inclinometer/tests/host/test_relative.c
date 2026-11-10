#include "relative.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static relative_snapshot_t heads(void){
    relative_snapshot_t s={0};
    for(unsigned i=0;i<2;i++)s.sensor[i]=(relative_sensor_t){.address=i+2,.valid=true,.good=1,
        .timestamp_us=1000000+i*16000,.decoded_x=i?1.25f:.25f,.decoded_y=i?-2.f:-1.f};
    return s;
}
int main(void){
    relative_config_t c;relative_config_default(&c);assert(sizeof(c)==8&&relative_config_valid(&c));
    relative_t state;relative_init(&state,&c);assert(state.mode==APP_DEFAULT_INSTRUMENT&&!state.offset_valid);
    relative_snapshot_t s=heads();relative_result_t r;
    relative_observe(&state,&s,1020000,&r);assert(!r.ready&&(r.quality&REL_PAIR_AXES));
    c.axes_confirmed=1;c.startup_mode=REL_MODE_LEVEL;relative_init(&state,&c);
    relative_observe(&state,&s,1020000,&r);assert(r.ready&&r.live[0]==1&&r.live[1]==-1);
    uint64_t seq=r.sequence[0];s.sensor[0].good++;s.sensor[0].decoded_x=5;s.sensor[0].timestamp_us+=10000;
    relative_observe(&state,&s,1030000,&r);assert(r.ready&&r.sequence[0]==seq&&r.source[0][0]==.25f);
    s.sensor[1].good++;s.sensor[1].timestamp_us+=10000;
    relative_read(&state,&s,1030000,&r);assert(r.sequence[0]==seq); /* polls cannot publish */
    relative_observe(&state,&s,1030000,&r);assert(r.sequence[0]==2&&r.live[0]==-3.75f);
    assert(relative_capture(&state,&s,1030000));relative_read(&state,&s,1030000,&r);
    assert(!r.ready&&(r.quality&REL_PAIR_WARMUP)&&r.offset_valid);
    s.sensor[0].good++;relative_observe(&state,&s,1030000,&r);assert(!r.ready);
    s.sensor[1].good++;relative_observe(&state,&s,1030000,&r);assert(r.ready&&r.relative[0]==0);
    s.sensor[0].fail_streak=1;relative_read(&state,&s,1030000,&r);assert(!r.ready&&(r.quality&REL_PAIR_ERROR));
    s.sensor[0].fail_streak=0;s.sensor[0].good++;
    relative_observe(&state,&s,1030000,&r);assert(!r.ready);
    s.sensor[1].good++;relative_observe(&state,&s,1030000,&r);assert(r.ready);
    relative_read(&state,&s,1600001,&r);assert(!r.ready&&(r.quality&REL_PAIR_STALE));
    s.sensor[0].good++;s.sensor[0].timestamp_us=1600001;relative_observe(&state,&s,1600001,&r);assert(!r.ready);
    s.sensor[1].good++;s.sensor[1].timestamp_us=1650001;relative_observe(&state,&s,1650001,&r);assert(r.ready&&r.skew_us==50000);
    s.sensor[0].good++;s.sensor[1].good++;s.sensor[1].timestamp_us++;relative_observe(&state,&s,1650002,&r);assert(!r.ready&&(r.quality&REL_PAIR_SKEW));
    s=heads();relative_init(&state,&c);s.sensor[0].valid=false;relative_evaluate(&state,&s,1020000,&r);assert(r.quality&REL_PAIR_MISSING);
    s=heads();s.sensor[0].decoded_x=NAN;relative_evaluate(&state,&s,1020000,&r);assert(r.quality&REL_PAIR_RANGE);
    s.sensor[0].decoded_x=90.01f;relative_evaluate(&state,&s,1020000,&r);assert(r.quality&REL_PAIR_RANGE);
    s=heads();s.sensor[0].timestamp_us=1100000;relative_evaluate(&state,&s,1020000,&r);assert(r.quality&REL_PAIR_STALE);
    s=heads();c.reference_index=1;c.object_index=0;relative_init(&state,&c);relative_observe(&state,&s,1020000,&r);assert(r.ready&&r.live[0]==-1);
    char decimal[21];relative_u64(decimal,UINT64_MAX);assert(!strcmp(decimal,"18446744073709551615"));
    char row[640];int n=relative_format_csv(row,sizeof(row),20,2,&c,&r,"SAMPLE",1020000);
    assert(n>0&&strstr(row,";SAMPLE;1020000\n"));unsigned cols=1;for(char *p=row;*p;p++)if(*p==';')cols++;assert(cols==28);
    assert(strstr(row,";-1.000;1.000;0;0.000;0.000;-1.000;1.000;1;SAMPLE;"));
    relative_read(&state,&s,2000000,&r);n=relative_format_csv(row,sizeof(row),1000,2,&c,&r,"GAP",2000000);
    assert(n>0&&!strstr(row,"nan")&&!strstr(row,"inf")&&strstr(row,";GAP;"));
    assert(relative_format_csv(row,4,1,2,&c,&r,"GAP",0)==-1);
    assert(fabsf(relative_mm_per_m(45)-1000)<.001f&&fabsf(relative_mm_per_m(-45)+1000)<.001f);
    assert(fabsf(relative_mm_per_m(1)-17.455065f)<.0001f&&isnan(relative_mm_per_m(90))&&isnan(relative_mm_per_m(NAN)));
    puts("PASS STM relative: both-new, raw, roles, axes, REL zero boundary, quality, recovery, CSV28, mm/m");return 0;
}
