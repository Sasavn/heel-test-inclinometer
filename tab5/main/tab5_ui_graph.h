#ifndef TAB5_UI_GRAPH_H
#define TAB5_UI_GRAPH_H
#include "tab5_ui.h"
#define TAB5_UI_GRAPH_NONE INT16_MIN
typedef struct { int16_t v[4]; } tab5_ui_graph_point_t;
typedef struct {
    tab5_ui_graph_point_t slow[1200], fast[300];
    uint64_t slow_bucket,fast_bucket,last_slow[2],last_fast[2],last_now;
    unsigned slow_next,fast_next,slow_count,fast_count;
    /* Per-slot sensor bitmasks: real sample and permitted edge from its
     * preceding sample. 600 bytes, no per-point 64-bit timestamps. */
    uint8_t fast_real[300],fast_bridge[300];
    uint64_t render_good[2],render_errors[2];
    bool render_blocked[2];
    bool initialized;
} tab5_ui_graph_t;
void tab5_ui_graph_reset(tab5_ui_graph_t *g);
/* now_us anchors the grid; each new sensor timestamp fills its own retained
 * bucket. Repeated/older timestamps are ignored, offline clears the current
 * bucket, and a backward now_us starts a fresh history. Unknown now is inert. */
void tab5_ui_graph_sample(tab5_ui_graph_t *g,const tab5_ui_state_t *s);
/* Fixed time grid, including missing samples; latest slot is at count-1. */
unsigned tab5_ui_graph_read(const tab5_ui_graph_t *g,unsigned seconds,unsigned series,int32_t *out);
/* Presentation only: bridge bounded interior fast-grid holes at normal
 * configured cadence times successful reply delta (+50% jitter), capped at
 * max(500ms, three periods). Errors/offline block edges. Raw history is unchanged.
 * Slow windows remain identical to raw reads; no extrapolated head/tail. */
unsigned tab5_ui_graph_read_render(const tab5_ui_graph_t *g,unsigned seconds,unsigned series,int32_t *out);
#endif
