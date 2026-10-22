#ifndef TAB5_DRAW_PROFILE_H
#define TAB5_DRAW_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_DRAW_PROFILE_COUNT 8

typedef struct {
    const char *name; /* Static string, valid for the lifetime of the firmware. */
    uint64_t us;
    uint64_t calls;
} tab5_draw_profile_entry_t;

typedef struct {
    tab5_draw_profile_entry_t entries[TAB5_DRAW_PROFILE_COUNT];
} tab5_draw_profile_snapshot_t;

/* Copy completed-call counters atomically; reset clears them AFTER the copy.
 * NULL out is allowed (e.g. reset only). Calls in flight are counted when they
 * finish, including their full duration across a reset. Timings are inclusive
 * WALL time, including preemption/waits, not task CPU time. Nested wrappers
 * overlap: do not sum the entries as exclusive rendering time.
 * No allocations, logging, LVGL lock or hardware access.
 *
 * Link with all eight --wrap=lv_draw_sw_NAME options:
 * fill border box_shadow image label arc line layer.
 * GNU wrapping intercepts undefined linker references; same-object/internal
 * or LTO-inlined calls may bypass it. Check map/call counts on the real build.
 */
void tab5_draw_profile_snapshot(tab5_draw_profile_snapshot_t *out, bool reset);

#ifdef __cplusplus
}
#endif
#endif
