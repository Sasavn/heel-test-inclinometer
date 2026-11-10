idf_component_register(SRCS "src/bsp_i2c.c" "src/bsp_io_expander.c" "src/bsp_feature_en.c" "src/bsp_storage.c" "src/bsp_usb.c" "src/bsp_display.c"
    INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_sdspi esp_driver_sdmmc fatfs vfs spiffs esp_lcd esp_io_expander usb
    PRIV_REQUIRES esp_psram esp_driver_ledc esp_io_expander_pi4ioe5v6408 esp_lcd_st7123 esp_lcd_ili9881c esp_lcd_touch_st7123 esp_lcd_touch_gt911)
