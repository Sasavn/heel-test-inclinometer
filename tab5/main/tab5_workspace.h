#ifndef TAB5_WORKSPACE_H
#define TAB5_WORKSPACE_H
#include "tab5_relative.h"
#define TAB5_WORKSPACE_CAPACITY 128
/* Separate versioned NVS blob. Limits are operator settings in degrees, NOT accuracy. */
typedef struct {
    uint8_t version, limits_enabled, filter_enabled, reserved;
    float limit_deg[2];
} tab5_workspace_config_t;
typedef struct {
    uint64_t timestamp_us;
    float value[2]; /* Relative result, optionally presentation EMA; never source X/Y. */
    bool gap;
} tab5_workspace_point_t;
typedef struct {
    bool known, held, values_valid, stats_valid, filter_warm, live_ready;
    bool limit_known, within_limit[2]; /* Live UNFILTERED result; false/unknown when bad. */
    tab5_workspace_config_t config;
    uint32_t generation, revision, live_quality;
    uint64_t held_us, values_us, live_pairs, live_gaps;
    float value[2], minimum[2], maximum[2];
    uint16_t count, next; /* Ring, oldest=(next+CAPACITY-count)%CAPACITY. */
    tab5_workspace_point_t history[TAB5_WORKSPACE_CAPACITY];
} tab5_workspace_view_t;
typedef struct {
    tab5_workspace_view_t live, frozen;
    uint64_t used_sequence[2], last_point_us, last_now_us;
    uint8_t reference_index, object_index, filter_count;
    bool identity_known, broken, held;
    float ema[2];
} tab5_workspace_t;
void tab5_workspace_config_default(tab5_workspace_config_t *out);
bool tab5_workspace_config_valid(const tab5_workspace_config_t *config);
void tab5_workspace_init(tab5_workspace_t *state,const tab5_workspace_config_t *config);
/* Single owner (main); no allocation, task, UART access, clocks or side effects. */
void tab5_workspace_update(tab5_workspace_t *state,const tab5_relative_config_t *roles,
                           const tab5_relative_result_t *pair,uint64_t now_us);
void tab5_workspace_clear(tab5_workspace_t *state);
bool tab5_workspace_configure(tab5_workspace_t *state,const tab5_workspace_config_t *config);
bool tab5_workspace_hold(tab5_workspace_t *state,bool hold,uint64_t now_us);
void tab5_workspace_snapshot(const tab5_workspace_t *state,tab5_workspace_view_t *out);
#endif
