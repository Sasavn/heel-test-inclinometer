#ifndef TAB5_PLOT_H
#define TAB5_PLOT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define TAB5_PLOT_WIDTH 1040
#define TAB5_PLOT_HEIGHT 364
#define TAB5_PLOT_SERIES 4
#define TAB5_PLOT_MAX_POINTS 1200
typedef struct {
    int32_t y_min,y_max,none;
    unsigned ticks,n;
    uint16_t background,grid,colors[TAB5_PLOT_SERIES];
    const int32_t *series[TAB5_PLOT_SERIES];
    bool visible[TAB5_PLOT_SERIES];
} tab5_plot_t;
/* Native-endian RGB565, not byte-swapped. stride is in uint16_t pixels.
 * Caller owns at least stride*HEIGHT pixels. Padding is untouched. Invalid
 * arguments return false without writes. No allocation, global state or LVGL.
 * Series share n (0..1200); null/hidden series are skipped. Only adjacent real
 * points form solid 2px segments; none breaks lines, with no extrapolation.
 * Later series overwrite earlier ones. ticks is capped at HEIGHT. */
bool tab5_plot_draw(uint16_t *pixels,size_t stride,const tab5_plot_t *plot);
#endif
