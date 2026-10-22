idf_component_register(SRCS
    "main.c" "tab5_board.c" "tab5_measure.c" "tab5_ui.c" "tab5_services.c" "tab5_panel.c" "tab5_time.c"
    "tab5_ui_graph.c" "font_tab5_16.c" "font_tab5_24.c" "font_tab5_40.c" "font_tab5_20.c" "font_tab5_28.c" "font_tab5_64.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_timer app_update json esp_driver_uart esp_driver_gpio
             esp_driver_usb_serial_jtag esp_driver_i2c esp_wifi esp_netif
             esp_event lwip fatfs sdmmc mbedtls
    PRIV_REQUIRES espressif__esp_lcd_st7123 espressif__esp_lcd_ili9881c
                  espressif__esp_lcd_touch_st7123 espressif__esp_lcd_touch_gt911)
foreach(fn esp_lcd_new_dsi_bus esp_lcd_new_panel_st7123 esp_lcd_new_panel_ili9881c
           esp_lcd_touch_new_i2c_st7123 esp_lcd_touch_new_i2c_gt911
           esp_lcd_panel_reset esp_lcd_panel_init esp_lcd_panel_disp_on_off)
    target_link_options(${COMPONENT_LIB} INTERFACE "LINKER:--wrap=${fn}")
endforeach()
