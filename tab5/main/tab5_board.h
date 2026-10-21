#pragma once
#include <stdbool.h>
#include <time.h>
void tab5_board_init(void);
bool tab5_board_time_valid(void);
bool tab5_board_rtc_valid(void);
bool tab5_board_set_time(const struct tm *value);
/* NAN means absent/unreadable; the Tab5 is 2S, never apply STM32 3S curve. */
float tab5_board_battery(void);
