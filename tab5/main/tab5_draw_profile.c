#include "tab5_draw_profile.h"
#include "src/draw/sw/lv_draw_sw.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum { FILL, BORDER, BOX_SHADOW, IMAGE, LABEL, ARC, LINE, LAYER };
static const char *const names[TAB5_DRAW_PROFILE_COUNT] = {
    "fill", "border", "box_shadow", "image", "label", "arc", "line", "layer"
};
static struct { uint64_t us, calls; } counters[TAB5_DRAW_PROFILE_COUNT];
static portMUX_TYPE counter_lock = portMUX_INITIALIZER_UNLOCKED;

static void completed(unsigned index, int64_t start)
{
    int64_t end = esp_timer_get_time();
    uint64_t elapsed = end >= start ? (uint64_t)(end - start) : 0;
    portENTER_CRITICAL(&counter_lock);
    counters[index].us += elapsed;
    counters[index].calls++;
    portEXIT_CRITICAL(&counter_lock);
}

void tab5_draw_profile_snapshot(tab5_draw_profile_snapshot_t *out, bool reset)
{
    portENTER_CRITICAL(&counter_lock);
    for(unsigned i = 0; i < TAB5_DRAW_PROFILE_COUNT; i++) {
        if(out) {
            out->entries[i].name = names[i];
            out->entries[i].us = counters[i].us;
            out->entries[i].calls = counters[i].calls;
        }
        if(reset) counters[i].us = counters[i].calls = 0;
    }
    portEXIT_CRITICAL(&counter_lock);
}

/* Declarations match the pinned LVGL 9.3 lv_draw_sw.h, including non-const
 * fill descriptor and the two-argument line entry point. Never hold our lock
 * while calling the renderer: it may call another instrumented primitive. */
#define WRAP_AREA(name, type, qualifier, index) \
    extern void __real_lv_draw_sw_##name(lv_draw_task_t *, qualifier type *, const lv_area_t *); \
    void __wrap_lv_draw_sw_##name(lv_draw_task_t *t, qualifier type *dsc, const lv_area_t *coords) \
    { \
        int64_t start = esp_timer_get_time(); \
        __real_lv_draw_sw_##name(t, dsc, coords); \
        completed(index, start); \
    }

WRAP_AREA(fill, lv_draw_fill_dsc_t, , FILL)
WRAP_AREA(border, lv_draw_border_dsc_t, const, BORDER)
WRAP_AREA(box_shadow, lv_draw_box_shadow_dsc_t, const, BOX_SHADOW)
WRAP_AREA(image, lv_draw_image_dsc_t, const, IMAGE)
WRAP_AREA(label, lv_draw_label_dsc_t, const, LABEL)
WRAP_AREA(arc, lv_draw_arc_dsc_t, const, ARC)
WRAP_AREA(layer, lv_draw_image_dsc_t, const, LAYER)

extern void __real_lv_draw_sw_line(lv_draw_task_t *, const lv_draw_line_dsc_t *);
void __wrap_lv_draw_sw_line(lv_draw_task_t *t, const lv_draw_line_dsc_t *dsc)
{
    int64_t start = esp_timer_get_time();
    __real_lv_draw_sw_line(t, dsc);
    completed(LINE, start);
}
