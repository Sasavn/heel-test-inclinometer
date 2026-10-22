#pragma once
#include <stdbool.h>
/* Portable validation shared by the touch action's backend and host tests.
 * The board RTC stores 2000..2099. No normalization of invalid dates allowed. */
bool tab5_time_fields_valid(int year,int month,int day,int hour,int minute,int second);
