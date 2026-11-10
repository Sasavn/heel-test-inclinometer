/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "bsp_err_check.h"
#include "usb/usb_host.h"

#include "bsp/m5stack_tab5.h"

static const char *TAG = "M5Stack Tab5";
static TaskHandle_t usb_host_task;  // USB Host Library task

static void usb_lib_task(void *arg)
{
    while (1) {
        // Start handling system events
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_ERROR_CHECK(usb_host_device_free_all());
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "USB: All devices freed");
            // Continue handling USB events to allow device reconnection
            // The only way this task can be stopped is by calling bsp_usb_host_stop()
        }
    }
}

esp_err_t bsp_usb_host_start(bsp_usb_host_power_mode_t mode, bool limit_500mA)
{
    (void)mode;
    (void)limit_500mA;
    if (usb_host_task) {
        return ESP_ERR_INVALID_STATE;
    }

    //Install USB Host driver. Should only be called once in entire application
    ESP_LOGI(TAG, "Installing USB Host");
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
        /* Pinned P4 usb_dwc_info.controllers[0] is HS/UTMI, wired to USB-A.
         * Do not select FS (audio pins) or change the USB-C Serial/JTAG PHY. */
        .peripheral_map = 1U << 0,
    };
    esp_err_t ret = usb_host_install(&host_config);
    if (ret != ESP_OK) {
        /* In particular, INVALID_STATE may mean another owner has the host.
         * Do not alter its power, driver or task. */
        return ret;
    }

    ret = bsp_feature_enable(BSP_FEATURE_USB, true);
    if (ret != ESP_OK) {
        goto cleanup_owned_host;
    }

    // Create a task that will handle USB library events
    if (xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 10, &usb_host_task) != pdTRUE) {
        ESP_LOGE(TAG, "Creating USB host lib task failed");
        usb_host_task = NULL;
        ret = ESP_ERR_NO_MEM;
        goto cleanup_owned_host;
    }

    return ESP_OK;

cleanup_owned_host: {
        /* Only reached after our successful install. Leave VBUS alone: its
         * previous state is unknown, so we cannot claim ownership of power. */
        esp_err_t cleanup_ret = usb_host_uninstall();
        if (cleanup_ret != ESP_OK) {
            ESP_LOGE(TAG, "USB host cleanup failed: %d (start error: %d)", cleanup_ret, ret);
            return cleanup_ret;
        }
        return ret;
    }
}

esp_err_t bsp_usb_host_stop(void)
{
    usb_host_uninstall();
    if (usb_host_task) {
        vTaskSuspend(usb_host_task);
        vTaskDelete(usb_host_task);
        usb_host_task = NULL;
    }
    return ESP_OK;
}
