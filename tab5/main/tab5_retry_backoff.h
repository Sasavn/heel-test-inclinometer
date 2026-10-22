#ifndef TAB5_RETRY_BACKOFF_H
#define TAB5_RETRY_BACKOFF_H
#include <stdbool.h>
#include <stdint.h>

/* All times are monotonic milliseconds. Owner provides synchronization.
 * Failure delays are measured from COMPLETION, never from mount start. */
typedef struct { uint64_t at_ms; uint32_t wait_ms, next_wait_ms; } tab5_retry_backoff_t;
static inline void tab5_retry_backoff_reset(tab5_retry_backoff_t *b,uint64_t now)
{
    *b=(tab5_retry_backoff_t){.at_ms=now,.wait_ms=2000,.next_wait_ms=2000};
}
static inline bool tab5_retry_backoff_due(const tab5_retry_backoff_t *b,uint64_t now)
{
    return now>=b->at_ms && now-b->at_ms>=b->wait_ms;
}
static inline void tab5_retry_backoff_failed(tab5_retry_backoff_t *b,uint64_t now)
{
    uint32_t delay=b->next_wait_ms?b->next_wait_ms:2000;
    b->at_ms=now;b->wait_ms=delay;
    b->next_wait_ms=delay>=15000?30000:delay*2;
}
#endif
