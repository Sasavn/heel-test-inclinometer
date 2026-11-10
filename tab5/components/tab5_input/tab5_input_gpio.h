#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
/* Return true only after accepting the event (e.g. successful queue send).
 * Nonblocking callbacks run in polling task context, never ISR context.
 * False causes retry with identical value and original timestamp. No direct
 * recording/UI action here: acceptance must mean queued exactly once. */
typedef bool (*tab5_gpio_level_cb)(bool level, uint32_t ms);
typedef bool (*tab5_gpio_detent_cb)(int delta, uint32_t ms);
typedef struct {
    tab5_gpio_detent_cb detent;
    tab5_gpio_level_cb button; /* raw debounced down/up; no PRESS/LONG */
    tab5_gpio_level_cb record; /* changed logical ON/OFF; boot is baseline */
} tab5_gpio_callbacks;
typedef struct {
    /* Driver requires explicit distinct pins from 2/3/4/48. Agreed optional
     * main controls profile: A=3, B=4, PRESS=2, REC=48; CONFIG defaults OFF. */
    int a, b, press, record;
    uint32_t debounce_ms;
    uint32_t poll_ms; /* explicit 1, 5 or 10; RTOS tick resolution may be coarser */
} tab5_gpio_config;
/* Call once, from a single task at startup. All callbacks required.
 * GPIO configuration never occurs when CONFIG_TAB5_INPUT_GPIO_ENABLE=n.
 * Backpressure pauses control decoding until the pending batch is accepted. No finite
 * queue can preserve unobserved physical motion during a stalled consumer.
 * UI task owns first-wake consumption and 600ms hold recognition. */
esp_err_t tab5_input_gpio_start(const tab5_gpio_config *, const tab5_gpio_callbacks *);
uint32_t tab5_input_gpio_retries(void);
/* Cached poll-task telemetry only. Raw order A/B/PRESS/REC; callback order
 * encoder/button/record. Levels are electrical (0=closed to GND). Counters
 * wrap modulo 2^32 and count OBSERVED samples/edges, not interrupt events.
 * Snapshot never reads/configures GPIO or dispatches a control command. */
typedef struct {
    bool enabled, initialized, sampled;
    tab5_gpio_config config;
    int8_t raw[4], decoder_partial;
    uint32_t sampled_ms, samples, raw_changes[4];
    uint32_t poll_gap_ms, poll_gap_max_ms;
    uint32_t ab_changes, invalid_ab, detent_positive, detent_negative;
    uint32_t press_down, press_up, record_on, record_off;
    uint32_t attempts[3], accepted[3], rejected[3];
    uint32_t event_ms[3], attempt_ms[3], event_age_ms[3], event_age_max_ms[3];
    int start_error;
} tab5_gpio_status;
void tab5_input_gpio_snapshot(tab5_gpio_status *out);
