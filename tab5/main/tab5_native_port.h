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
    /* Successful external-buffer LCD submits; excludes protocol/DMA overhead. */
    uint64_t lcd_copy_bytes;
    uint32_t full_lcd_copies,partial_lcd_copies;
    uint32_t renders,flushes,refreshes,refresh_max_us,errors;
    /* Sampled touch / dequeued encoder -> CPU render start. Excludes producer
     * delay, touch I2C, PPA and LCD time; not end-to-end encoder latency. */
    uint32_t interactive_renders,input_render_wait_max_us;
    /* Explicit diagnostic: complete full-frame checks, differing RGB565 pixels,
     * cache-sync errors. verify_us includes diagnostic landscape materialization
     * before LCD and the full scan after copy EOF; the latter is excluded from
     * refresh EOF timing. No allocation or ISR scan. */
    uint32_t verify_checks,verify_errors;
    uint64_t verify_mismatches,verify_us;
    /* Actual physical read starts only, >=10000us apart; no latency guarantee. */
    uint32_t touch_reads,touch_read_gap_max_us;
    /* First locally sampled/dequeued input in the rendered batch. Excludes
     * physical/controller latency; LCD completion is copy EOF, NOT vsync.
     * Render/PPA-submit/verification and scheduling may still delay touch polling.
     * DMA callbacks wake the task; idle touch polling uses a bounded 10ms wait. */
    uint32_t sample_render_count,sample_render_max_us,sample_lcd_count,sample_lcd_max_us;
    uint64_t sample_render_us,sample_lcd_us;
    uint32_t ppa_timeouts,lcd_timeouts,submit_errors;
    /* Sequential composition: accepted portrait DMA2D copies + exact CPU-edge PPA.
     * rotate_us includes DMA/PPA and the final bounded CPU phase; these counters separate their costs. */
    uint32_t compose_frames,compose_copy_jobs,compose_ppa_jobs,compose_fallbacks,compose_timeouts;
    uint64_t compose_copy_bytes,compose_copy_us,compose_ppa_us;
    /* Final CPU tile phase: <=16384 pixels/rect, <=32768/frame, after all
     * DMA EOFs. Time includes exact-line M2C preparation + CPU transpose. */
    /* Successful full portrait C2M calls/bytes, and immutable-generation skips. */
    uint32_t compose_source_sync_jobs,compose_source_sync_hits;
    uint64_t compose_source_sync_bytes;
    uint32_t compose_cpu_jobs;
    uint64_t compose_cpu_pixels,compose_cpu_us;
    /* Normal-build wall time. Producer input includes benchmark/encoder
     * control + touch/FIFO dispatch; lock wait excludes external main locks.
     * Wake wait belongs to the dedicated transfer worker; includes scheduling
     * delay and overlaps in-flight DMA time.
     * Callback delay is ISR timestamp -> task observes completion, before
     * verifier/next submit; aggregate equals the three stage counters.
     * These wall counters overlap transport timings: DO NOT sum as phases. */
    uint64_t touch_read_us,cpu_tick_us,input_dispatch_us,ui_lock_wait_us,wake_wait_us;
    uint64_t callback_process_delay_us,cache_callback_process_delay_us;
    uint64_t ppa_callback_process_delay_us,lcd_callback_process_delay_us;
    uint32_t compose_unaligned_views; /* Accepted GPU views/offsets outside the old16px grid. */
    uint32_t overlap_renders,queued_peak,worker_completed_frames;
    uint64_t overlap_us; /* CPU renderer wall interval intersecting active transfer frame, ending at LCD ISR. */
    bool worker_enabled; /* Two CPU slots, one transfer worker; reset preserves this flag. */
    uint32_t producer_backpressure; /* UI render wanted, both CPU slots retained. */
    uint32_t pipeline_state; /* 0 IDLE, 1 PPA_PENDING, 2 LCD_PENDING, 3 CACHE_PENDING; timeout retains owner. */
} tab5_native_port_stats_t;

/* Queue 1..2 checks on future successfully copied frames; false for invalid count or
 * unstarted port. New requests replace the pending count. Stats reset does not
 * cancel requests. False if single driver-FB query is unavailable/invalid.
 * Default 0: no reference memcpy/materialization or full-frame scan.
 * Diagnostic first frame reconstructs FULL independent CPU reference; second
 * slot can clone that CPU baseline before a partial render, never portrait. */
bool tab5_native_port_request_verify(unsigned frames);
/* Experimental CPU transpose, default OFF. Task-context atomic accessors;
 * captured once per next composition plan, never changes live jobs. No NVS. */
void tab5_native_port_set_cpu_tiles_enabled(bool enabled);
bool tab5_native_port_cpu_tiles_enabled(void);
/* Reasons: 0 none, 1 complete, 2 physical input, 3 cancel, 4 deadline,
 * 5 UI unavailable, 6 pipeline error. Deadline is 25s, observed by UI task;
 * render/driver calls cannot be preempted. Main gates requests !recording.
 * Physical touch abort is swallowed until release, then the UI is restored. */
typedef struct {
    bool pending,active,finished,restored,cache_enabled;
    uint32_t request_id,requested_frames,completed_frames,accepted_frames,interval_us,reason,touch_reads,fps_milli;
    int32_t scroll_min,scroll_max;
    uint64_t started_us,elapsed_us,setup_us,cold_frame_us;
    uint64_t render_us,rotate_us,lcd_submit_us,lcd_wait_us,refresh_us;
    uint64_t lcd_copy_bytes; /* Completed benchmark frames only. */
} tab5_native_scroll_benchmark_status_t;
bool tab5_native_port_request_scroll_benchmark(uint32_t frames,uint32_t interval_us,bool cache_enabled);
void tab5_native_port_cancel_scroll_benchmark(void);
void tab5_native_port_scroll_benchmark_status(tab5_native_scroll_benchmark_status_t *out);
/* Optional hooks: UI task/mutex only. Missing hooks allows baseline off only.
 * clear makes first frame cold. Old enable state, not payloads, is restored. */
bool tab5_native_port_set_scroll_cache_hooks(bool (*get_enabled)(void),void (*set_enabled)(bool),void (*clear)(void));

esp_err_t tab5_native_port_start(tab5_ui_action_cb_t callback,void *user);
bool tab5_native_port_lock(uint32_t timeout_ms);
void tab5_native_port_unlock(void);
/* Atomic counters only, never takes the UI mutex. Reset clears counters but
 * retains pipeline_state: diagnostics cannot release DMA buffer ownership. */
void tab5_native_port_stats(tab5_native_port_stats_t *out,bool reset);
#endif
