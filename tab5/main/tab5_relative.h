#ifndef TAB5_RELATIVE_H
#define TAB5_RELATIVE_H
#include "tab5_measure.h"

typedef enum { TAB5_MODE_HEEL=0, TAB5_MODE_LEVEL=1 } tab5_mode_t;
/* Fixed-size, versioned NVS payload; REL offset and recording are never durable. */
typedef struct {
    uint8_t version, startup_mode, reference_index, object_index;
    uint8_t axes_confirmed, reserved[3];
} tab5_relative_config_t;
enum { TAB5_PAIR_MISSING=1u, TAB5_PAIR_STALE=2u, TAB5_PAIR_SKEW=4u,
    TAB5_PAIR_AXES=8u, TAB5_PAIR_WARMUP=16u, TAB5_PAIR_RANGE=32u,
    TAB5_PAIR_ERROR=64u };
#define TAB5_RELATIVE_MAX_AGE_US UINT64_C(500000)
#define TAB5_RELATIVE_MAX_SKEW_US UINT64_C(50000)
typedef struct {
    bool ready, offset_valid;
    uint32_t quality;
    float source[2][2]; /* [reference/object][X/Y], decoded, no user sensor zero. */
    float live[2], offset[2], relative[2]; /* object-reference; live-offset */
    uint64_t timestamp_us[2], sequence[2], age_us[2], skew_us;
    uint32_t generation;
} tab5_relative_result_t;
typedef struct {
    tab5_mode_t mode;
    tab5_relative_config_t config;
    uint64_t floor_good[2];
    bool offset_valid;
    float offset[2];
    uint32_t generation;
} tab5_relative_t;

void tab5_relative_config_default(tab5_relative_config_t *out);
bool tab5_relative_config_valid(const tab5_relative_config_t *config);
void tab5_relative_init(tab5_relative_t *state,const tab5_relative_config_t *config);
/* Change/reset boundaries require a fresh successful reply from BOTH heads. */
void tab5_relative_boundary(tab5_relative_t *state,const tab5_measure_snapshot_t *sample);
void tab5_relative_evaluate(const tab5_relative_t *state,const tab5_measure_snapshot_t *sample,
                            uint64_t now_us,tab5_relative_result_t *out);
bool tab5_relative_capture(tab5_relative_t *state,const tab5_measure_snapshot_t *sample,uint64_t now_us);
#endif
