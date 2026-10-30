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
    uint32_t poll_ms; /* explicit 5 or 10; RTOS tick resolution may be coarser */
} tab5_gpio_config;
/* Call once, from a single task at startup. All callbacks required.
 * GPIO configuration never occurs when CONFIG_TAB5_INPUT_GPIO_ENABLE=n.
 * Backpressure pauses sampling until the pending batch is accepted. No finite
 * queue can preserve unobserved physical motion during a stalled consumer.
 * UI task owns first-wake consumption and 600ms hold recognition. */
esp_err_t tab5_input_gpio_start(const tab5_gpio_config *, const tab5_gpio_callbacks *);
uint32_t tab5_input_gpio_retries(void);
