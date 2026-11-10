#ifndef TAB5_BACKLIGHT_H
#define TAB5_BACKLIGHT_H
#include <stdint.h>
#include <stdbool.h>
typedef enum { TAB5_BL_IDLE, TAB5_BL_TOUCH_DOWN, TAB5_BL_TOUCH_UP, TAB5_BL_ROTATE, TAB5_BL_PRESS } tab5_bl_event_t;
typedef struct { uint64_t last_ms; uint32_t timeout_s; bool asleep, touch, swallow; } tab5_backlight_t;
/* Caller supplies nondecreasing monotonic milliseconds. No GPIO or brightness writes.
 * Apply asleep ? 0 : saved_manual_brightness after every step. Consume means do
 * not forward the input to UI. Map physical encoder press once, before SHORT/LONG
 * synthesis; suppress any derived click/long event from a consumed press. */
bool tab5_backlight_timeout_valid(uint32_t seconds);
void tab5_backlight_init(tab5_backlight_t *p, uint64_t now_ms);
bool tab5_backlight_configure(tab5_backlight_t *p, uint32_t seconds, uint64_t now_ms);
bool tab5_backlight_step(tab5_backlight_t *p, uint64_t now_ms, tab5_bl_event_t event);
#endif
