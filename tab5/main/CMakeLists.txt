idf_component_register(SRCS
    "main.c" "tab5_board.c" "tab5_measure.c" "tab5_ui.c" "tab5_services.c"
    "font_tab5_20.c" "font_tab5_28.c" "font_tab5_64.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_timer app_update json esp_driver_uart esp_driver_gpio
             esp_driver_usb_serial_jtag esp_driver_i2c esp_wifi esp_netif
             esp_event lwip fatfs sdmmc mbedtls)
