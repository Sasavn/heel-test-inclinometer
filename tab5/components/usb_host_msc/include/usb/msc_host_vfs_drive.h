#pragma once
#include <stdint.h>
#include "usb/msc_host_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Borrow the physical drive assigned to this successfully registered VFS.
 * Caller must serialize with unregister/device teardown and keep handle alive.
 * No allocation, free-drive scan, mount or ownership transfer is performed.
 * On failure, *out_pdrv is 0xff when out_pdrv is non-NULL.
 */
esp_err_t msc_host_vfs_get_drive(msc_host_vfs_handle_t handle, uint8_t *out_pdrv);

#ifdef __cplusplus
}
#endif
