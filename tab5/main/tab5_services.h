#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define TAB5_SERVICE_LINE_MAX 1200
#define TAB5_SERVICE_CSV_HEADER "Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms\n"
typedef enum { TAB5_SD = 0, TAB5_USB = 1 } tab5_medium_t;
typedef struct {
 unsigned sensor; /* Modbus address 2 or 3. */
 char date[11], time[9];
 double raw_x, raw_y, offset_x, offset_y, calc_x, calc_y, battery;
 uint32_t ms;
} tab5_service_sample_t;
typedef struct {
 bool mounted, recording, failed;
 uint32_t queued, written, dropped;
 int error;
} tab5_medium_status_t;
/* Reply is one UTF-8 line WITHOUT LF; transport appends LF. Context is borrowed
 * for the synchronous call only. Application emits OK to terminate replies. */
typedef void (*tab5_service_reply_fn)(void *ctx, const char *line);
typedef void (*tab5_service_command_fn)(void *ctx, const char *line,
                                      tab5_service_reply_fn reply, void *reply_ctx);
typedef struct {
 bool ap_enabled, sta_enabled;
 char ap_ssid[33], ap_password[65];
 char sta_ssid[33], sta_password[65];
 char auth_token[65];
 uint16_t tcp_port;
} tab5_network_config_t;
typedef struct {
 bool initialized, ap_started, sta_connected, tcp_listening;
 char sta_ip[16], ap_ip[16];
 uint32_t clients;
 int last_error;
} tab5_network_snapshot_t;
typedef struct {
 tab5_service_command_fn command;
 void *command_ctx;
 tab5_network_config_t network;
} tab5_services_config_t;
/* Config is copied. Main initializes NVS and board BSP before init.
 * command runs synchronously on network task; protect shared measurement state.
 * USB/radio/netif/listener startup failures are optional and reported in status.
 * Core allocation/task creation failures are fatal. Disabled AP+STA skips C6. */
esp_err_t tab5_services_init(const tab5_services_config_t *config);
esp_err_t tab5_services_network_configure(const tab5_network_config_t *config);
void tab5_services_network_snapshot(tab5_network_snapshot_t *out);
esp_err_t tab5_services_record_start(const char *session);
void tab5_services_record_stop(void);
esp_err_t tab5_services_submit(const tab5_service_sample_t *sample);
void tab5_services_status(tab5_medium_t medium, tab5_medium_status_t *out);
esp_err_t tab5_services_retry(tab5_medium_t medium);
/* Diagnostic control of background mount attempts only. Defaults to true.
 * Does not unmount, stop recording, or cancel an already-started mount.
 * Enabling resets SD backoff (first retry after 2s); disabling preserves it.
 * Explicit tab5_services_retry() remains available while auto retry is off. */
esp_err_t tab5_services_set_auto_retry(tab5_medium_t medium,bool enabled);
/* Route console OTA here BEFORE application parser. Returns true if consumed.
 * Main owns console authentication; TCP service authenticates before callback.
 * owner must be nonzero and unique per live connection. */
bool tab5_services_line(uint32_t owner, const char *line, bool authenticated,
                        tab5_service_reply_fn reply, void *ctx);
void tab5_services_disconnect(uint32_t owner);
/* Transport calls after all queued reply bytes (including LF) have drained.
 * For console call only after usb_serial_jtag_wait_tx_done succeeds. */
void tab5_services_response_flushed(uint32_t owner);
/* Call only when transport has room for one complete line (<= 512 bytes).
 * Emits at most one line. Pumps files/get without blocking on transport. */
void tab5_services_poll(uint32_t owner, tab5_service_reply_fn reply, void *ctx);
/* Nonblocking bounded broadcast of a line WITHOUT LF to authenticated TCP. */
esp_err_t tab5_services_publish(const char *line);
/* Call only after board startup self-test succeeds. */
esp_err_t tab5_services_confirm_boot(void);
int tab5_services_format_csv(char *out, size_t capacity,
                             const tab5_service_sample_t *sample);
/* Round ties to even, shared by CSV and R sample lines. digits = 0..3.
 * Nonfinite values map to zero; finite values saturate at 999999. */
int32_t tab5_services_scaled(double value, unsigned digits);
#ifdef __cplusplus
}
#endif
