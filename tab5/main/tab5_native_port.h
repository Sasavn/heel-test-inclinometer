#pragma once
#include "tab5_ui.h"
#ifndef TAB5_NATIVE_INPUT_ONLY
#include "esp_err.h"
#endif
#include <stdbool.h>
#include <stdint.h>

/* Hardware-neutral, task-context producers. No GPIO, I2C or ISR API here.
 * Producers only copy events; UI callbacks run in input_dispatch's caller.
 * FIFO is bounded: false means not accepted, caller may retry (not silently
 * coalesced). time_ms is monotonic uint32 milliseconds, including wraparound.
 * TOUCH coordinates are already landscape 1280x720. */
#define TAB5_NATIVE_INPUT_CAPACITY 64u
typedef struct {
    uint32_t posted, consumed, rejected, pending;
} tab5_native_input_stats_t;
bool tab5_native_port_post_input(tab5_ui_input_event_t event,int32_t delta,uint32_t time_ms);
bool tab5_native_port_post_touch(int x,int y,bool pressed,uint32_t time_ms);
/* Single UI consumer, under the UI lock; never call from a producer/ISR.
 * Drains at most CAPACITY events, services retained touch and UI idle timeout.
 * Returns true when queued input was consumed (request an interactive frame). */
bool tab5_native_port_input_dispatch(uint32_t now_ms);
void tab5_native_port_input_stats(tab5_native_input_stats_t *out);
/* Lifecycle only, with no consumer running. Reset opens; close rejects input.
 * Host emulator uses the same queue/consumer without compiling any IDF code. */
void tab5_native_port_input_reset(void);
void tab5_native_port_input_close(void);

#ifndef TAB5_NATIVE_INPUT_ONLY
typedef struct {
    uint64_t render_us,flush_us,refresh_us,wait_us,rotate_us;
    uint32_t renders,flushes,refreshes,refresh_max_us,errors;
    /* Sampled touch / dequeued encoder -> CPU render start. Excludes producer
     * delay, touch I2C, PPA and LCD time; not end-to-end encoder latency. */
    uint32_t interactive_renders,input_render_wait_max_us;
    /* Explicit diagnostic: complete full-frame checks, differing RGB565 pixels,
     * cache-sync errors and scan wall time. Scan time is excluded from rotate/
     * flush_us, but included in end-to-end refresh_us. No allocation or ISR scan. */
    uint32_t verify_checks,verify_errors;
    uint64_t verify_mismatches,verify_us;
    uint32_t touch_reads,touch_read_gap_max_us;
    /* First locally sampled/dequeued input in the rendered batch. Excludes
     * physical/controller latency; LCD completion is copy EOF, NOT vsync.
     * Render/PPA/verification and scheduling may still delay touch polling. */
    uint32_t sample_render_count,sample_render_max_us,sample_lcd_count,sample_lcd_max_us;
    uint64_t sample_render_us,sample_lcd_us;
    uint32_t ppa_timeouts,lcd_timeouts,submit_errors;
    uint32_t pipeline_state; /* 0 IDLE, 1 PPA_PENDING, 2 LCD_PENDING; timeout retains owner. */
} tab5_native_port_stats_t;

/* Queue 1..2 checks on future successful PPA frames; false for invalid count or
 * unstarted port. New requests replace the pending count. Stats reset does not
 * cancel requests. Default 0: no scan/cache sync; only an atomic count check. */
bool tab5_native_port_request_verify(unsigned frames);
esp_err_t tab5_native_port_start(tab5_ui_action_cb_t callback,void *user);
bool tab5_native_port_lock(uint32_t timeout_ms);
void tab5_native_port_unlock(void);
/* Atomic counters only, never takes the UI mutex. Reset clears counters but
 * retains pipeline_state: diagnostics cannot release DMA buffer ownership. */
void tab5_native_port_stats(tab5_native_port_stats_t *out,bool reset);
#endif
