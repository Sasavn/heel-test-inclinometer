#include "tab5_draw_profile.h"
#include "native/native_ui.h"
#ifdef ESP_PLATFORM
#include "tab5_native_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include <string.h>
#include <stdio.h>

bool tab5_draw_profile_timing_enabled(void){return NUI_PROFILE_PRIMITIVE_TIMING!=0;}
bool tab5_draw_profile_counters_enabled(void){return NUI_PROFILE_FINE_COUNTERS!=0;}
void tab5_draw_profile_format(char *out,size_t capacity,const tab5_draw_profile_entry_t *entry){
    if(!out||!capacity)return;
    if(!entry){out[0]=0;return;}
    char us[24]="null",calls[24]="null";
    bool timed=tab5_draw_profile_timing_enabled(),counted=tab5_draw_profile_counters_enabled();
    if(timed)snprintf(us,sizeof(us),"%llu",(unsigned long long)entry->us);
    if(counted)snprintf(calls,sizeof(calls),"%llu",(unsigned long long)entry->calls);
    snprintf(out,capacity,"{\"draw\":\"%s\",\"us\":%s,\"calls\":%s,\"timing_enabled\":%s,\"counters_enabled\":%s}",
             entry->name?entry->name:"unknown",us,calls,timed?"true":"false",counted?"true":"false");
}

/* Keep the diagnostic protocol's eight-entry shape, but report actual native
 * primitives rather than retaining linker wrappers from the removed toolkit. */
static const char *const names[TAB5_DRAW_PROFILE_COUNT]={
    "fill","border","box_shadow","image","label","arc","line","triangle"
};
static const unsigned indices[TAB5_DRAW_PROFILE_COUNT]={
    NUI_PRIM_FILL,NUI_PRIM_BORDER,NUI_PRIM_SHADOW,NUI_PRIM_IMAGE,
    NUI_PRIM_LABEL,NUI_PRIM_ARC,NUI_PRIM_LINE,NUI_PRIM_TRIANGLE
};
static nui_native_stats_t baseline;
static tab5_draw_profile_snapshot_t completed_snapshot;
#ifdef ESP_PLATFORM
static portMUX_TYPE cache_lock=portMUX_INITIALIZER_UNLOCKED;
static void copy_completed(tab5_draw_profile_snapshot_t *out){
    if(!out)return;
#ifdef ESP_PLATFORM
    portENTER_CRITICAL(&cache_lock);
#endif
    *out=completed_snapshot;
#ifdef ESP_PLATFORM
    portEXIT_CRITICAL(&cache_lock);
#endif
    /* Services are reachable before the display starts. Valid names are
     * required even when there has not been a completed snapshot yet. */
    for(unsigned i=0;i<TAB5_DRAW_PROFILE_COUNT;i++)out->entries[i].name=names[i];
}
#endif

void tab5_draw_profile_snapshot(tab5_draw_profile_snapshot_t *out,bool reset)
{
#ifdef ESP_PLATFORM
    /* The renderer's 64-bit counters are protected by its owning UI mutex.
     * Never mask interrupts while acquiring a task mutex. */
    if(!tab5_native_port_lock(1000)){
        copy_completed(out);
        return;
    }
#endif
    nui_native_stats_t stats;
    nui_native_profile_snapshot(&stats);
    tab5_draw_profile_snapshot_t current;
    for(unsigned i=0;i<TAB5_DRAW_PROFILE_COUNT;i++){
        unsigned k=indices[i];
        current.entries[i].name=names[i];
        current.entries[i].us=stats.primitive[k].us>=baseline.primitive[k].us?
            stats.primitive[k].us-baseline.primitive[k].us:stats.primitive[k].us;
        current.entries[i].calls=stats.primitive[k].calls>=baseline.primitive[k].calls?
            stats.primitive[k].calls-baseline.primitive[k].calls:stats.primitive[k].calls;
    }
#ifdef ESP_PLATFORM
    portENTER_CRITICAL(&cache_lock);
#endif
    completed_snapshot=current;
#ifdef ESP_PLATFORM
    portEXIT_CRITICAL(&cache_lock);
#endif
    if(out)*out=current;
    if(reset)baseline=stats;
#ifdef ESP_PLATFORM
    tab5_native_port_unlock();
#endif
}
