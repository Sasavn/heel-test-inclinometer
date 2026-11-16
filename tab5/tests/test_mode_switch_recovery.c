/* Production acquisition core, queue callback, control owner and live consumer.
 * The runner injects only OS/NVS/transport boundaries. Never opens a device. */
#include "tab5_ui.h"
#include "tab5_services.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
#define ESP_OK 0
#define NVS_READWRITE 1
#define pdTRUE 1
typedef int nvs_handle_t;
typedef struct { unsigned index; tab5_measure_sensor_t value; uint64_t observed_us; } sample_event_t;
static tab5_measure_core_t core;
static tab5_relative_t relative_state;
static tab5_workspace_t workspace;
static bool recording, samples_on, control_reserved;
static float battery=12;
static char notice[160];
static uint64_t now;
static int samples=1,storage_samples=2;
static uint32_t sample_drops,route_drops;
static sample_event_t queue[64];
static unsigned queued;
static tab5_relative_config_t saved;
static int64_t esp_timer_get_time(void){return (int64_t)now;}
void tab5_measure_snapshot(tab5_measure_snapshot_t *s){*s=core.state;}
bool tab5_services_control_begin(void){if(control_reserved)return false;control_reserved=true;return true;}
void tab5_services_control_end(void){CHECK(control_reserved);control_reserved=false;}
static int nvs_open(const char *key,int mode,int *h){CHECK(!strcmp(key,"tab5")&&mode==NVS_READWRITE);*h=1;return 0;}
static int nvs_set_blob(int h,const char *key,const void *p,size_t n){CHECK(h==1&&!strcmp(key,"dual_cfg")&&n==sizeof(saved));memcpy(&saved,p,n);return 0;}
static int nvs_commit(int h){CHECK(h==1);return 0;}
static void nvs_close(int h){CHECK(h==1);}
static bool save_workspace_config(const tab5_workspace_config_t *cfg){(void)cfg;return true;}
static void broadcast(const char *line){(void)line;}
static void tab5_time_local_now(time_t t,struct tm *out){*out=*gmtime(&t);}
int32_t tab5_services_scaled(double v,unsigned digits){(void)v;(void)digits;return 0;}
static int xQueueSend(int q,const sample_event_t *e,int timeout){
    CHECK(!timeout);if(q==storage_samples)return pdTRUE;
    CHECK(q==samples&&queued<64);queue[queued++]=*e;return pdTRUE;
}
#include "mode_switch_generated.h"

static void drain(void){for(unsigned i=0;i<queued;i++)consume_sample(&queue[i]);queued=0;}
static void rx(unsigned head,unsigned cycle,tab5_measure_error_t error,bool delayed){
    now=1000000+(uint64_t)cycle*100000+head*33333;
    const uint16_t raw[2]={(uint16_t)(10000+head*100),10000};
    if(error==TAB5_MEASURE_OK)CHECK(tab5_measure_core_sample(&core,head,raw,now));
    else tab5_measure_core_error(&core,head,error);
    sample_callback(head,&core.state.sensor[head],NULL);
    if(!delayed)drain();
}
static tab5_relative_result_t read_pair(void){
    tab5_relative_result_t r;tab5_relative_read(&relative_state,&core.state,now,&r);return r;
}
static void negative_config(void){
    tab5_relative_config_t cfg;tab5_relative_config_default(&cfg);cfg.axes_confirmed=1;
    for(unsigned variant=0;variant<6;variant++){
        tab5_relative_init(&relative_state,&cfg);
        if(variant==0)relative_state.config.reference_index=TAB5_SENSOR_COUNT;
        if(variant==1)relative_state.config.object_index=255;
        if(variant==2)relative_state.config.reference_index=relative_state.config.object_index;
        if(variant==3)relative_state.config.version=99;
        if(variant==4)relative_state.config.axes_confirmed=2;
        if(variant==5)relative_state.config.reserved[0]=1;
        tab5_relative_t before=relative_state;tab5_relative_result_t r;
        tab5_relative_observe(&relative_state,&core.state,now,&r);
        CHECK(!r.ready&&r.quality==TAB5_PAIR_MISSING&&isnan(r.live[0]));
        CHECK(!memcmp(&before,&relative_state,sizeof(before)));
        tab5_relative_read(&relative_state,&core.state,now,&r);
        CHECK(!r.ready&&r.quality==TAB5_PAIR_MISSING);
        CHECK(!tab5_relative_capture(&relative_state,&core.state,now));
        CHECK(!memcmp(&before,&relative_state,sizeof(before)));
    }
    tab5_relative_result_t r;
    tab5_relative_read(NULL,NULL,now,&r);CHECK(!r.ready);
    tab5_relative_observe(NULL,NULL,now,&r);CHECK(!r.ready);
    tab5_relative_init(&relative_state,&cfg);
    tab5_relative_read(&relative_state,NULL,now,&r);CHECK(!r.ready);
    tab5_relative_observe(&relative_state,NULL,now,&r);CHECK(!r.ready);
    puts("PASS invalid roles/index/version/axes/reserved: observe/read/capture reject before indexing, state unchanged");
}
int main(int argc,char **argv){
    CHECK(argc==7);
    unsigned head=(unsigned)atoi(argv[1]),failures=(unsigned)atoi(argv[2]);
    bool swapped=atoi(argv[3])!=0,delayed=atoi(argv[4])!=0,instrument=atoi(argv[5])!=0;
    tab5_measure_error_t error=(tab5_measure_error_t)atoi(argv[6]);
    CHECK(head<2&&failures>0&&failures<=4&&error>=TAB5_MEASURE_TIMEOUT&&error<=TAB5_MEASURE_IO);
    tab5_measure_core_init(&core,NULL,0);
    tab5_relative_config_t cfg;tab5_relative_config_default(&cfg);cfg.axes_confirmed=1;
    if(swapped){cfg.reference_index=1;cfg.object_index=0;}
    tab5_relative_init(&relative_state,&cfg);tab5_workspace_init(&workspace,NULL);
    rx(0,0,TAB5_MEASURE_OK,false);rx(1,0,TAB5_MEASURE_OK,false);
    CHECK(read_pair().ready);
    tab5_ui_action_t action={.kind=instrument?TAB5_UI_INSTRUMENT:TAB5_UI_MODE,.mode=TAB5_MODE_LEVEL};
    CHECK(apply_dual_action(&action)&&relative_state.mode==TAB5_MODE_LEVEL&&!control_reserved);
    CHECK(!read_pair().ready&&(read_pair().quality&TAB5_PAIR_WARMUP));
    rx(0,1,TAB5_MEASURE_OK,false);CHECK(!read_pair().ready);
    rx(1,1,TAB5_MEASURE_OK,false);CHECK(read_pair().ready);
    for(unsigned n=2;n<2+failures;n++){
        rx(0,n,head==0?error:TAB5_MEASURE_OK,delayed);
        rx(1,n,head==1?error:TAB5_MEASURE_OK,delayed);
        if(delayed)drain();
        tab5_relative_result_t r=read_pair();
        CHECK(!r.ready&&(r.quality&TAB5_PAIR_ERROR)&&isnan(r.live[0]));
        CHECK(!tab5_relative_capture(&relative_state,&core.state,now));
        /* An additional health read sees the same failure, never a new one. */
        uint64_t used[2];memcpy(used,relative_state.pair_used,sizeof(used));
        for(unsigned k=0;k<4;k++)CHECK(!read_pair().ready);
        CHECK(!memcmp(used,relative_state.pair_used,sizeof(used)));
        if(n>2){
            CHECK(used[swapped?1:0]==core.state.sensor[0].good);
            CHECK(used[swapped?0:1]==core.state.sensor[1].good-(head==0?1:0));
        }
    }
    unsigned recovery_cycle=2+failures;
    uint64_t floor[2];memcpy(floor,relative_state.pair_used,sizeof(floor));
    rx(0,recovery_cycle,TAB5_MEASURE_OK,true);
    /* Health already sees A_new while its real callback is still queued. */
    for(unsigned k=0;k<8;k++)CHECK(!read_pair().ready);
    fprintf(stderr,"queued recovery health: floor=%llu/%llu after-read=%llu/%llu\n",
        (unsigned long long)floor[0],(unsigned long long)floor[1],
        (unsigned long long)relative_state.pair_used[0],(unsigned long long)relative_state.pair_used[1]);
    CHECK(!memcmp(floor,relative_state.pair_used,sizeof(floor)));
    drain();
    CHECK(!read_pair().ready); /* Second head still has its old error or skew. */
    CHECK(!tab5_relative_capture(&relative_state,&core.state,now));
    /* Repeated feed=false must not eat the healthy first head. */
    for(unsigned k=0;k<8;k++)CHECK(!read_pair().ready);
    CHECK(relative_state.pair_used[swapped?1:0]==floor[swapped?1:0]);
    rx(1,recovery_cycle,TAB5_MEASURE_OK,delayed);if(delayed)drain();
    tab5_relative_result_t r=read_pair();
    fprintf(stderr,"recovery head=%u failures=%u swapped=%u delayed=%u instrument=%u error=%u ready=%u quality=%u skew=%llu\n",
        head+2,failures,swapped,delayed,instrument,error,r.ready,r.quality,(unsigned long long)r.skew_us);
    CHECK(r.ready&&r.quality==0&&r.skew_us==33333&&r.live[0]==(swapped?-1:1));
    for(unsigned role=0;role<2;role++){
        unsigned physical=role?cfg.object_index:cfg.reference_index;
        CHECK(r.sequence[role]>floor[role]&&r.sequence[role]==core.state.sensor[physical].good);
        CHECK(r.timestamp_us[role]==1000000+(uint64_t)recovery_cycle*100000+physical*33333);
    }
    for(unsigned n=recovery_cycle+1;n<recovery_cycle+5;n++){
        rx(0,n,TAB5_MEASURE_OK,delayed);rx(1,n,TAB5_MEASURE_OK,delayed);if(delayed)drain();
        r=read_pair();CHECK(r.ready&&r.quality==0&&r.skew_us==33333);
    }
    /* Real role control resets the boundary without changing acquisition order. */
    action=(tab5_ui_action_t){.kind=TAB5_UI_REL_ROLES,.relative_config=cfg};
    action.relative_config.reference_index=cfg.object_index;
    action.relative_config.object_index=cfg.reference_index;
    CHECK(apply_dual_action(&action)&&!read_pair().ready);
    rx(0,recovery_cycle+5,TAB5_MEASURE_OK,false);CHECK(!read_pair().ready);
    rx(1,recovery_cycle+5,TAB5_MEASURE_OK,false);r=read_pair();
    CHECK(r.ready&&r.live[0]==(swapped?1:-1));
    /* A NEW fault invalidates the recovered green pair immediately, including
     * read-only health detection before the queued callback is consumed. */
    tab5_measure_core_error(&core,head,error);
    r=read_pair();CHECK(!r.ready&&(r.quality&TAB5_PAIR_ERROR)&&isnan(r.relative[0]));
    CHECK(!tab5_relative_capture(&relative_state,&core.state,now));
    CHECK(!sample_drops&&!route_drops&&!queued&&!recording);
    negative_config();
    puts("PASS mode/instrument switch, original queues/consumer, new/repeated faults, both-fresh recovery, continued publication, no stale green");
}
