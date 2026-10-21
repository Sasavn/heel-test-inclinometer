#ifndef TAB5_MEASURE_H
#define TAB5_MEASURE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TAB5_SENSOR_COUNT 2
#define TAB5_ROLL_CAPACITY 305
/* Core is single-owner. External callers serialize core access; IDF wrapper
 * below owns its core and provides locked snapshots/configuration. Times: us. */
typedef struct {
    uint16_t frequency_hz, gap_ms;
    float alpha;
    uint8_t roll_window_s, roll_rate_hz;
    float roll_calm_deg, roll_hysteresis_deg;
} tab5_measure_config_t;
typedef enum { TAB5_MEASURE_OK, TAB5_MEASURE_TIMEOUT, TAB5_MEASURE_CRC,
    TAB5_MEASURE_FRAME, TAB5_MEASURE_EXCEPTION, TAB5_MEASURE_IO } tab5_measure_error_t;
typedef struct {
    uint8_t address, fail_streak;
    /* valid is connection status (lost after 3 consecutive failures), not freshness.
     * timestamp_us/good advance ONLY on good replies. Never record snapshots. */
    bool valid, calm_x, calm_y;
    uint16_t raw_x, raw_y;
    float decoded_x, decoded_y, filtered_x, filtered_y, x, y;
    float zero_x, zero_y, roll_x, roll_y;
    uint8_t roll_fill_s;
    uint64_t timestamp_us, good, errors;
    tab5_measure_error_t last_error;
} tab5_measure_sensor_t;
typedef struct {
    tab5_measure_config_t config;
    tab5_measure_sensor_t sensor[TAB5_SENSOR_COUNT];
    bool stable;
    uint64_t cycles;
} tab5_measure_snapshot_t;
typedef struct {
    float history[2][3];
    bool initialized, roll_used;
    int16_t roll[2][TAB5_ROLL_CAPACITY];
    uint64_t time[TAB5_ROLL_CAPACITY], roll_due_us;
    uint16_t count, next;
} tab5_measure_filter_t;
typedef struct {
    tab5_measure_snapshot_t state;
    tab5_measure_filter_t filter[TAB5_SENSOR_COUNT];
    uint64_t next_cycle_us;
} tab5_measure_core_t;
/* Callback runs in UART task AFTER unlocking; sample is borrowed for call only.
 * A valid callback is the only recording trigger; snapshot is never a log source.
 * Do not block UART task: copy/queue sample for storage/protocol. */
typedef void (*tab5_measure_sample_cb_t)(unsigned index,
    const tab5_measure_sensor_t *sample, void *user);
void tab5_measure_config_default(tab5_measure_config_t *config);
void tab5_measure_core_init(tab5_measure_core_t *core, const tab5_measure_config_t *config, uint64_t now_us);
void tab5_measure_core_configure(tab5_measure_core_t *core, const tab5_measure_config_t *config, uint64_t now_us);
bool tab5_measure_cycle_due(tab5_measure_core_t *core, uint64_t now_us);
float tab5_measure_decode(uint16_t raw);
uint16_t tab5_measure_crc16(const uint8_t *data, size_t size);
void tab5_measure_request(uint8_t address, uint8_t out[8]);
tab5_measure_error_t tab5_measure_parse(uint8_t address, const uint8_t *frame, size_t size, uint16_t raw[2]);
bool tab5_measure_core_sample(tab5_measure_core_t *core, unsigned index, const uint16_t raw[2], uint64_t now_us);
void tab5_measure_core_error(tab5_measure_core_t *core, unsigned index, tab5_measure_error_t error);
void tab5_measure_core_zero(tab5_measure_core_t *core, bool reset);
#ifdef ESP_PLATFORM
#include "esp_err.h"
/* Singleton, UART1 115200 8N1, TX20 RX21 DIR34, active-high TX.
 * Start once after persisted config is loaded. No hardware touched on host. */
esp_err_t tab5_measure_start(const tab5_measure_config_t *config, tab5_measure_sample_cb_t callback, void *user);
void tab5_measure_snapshot(tab5_measure_snapshot_t *out);
void tab5_measure_configure(const tab5_measure_config_t *config);
void tab5_measure_zero(bool reset);
#endif
#ifdef __cplusplus
}
#endif
#endif
