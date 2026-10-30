idf_component_register(SRCS
    "main.c" "tab5_board.c" "tab5_measure.c" "tab5_ui.c" "native/native_ui.c" "tab5_native_port.c" "tab5_services.c" "tab5_panel.c" "tab5_time.c" "tab5_time_sync.c" "tab5_draw_profile.c"
    "tab5_ui_graph.c" "tab5_plot.c" "tab5_level.c" "tab5_backlight.c" "native/font_native_fallback14.c"
    "font_tab5_16.c" "font_tab5_16m.c" "font_tab5_20.c" "font_tab5_20m.c"
    "font_tab5_24.c" "font_tab5_32.c" "font_tab5_40.c" "font_tab5_64.c"
    "font_tab5_28.c" "font_tab5_36.c" "font_tab5_48.c" "font_tab5_56.c" "font_tab5_108.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_timer app_update json esp_driver_uart esp_driver_gpio
             esp_driver_usb_serial_jtag esp_driver_i2c esp_wifi esp_netif
             esp_event lwip fatfs sdmmc mbedtls tab5_board_support tab5_input esp_driver_ppa esp_lcd
    PRIV_REQUIRES esp_mm espressif__esp_lcd_st7123 espressif__esp_lcd_ili9881c
                  espressif__esp_lcd_touch_st7123 espressif__esp_lcd_touch_gt911)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    NUI_PROFILE_FINE_COUNTERS=$<BOOL:${CONFIG_TAB5_NATIVE_FINE_PROFILE}>
    NUI_PROFILE_PRIMITIVE_TIMING=$<BOOL:${CONFIG_TAB5_NATIVE_PRIMITIVE_TIMING}>)
foreach(fn esp_lcd_new_dsi_bus esp_lcd_new_panel_st7123 esp_lcd_new_panel_ili9881c
           esp_lcd_touch_new_i2c_st7123 esp_lcd_touch_new_i2c_gt911
           esp_lcd_panel_reset esp_lcd_panel_init esp_lcd_panel_disp_on_off)
    target_link_options(${COMPONENT_LIB} INTERFACE "LINKER:--wrap=${fn}")
endforeach()

# Audited private framebuffer-copy ABI: ESP-IDF 5.5.2 only. Use vendor header.
if(NOT "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}" VERSION_EQUAL "5.5.2")
    message(FATAL_ERROR "Tab5 GPU composer private esp_async_fbcpy ABI requires IDF 5.5.2")
endif()
idf_component_get_property(tab5_esp_lcd_dir esp_lcd COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${tab5_esp_lcd_dir}/priv_include")
