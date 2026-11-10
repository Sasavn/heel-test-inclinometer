#ifndef STM_RELATIVE_H
#define STM_RELATIVE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define REL_SENSOR_COUNT 2
#define REL_MEASURE_OK 0
#ifndef APP_DEFAULT_INSTRUMENT
#define APP_DEFAULT_INSTRUMENT 0
#endif
#if APP_DEFAULT_INSTRUMENT != 0 && APP_DEFAULT_INSTRUMENT != 1
#error APP_DEFAULT_INSTRUMENT must be 0 (heel) or 1 (level)
#endif
typedef struct {
    uint8_t address, last_error, fail_streak;
    bool valid;
    uint64_t good, timestamp_us;
    float decoded_x, decoded_y;
} relative_sensor_t;
typedef struct { relative_sensor_t sensor[REL_SENSOR_COUNT]; } relative_snapshot_t;

typedef enum { REL_MODE_HEEL=0, REL_MODE_LEVEL=1 } relative_mode_t;
/* Portable versioned config; STM32 packs durable fields in SET1. No durable REL zero. */
typedef struct {
    uint8_t version, startup_mode, reference_index, object_index;
    uint8_t axes_confirmed, reserved[3];
} relative_config_t;
enum { REL_PAIR_MISSING=1u, REL_PAIR_STALE=2u, REL_PAIR_SKEW=4u,
    REL_PAIR_AXES=8u, REL_PAIR_WARMUP=16u, REL_PAIR_RANGE=32u,
    REL_PAIR_ERROR=64u };
#define RELATIVE_MAX_AGE_US UINT64_C(500000)
#define RELATIVE_MAX_SKEW_US UINT64_C(50000)
typedef struct {
    bool ready, offset_valid;
    uint32_t quality;
    float source[2][2]; /* [reference/object][X/Y], decoded, no user sensor zero. */
    float live[2], offset[2], relative[2]; /* object-reference; live-offset */
    uint64_t timestamp_us[2], sequence[2], age_us[2], skew_us;
    uint32_t generation;
} relative_result_t;
typedef struct {
    relative_mode_t mode;
    relative_config_t config;
    uint64_t floor_good[2];
    bool offset_valid;
    float offset[2];
    uint32_t generation;
    /* Single-owner publication state, never persisted. Copies used for a new
     * recorder epoch MUST call pair_reset before consuming ordered events. */
    bool pair_known;
    uint64_t pair_used[2]; /* Role order, including rejected/failed replies. */
    relative_result_t pair;
} relative_t;

void relative_config_default(relative_config_t *out);
bool relative_config_valid(const relative_config_t *config);
void relative_init(relative_t *state,const relative_config_t *config);
/* Change/reset boundaries require a fresh successful reply from BOTH heads. */
void relative_boundary(relative_t *state,const relative_snapshot_t *sample);
void relative_evaluate(const relative_t *state,const relative_snapshot_t *sample,
                            uint64_t now_us,relative_result_t *out);
/* Pure validator above also audits candidates. Feed observe from ordered RX;
 * runtime readers use read below. Publish only both-new pairs, retain original
 * RX provenance on partial updates, invalidate actual faults. Serialize owner. */
void relative_pair_reset(relative_t *state);
void relative_observe(relative_t *state,const relative_snapshot_t *sample,
                           uint64_t now_us,relative_result_t *out);
/* Read/health check cannot publish healthy latest/latest input. Feed observe
 * from ordered acquisition events even when no UI reader samples valid phases. */
void relative_read(relative_t *state,const relative_snapshot_t *health,
                        uint64_t now_us,relative_result_t *out);
bool relative_capture(relative_t *state,const relative_snapshot_t *sample,uint64_t now_us);
/* Tab5 audit schema, byte-for-byte same header and column order. */
#define RELATIVE_CSV_HEADER "EventMs;EventSensor;Reference;Object;RefUs;ObjUs;RefSeq;ObjSeq;RefAgeUs;ObjAgeUs;SkewUs;Quality;Ready;AxesConfirmed;RefX;RefY;ObjX;ObjY;LiveDX;LiveDY;OffsetValid;OffsetX;OffsetY;RelDX;RelDY;Generation;Event;ObservedUs\n"
void relative_number(char *out,size_t cap,float value);
void relative_u64(char out[21],uint64_t value);
int relative_format_csv(char *out,size_t cap,uint32_t ms,unsigned sensor,
    const relative_config_t *config,const relative_result_t *r,const char *event,uint64_t now);
/* mm rise over one metre of horizontal run; undefined at/after +/-90 degrees. */
float relative_mm_per_m(float degrees);
#endif
