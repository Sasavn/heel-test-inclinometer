#include "tab5_resources.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(tab5_cpu_busy_pct(1000000,2000000,2)==0);
    assert(tab5_cpu_busy_pct(1000000,0,2)==100);
    assert(tab5_cpu_busy_pct(1000000,1000000,2)==50);
    assert(tab5_cpu_busy_pct(1000000,1500000,2)==25);
    assert(tab5_cpu_busy_pct(2000000,3000000,2)==25);
    assert(tab5_cpu_busy_pct(1000000,2001000,2)==0);
    uint32_t old=UINT32_MAX-499999u,now=500000u;
    assert((uint32_t)(now-old)==1000000u);
    assert(tab5_cpu_busy_pct(1000000,(uint32_t)(now-old),2)==50);
    assert(tab5_cpu_busy_pct(0,0,2)==0);
    puts("resources: dual-core idle/busy/mixed/rounding/wrap checks PASS");
}
