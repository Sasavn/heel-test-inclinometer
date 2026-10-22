#ifndef TAB5_RESOURCES_H
#define TAB5_RESOURCES_H
#include <stdint.h>

/* Mean busy fraction across all cores over a sampling window, not since boot.
 * Callers use modular deltas for the 32-bit FreeRTOS idle counters. */
static inline unsigned tab5_cpu_busy_pct(uint64_t window_us,uint64_t idle_us,unsigned cores) {
    if(!window_us||!cores)return 0;
    uint64_t capacity=window_us*cores;
    if(idle_us>=capacity)return 0;
    return (unsigned)(((capacity-idle_us)*100+capacity/2)/capacity);
}
#endif
