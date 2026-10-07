/*
 * lv_port.h — привязка LVGL к железу.
 *
 * Вся работа с HAL (SPI1/ILI9341, TIM3, PA0, HAL_GetTick) — только в
 * lv_port_disp.c и lv_port_indev.c. Интерфейс (ui*.c) железа не касается,
 * поэтому симулятор tools/ui_sim подставляет свои реализации этих функций.
 */
#ifndef LV_PORT_H_
#define LV_PORT_H_

#include "lvgl.h"

// Создать дисплей LVGL 320x240 (частичная отрисовка в статический буфер)
// и источник времени. Вызывать сразу после lv_init().
lv_display_t* lv_port_disp_init(void);

// Создать устройство ввода «энкодер» (TIM3 + кнопка PA0).
lv_indev_t* lv_port_indev_init(void);

#endif /* LV_PORT_H_ */
