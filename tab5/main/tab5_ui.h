#ifndef TAB5_UI_H
#define TAB5_UI_H
#include "tab5_measure.h"
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { TAB5_UI_START, TAB5_UI_STOP, TAB5_UI_ZERO, TAB5_UI_ZERO_RESET,
    TAB5_UI_SETTINGS, TAB5_UI_NETWORK } tab5_ui_action_kind_t;
typedef struct {
    bool ap_enabled, sta_enabled;
    char ap_ssid[33], ap_password[65], sta_ssid[33], sta_password[65];
} tab5_ui_network_t;
typedef struct {
    tab5_ui_action_kind_t kind;
    tab5_measure_config_t config;
    bool light_theme;
    tab5_ui_network_t network;
} tab5_ui_action_t;
/* Runs in LVGL context. Borrowed action: copy to main's queue before returning.
 * main owns validation, persistence, recording and network services. */
typedef void (*tab5_ui_action_cb_t)(const tab5_ui_action_t *action, void *user);
typedef struct {
    tab5_measure_snapshot_t measurement;
    bool recording, light_theme;
    tab5_ui_network_t network;
    char status[160], network_status[160], clock_power[96];
} tab5_ui_state_t;
/* All calls require BSP LVGL lock (or the LVGL thread). Display already started
 * and rotated to 1280x720 by main. No task, timer or display created here.
 * Fonts font_tab5_20, font_tab5_28, font_tab5_64 must be compiled by main. */
void tab5_ui_init(tab5_ui_action_cb_t callback, void *user);
void tab5_ui_update(const tab5_ui_state_t *state);
void tab5_ui_destroy(void);
#ifdef __cplusplus
}
#endif
#endif
