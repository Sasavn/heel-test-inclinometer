#ifndef TAB5_LEVEL_H
#define TAB5_LEVEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define TAB5_LEVEL_WIDTH 200
#define TAB5_LEVEL_HEIGHT 200
/* False leaves outputs untouched. Radial clamp 5 degrees, 18 pixels/degree,
 * center (100,100), positive Y upward. Coordinates use nearest-integer rounding (half away from zero). */
bool tab5_level_position(float x,float y,bool valid,int *px,int *py);
typedef struct {
    uint16_t card_bg,alt_circle_bg,border,sensor_color;
    float x,y;
    bool valid;
} tab5_level_t;
/* Caller owns stride*200 uint16_t pixels, native-endian RGB565. stride is
 * pixels, >=200; padding untouched. Invalid args: false, no writes.
 * No allocation, LVGL, AA or ellipse. Invalid sensor draws static gauge only. */
bool tab5_level_draw(uint16_t *pixels,size_t stride,const tab5_level_t *level);
#endif
