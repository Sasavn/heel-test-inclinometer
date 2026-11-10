#include "lvgl.h"
#include "app.h"
uint32_t HAL_GetTick(void){return lv_tick_get();}
void settings_touch(uint32_t now){(void)now;}
#define APP_INSTRUMENT_SIM 1
#include "../../Core/Src/instrument.c"
#include "../../Core/Src/relative.c"
