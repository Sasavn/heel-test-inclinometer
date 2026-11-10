/* Source extension for the pinned MSC VFS, not a duplicate opaque structure.
 * Upstream license headers remain in the included source. */
#ifndef TAB5_MSC_VFS_SOURCE
#define TAB5_MSC_VFS_SOURCE "upstream/src/msc_host_vfs.c"
#endif
#include TAB5_MSC_VFS_SOURCE
#include "usb/msc_host_vfs_drive.h"

esp_err_t msc_host_vfs_get_drive(msc_host_vfs_handle_t handle, uint8_t *out_pdrv)
{
    if (out_pdrv) {
        *out_pdrv = 0xff;
    }
    if (!handle || !out_pdrv) {
        return ESP_ERR_INVALID_ARG;
    }
    /* This project pins IDF 5.5.2: the real register() above stores pdrv in
     * this exact handle, and unregister() releases that same drive. */
    if (handle->pdrv >= FF_VOLUMES ||
        handle->drive[0] != (char)('0' + handle->pdrv) ||
        handle->drive[1] != ':' || handle->drive[2] != '\0' ||
        !handle->base_path) {
        return ESP_ERR_INVALID_STATE;
    }
    *out_pdrv = handle->pdrv;
    return ESP_OK;
}
