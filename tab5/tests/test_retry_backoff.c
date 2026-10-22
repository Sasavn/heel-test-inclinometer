#include "tab5_retry_backoff.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    tab5_retry_backoff_t b;
    uint64_t now=1000;
    tab5_retry_backoff_reset(&b,now);
    assert(!tab5_retry_backoff_due(&b,now+1999));
    assert(tab5_retry_backoff_due(&b,now+2000));
    const unsigned delays[]={2000,4000,8000,16000,30000,30000,30000};
    for(unsigned i=0;i<sizeof(delays)/sizeof(delays[0]);i++) {
        /* A slow mount may take seconds. No immediate catch-up at its end. */
        now+=5000;
        tab5_retry_backoff_failed(&b,now);
        assert(b.wait_ms==delays[i]);
        assert(!tab5_retry_backoff_due(&b,now));
        assert(!tab5_retry_backoff_due(&b,now+delays[i]-1));
        assert(tab5_retry_backoff_due(&b,now+delays[i]));
        now+=delays[i];
    }
    /* Mounted/re-enabled reset discards a saturated failure history. */
    tab5_retry_backoff_reset(&b,now);
    assert(!tab5_retry_backoff_due(&b,now+1999));
    assert(tab5_retry_backoff_due(&b,now+2000));
    tab5_retry_backoff_failed(&b,now+2000);
    assert(b.wait_ms==2000&&b.next_wait_ms==4000);
    /* 64-bit uptime past the 32-bit millisecond wrap; no unsigned early retry. */
    now=(uint64_t)UINT32_MAX+1000;
    tab5_retry_backoff_reset(&b,now);
    assert(!tab5_retry_backoff_due(&b,now-1));
    assert(!tab5_retry_backoff_due(&b,now+1999));
    assert(tab5_retry_backoff_due(&b,now+2000));
    puts("SD backoff: 2/4/8/16/30s cap, completion-relative deadlines, reset, 64-bit uptime PASS");
    return 0;
}
