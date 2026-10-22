#include "tab5_draw_profile.h"
#include "native/native_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Link the real diagnostic adapter without a window or target port. */
void nui_native_profile_snapshot(nui_native_stats_t *out){memset(out,0,sizeof(*out));}
int main(void){
    assert(tab5_draw_profile_timing_enabled()==(NUI_PROFILE_PRIMITIVE_TIMING!=0));
    assert(tab5_draw_profile_counters_enabled()==(NUI_PROFILE_FINE_COUNTERS!=0));
    tab5_draw_profile_entry_t e={.name="fill",.us=1234,.calls=99};
    char text[256];tab5_draw_profile_format(text,sizeof(text),&e);puts(text);
    tab5_draw_profile_snapshot_t snapshot;tab5_draw_profile_snapshot(&snapshot,false);
    for(unsigned i=0;i<TAB5_DRAW_PROFILE_COUNT;i++)assert(snapshot.entries[i].name);
    tab5_draw_profile_format(text,sizeof(text),NULL);assert(!*text);
    return 0;
}
