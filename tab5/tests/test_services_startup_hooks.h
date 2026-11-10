#pragma once
/* Shared fixture definitions only. Huygens supplies test_mount_hook at the
 * actual host mount I/O boundary. No duplicate backend globals or logic. */
#include <stdbool.h>
#ifndef ESP_ERR_TIMEOUT
#define ESP_ERR_TIMEOUT 0x107
#endif
typedef struct {
 unsigned mounts[2],medium_lock_entries;
 int mount_error[2];
 int filesystem[2];
 bool pause_sd,inflight_checked;
} tab5_services_startup_probe_t;
