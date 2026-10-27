#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* No clock setters in the worker. Manual button or one automatic boot attempt. */
typedef enum {
    TAB5_TIME_SYNC_OK, TAB5_TIME_SYNC_NO_STA, TAB5_TIME_SYNC_BUSY,
    TAB5_TIME_SYNC_DNS, TAB5_TIME_SYNC_TIMEOUT, TAB5_TIME_SYNC_IO,
    TAB5_TIME_SYNC_PACKET, TAB5_TIME_SYNC_RANGE, TAB5_TIME_SYNC_RESOURCE
} tab5_time_sync_code_t;
typedef struct {
    tab5_time_sync_code_t code;
    int64_t utc_seconds;
    int64_t received_us; /* monotonic receipt, so main can compensate queue age */
} tab5_time_sync_result_t;
/* Injectable one-shot transport; absolute monotonic deadline covers DNS+UDP.
 * Must never retain request/response stack pointers after returning. */
typedef struct {
    void *ctx;
    int64_t (*now_us)(void *ctx);
    bool (*nonce)(void *ctx,uint8_t out[8]);
    tab5_time_sync_code_t (*exchange)(void *ctx,const uint8_t request[48],
        uint8_t *response,size_t capacity,size_t *length,int64_t deadline_us,int64_t *received_us);
} tab5_time_sync_transport_t;
tab5_time_sync_result_t tab5_time_sync_fetch(const tab5_time_sync_transport_t *transport,bool sta_connected);
const char *tab5_time_sync_message(tab5_time_sync_code_t code);
/* Consume one boot attempt only after STA connects and recording/other sync end. */
bool tab5_time_sync_auto_due(bool *attempted,bool sta_connected,bool busy,bool recording);
/* Start/poll/busy are serialized by main. start refuses outstanding completion.
 * On false start, no worker ran; main chooses NO_STA/BUSY/RESOURCE status. */
bool tab5_time_sync_start(bool sta_connected);
bool tab5_time_sync_busy(void);
bool tab5_time_sync_poll(tab5_time_sync_result_t *out);
