#ifndef TAB5_DRAW_PROFILE_H
#define TAB5_DRAW_PROFILE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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

/* Native primitive counters, copied under the UI task mutex. Reset starts a
 * new reporting interval without resetting renderer allocation/frame counters.
 * NULL out permits reset only. Timings are inclusive wall time, so nested
 * primitives can overlap; do not sum them as exclusive frame time.
 * If the UI mutex cannot be acquired in one second, returns the last completed
 * snapshot and does not apply the requested reset. No hardware access.
 */
void tab5_draw_profile_snapshot(tab5_draw_profile_snapshot_t *out, bool reset);
bool tab5_draw_profile_timing_enabled(void);
bool tab5_draw_profile_counters_enabled(void);
/* Disabled fine measurements are JSON null, never a measured zero. */
void tab5_draw_profile_format(char *out,size_t capacity,const tab5_draw_profile_entry_t *entry);

#ifdef __cplusplus
}
#endif
#endif
