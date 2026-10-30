idf_component_register(SRCS "tab5_input.c" "tab5_input_gpio.c"
    INCLUDE_DIRS "." REQUIRES esp_driver_gpio esp_timer freertos)
