#ifndef TAB5_UI_H
#define TAB5_UI_H
#include "tab5_measure.h"
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { TAB5_UI_START, TAB5_UI_STOP, TAB5_UI_ZERO, TAB5_UI_ZERO_RESET,
    TAB5_UI_SETTINGS, TAB5_UI_NETWORK, TAB5_UI_TIME } tab5_ui_action_kind_t;
typedef struct {
    bool ap_enabled, sta_enabled;
    char ap_ssid[33], ap_password[65], sta_ssid[33], sta_password[65];
} tab5_ui_network_t;
typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} tab5_ui_datetime_t;
typedef struct {
    tab5_ui_action_kind_t kind;
    tab5_measure_config_t config;
    bool light_theme;
    tab5_ui_network_t network;
    tab5_ui_datetime_t datetime; /* TAB5_UI_TIME: main validates and writes RTC */
} tab5_ui_action_t;
/* Runs in LVGL context. Borrowed action: copy to main's queue before returning.
 * main owns validation, persistence, recording and network services. */
typedef void (*tab5_ui_action_cb_t)(const tab5_ui_action_t *action, void *user);
typedef struct {
    bool known, mounted, recording, failed;
    uint64_t written;
} tab5_ui_medium_t;

typedef struct {
    bool known, ap_started, sta_connected;
    char ap_ip[48], sta_ip[48];
    uint16_t tcp_port;
    int32_t error; /* zero means no error */
} tab5_ui_link_t;
typedef struct {
    bool known, time_valid, battery_present;
    char datetime[32]; /* already formatted local time; no parsing by UI */
    float battery_v;
} tab5_ui_clock_t;

typedef struct {
    bool known;
    uint64_t free_bytes, total_bytes;
    bool details_known, estimate_known;
    char filesystem[16], folder[48];
    uint32_t remaining_s;
} tab5_ui_capacity_t;
typedef struct {
    bool known, start_known;
    char name[40];
    int64_t start_unix;
    uint32_t duration_s;
    uint64_t rows;
    uint8_t media_mask; /* bit 0 SD, bit 1 USB */
    bool closed_ok;
} tab5_ui_recent_t;
typedef struct {
    tab5_ui_link_t link;
    tab5_ui_clock_t clock;
    char firmware[32]; /* FW_VERSION supplied by main; read-only */
    char panel[160]; /* actual panel/timing captured by main; read-only */
    tab5_ui_medium_t sd, usb; /* known=false until main supplies service snapshots */
    tab5_measure_snapshot_t measurement;
    bool recording, light_theme;
    tab5_ui_network_t network;
    char status[160], network_status[160], clock_power[96];
    /* SPEC 22.10: append-only state contract. Unknown is never inferred. */
    bool now_known;
    uint64_t now_us; /* monotonic, same time base as sensor.timestamp_us */
    bool rec_known;
    char rec_file[40];
    uint64_t rec_started_us;
    bool rec_duration_known;
    uint32_t rec_duration_s;
    tab5_ui_capacity_t capacity[2]; /* [0] SD, [1] USB */
    bool tcp_clients_known, tcp_peer_known;
    uint16_t tcp_clients;
    char tcp_peer[48];
    bool recent_known, recent_partial;
    uint8_t recent_count;
    tab5_ui_recent_t recent[5];
    bool datetime_known;
    tab5_ui_datetime_t datetime;
    bool action_result_known;
    uint32_t action_result_serial;
    tab5_ui_action_kind_t action_result_kind;
    bool action_result_success;
    /* CPU: backend average across both cores, 0..100. Memory: internal heap
     * and PSRAM separately. A zero total is unknown even when resources_known. */
    bool resources_known, cpu_known;
    uint8_t cpu_load_pct;
    uint32_t ram_used_bytes, ram_total_bytes;
    uint32_t psram_used_bytes, psram_total_bytes;
} tab5_ui_state_t;
/* All calls require BSP LVGL lock (or the LVGL thread). Display already started
 * and rotated to 1280x720 by main. No task or display created here; a short-lived stepper repeat timer is UI-owned.
 * Generated font_tab5_16/16m/20/20m/24/32/40/64 and tab5_ui_graph/time must be linked. */
void tab5_ui_init(tab5_ui_action_cb_t callback, void *user);
void tab5_ui_update(const tab5_ui_state_t *state);
void tab5_ui_destroy(void);
#ifdef __cplusplus
}
#endif
#endif
