#pragma once
#include "tab5_ui.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t render_us,flush_us,refresh_us,wait_us,rotate_us;
    uint32_t renders,flushes,refreshes,refresh_max_us,errors;
    /* Sampled-input -> CPU render start, excludes touch I2C, PPA and LCD time. */
    uint32_t interactive_renders,input_render_wait_max_us;
} tab5_native_port_stats_t;

esp_err_t tab5_native_port_start(tab5_ui_action_cb_t callback,void *user);
bool tab5_native_port_lock(uint32_t timeout_ms);
void tab5_native_port_unlock(void);
/* Atomic counters only, never takes the UI mutex. Reset returns zero counters. */
void tab5_native_port_stats(tab5_native_port_stats_t *out,bool reset);
