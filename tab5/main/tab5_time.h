#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
/* Portable validation shared by the touch action's backend and host tests.
 * The board RTC stores 2000..2099. No normalization of invalid dates allowed. */
bool tab5_time_fields_valid(int year,int month,int day,int hour,int minute,int second);
/* Fixed offsets, no DST and no process-global TZ dependency. UTC=0 is valid. */
bool tab5_time_offset_valid(int minutes);
void tab5_time_set_offset(int minutes); /* main only; readers use atomic snapshot */
int tab5_time_offset(void);
bool tab5_time_local_to_utc(const struct tm *local,int minutes,int64_t *utc);
bool tab5_time_utc_to_local(int64_t utc,int minutes,struct tm *local);
bool tab5_time_local_now(time_t utc,struct tm *local);
/* Arguments after "set time ": legacy local calendar, optional " tz=180".
 * Exact grammar, no normalization/trailing junk; outputs only on success. */
bool tab5_time_parse_command(const char *args,int current_offset,struct tm *local,
    int *offset,bool *offset_set);
typedef bool (*tab5_time_system_fn)(void *ctx,int64_t utc);
typedef bool (*tab5_time_rtc_fn)(void *ctx,const struct tm *local);
typedef struct { bool valid,system_ok,rtc_ok; } tab5_time_apply_result_t;
/* RTC is local wall time for compatibility; system is always canonical UTC.
 * Both calendars are validated before any side effect. Partial success is explicit. */
tab5_time_apply_result_t tab5_time_apply(int64_t utc,int minutes,
    tab5_time_system_fn system,tab5_time_rtc_fn rtc,void *ctx);
