/*
 * lv_port_disp.c — дисплей LVGL на ILI9341 (SPI1, без DMA) и тик LVGL.
 *
 * Частичная отрисовка: LVGL рисует полосу 320 x UI_DISP_BUF_LINES строк
 * в статический буфер, flush отправляет её в окно дисплея блокирующим
 * HAL_SPI_Transmit. Пока идёт передача, суперцикл стоит (DMA не используется).
 *
 * Оценка: полоса 320x20 = 12.8 КБ; при SCK 48 МГц это ~2.1 мс на шине
 * (реально ~2.5–3 мс с накладными HAL). Обычное обновление (4 значения
 * углов + часы) — до ~43 КБ, т.е. ~9 мс SPI за 100 мс (tools/ui_sim).
 */
#include "lv_port.h"
#include "main.h"
#include "ili9341.h"

// Высота полосы отрисовки, строк. 20 строк = 12.8 КБ RAM.
#define UI_DISP_BUF_LINES  20

// Буфер отрисовки: RGB565, выровнен на 4 байта (LV_DRAW_BUF_ALIGN)
static uint8_t disp_buf[ILI9341_WIDTH * UI_DISP_BUF_LINES * 2] __attribute__((aligned(4)));

// Передать готовую полосу на дисплей
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
	uint32_t w = (uint32_t) lv_area_get_width(area);
	uint32_t h = (uint32_t) lv_area_get_height(area);

	// LVGL хранит RGB565 младшим байтом вперёд, ILI9341 ждёт старший первым
	lv_draw_rgb565_swap(px_map, w * h);

	ILI9341_DrawBitmap((uint16_t) area->x1, (uint16_t) area->y1, (uint16_t) w,
			(uint16_t) h, px_map);

	lv_display_flush_ready(disp);
}

// Источник времени LVGL — системный тик HAL (1 мс)
static uint32_t disp_tick_get(void) {
	return HAL_GetTick();
}

lv_display_t* lv_port_disp_init(void) {
	lv_tick_set_cb(disp_tick_get);

	lv_display_t *disp = lv_display_create(ILI9341_WIDTH, ILI9341_HEIGHT);
	lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
	lv_display_set_buffers(disp, disp_buf, NULL, sizeof(disp_buf),
			LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_flush_cb(disp, disp_flush);
	return disp;
}
