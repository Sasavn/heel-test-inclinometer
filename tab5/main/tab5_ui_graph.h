#ifndef TAB5_UI_GRAPH_H
#define TAB5_UI_GRAPH_H
#include "tab5_ui.h"
#define TAB5_UI_GRAPH_NONE INT16_MIN
/* Public presentation gap sentinel; independent of any widget toolkit. */
#define TAB5_UI_GRAPH_RENDER_NONE INT32_MAX
typedef struct { int16_t v[4]; } tab5_ui_graph_point_t;
typedef struct {
    tab5_ui_graph_point_t slow[1200], fast[300];
    uint64_t slow_bucket,fast_bucket,last_slow[2],last_fast[2],last_now;
    unsigned slow_next,fast_next,slow_count,fast_count;
    /* Packed slot nibbles: two real-sample bits and two allowed-edge bits.
     * Both grids cost 750 bytes total, without per-slot timestamps. */
    uint8_t fast_meta[150],slow_meta[600];
    uint64_t render_good[2][2];
    bool render_blocked[2][2];
    bool initialized;
} tab5_ui_graph_t;
void tab5_ui_graph_reset(tab5_ui_graph_t *g);
/* now_us anchors the grid; each new sensor timestamp fills its own retained
 * bucket. Repeated/older timestamps are ignored, offline clears the current
 * bucket, and a backward now_us starts a fresh history. Unknown now is inert. */
void tab5_ui_graph_sample(tab5_ui_graph_t *g,const tab5_ui_state_t *s);
/* Fixed time grid, including missing samples; latest slot is at count-1. */
unsigned tab5_ui_graph_read(const tab5_ui_graph_t *g,unsigned seconds,unsigned series,int32_t *out);
/* Display only, all windows: interpolate bounded interior holes between real
 * samples, including short isolated failures, up to max(500ms, 1.5 nominal
 * periods). Offline (invalid or >=3 failures), invalid axes and longer outages
 * remain gaps. No extrapolation; raw history/read semantics never change. */
unsigned tab5_ui_graph_read_render(const tab5_ui_graph_t *g,unsigned seconds,unsigned series,int32_t *out);
#endif
