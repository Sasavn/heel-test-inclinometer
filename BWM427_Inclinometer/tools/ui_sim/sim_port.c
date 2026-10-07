/*
 * sim_port.c — замена Core/Src/ui/lv_port_*.c для симулятора на ПК:
 * кадровый буфер вместо ILI9341, «скриптовый» энкодер вместо TIM3/PA0,
 * виртуальное время вместо HAL_GetTick, запись снимков в BMP.
 */
#include <stdio.h>
#include <string.h>
#include "lv_port.h"
#include "sim.h"
#include "ui.h"

#define SIM_W 320
#define SIM_H 240
#define SIM_BUF_LINES 20   // как в прошивке

static uint16_t fb[SIM_W * SIM_H];               // RGB565 (порядок байт хоста)
static uint8_t draw_buf[SIM_W * SIM_BUF_LINES * 2] __attribute__((aligned(4)));
static uint32_t sim_ms;
static int32_t enc_pending;
static bool btn_down;

sim_stats_t sim_stats;

static uint32_t sim_tick(void) {
	return sim_ms;
}

uint32_t sim_now(void) {
	return sim_ms;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *a, uint8_t *px) {
	int32_t w = lv_area_get_width(a);
	const uint16_t *src = (const uint16_t*) px;
	for (int32_t y = a->y1; y <= a->y2; y++) {
		memcpy(&fb[y * SIM_W + a->x1], src, (size_t) w * 2);
		src += w;
	}
	sim_stats.flushes++;
	sim_stats.pixels += (uint32_t) (w * lv_area_get_height(a));
	lv_display_flush_ready(disp);
}

lv_display_t* lv_port_disp_init(void) {
	lv_tick_set_cb(sim_tick);
	lv_display_t *disp = lv_display_create(SIM_W, SIM_H);
	lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
	lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf),
			LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_flush_cb(disp, flush_cb);
	return disp;
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
	(void) indev;
	data->enc_diff = (int16_t) enc_pending;
	enc_pending = 0;
	data->state = btn_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
	data->key = LV_KEY_ENTER;
}

lv_indev_t* lv_port_indev_init(void) {
	lv_indev_t *indev = lv_indev_create();
	lv_indev_set_type(indev, LV_INDEV_TYPE_ENCODER);
	lv_indev_set_read_cb(indev, read_cb);
	lv_indev_set_long_press_time(indev, 1000);
	return indev;
}

/*--------------------------------------------------------------------
 * Сценарий
 *--------------------------------------------------------------------*/
void sim_run(uint32_t ms) {
	// Суперцикл прошивки: ui_task() примерно каждые 5 мс
	for (uint32_t t = 0; t < ms; t += 5) {
		sim_ms += 5;
		sim_app_tick(sim_ms);
		ui_task();
	}
}

void sim_rotate(int32_t steps) {
	enc_pending += steps;
	sim_run(100);
}

void sim_click(void) {
	btn_down = true;
	sim_run(100);
	btn_down = false;
	sim_run(100);
}

void sim_long_press(void) {
	btn_down = true;
	sim_run(1300);
	btn_down = false;
	sim_run(100);
}

int sim_screenshot(const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f) {
		perror(path);
		return -1;
	}
	const uint32_t row = SIM_W * 3; // 960, кратно 4
	const uint32_t img = row * SIM_H;
	uint8_t hdr[54] = { 'B', 'M' };
	uint32_t v;
	v = 54 + img;  memcpy(&hdr[2], &v, 4);
	v = 54;        memcpy(&hdr[10], &v, 4);
	v = 40;        memcpy(&hdr[14], &v, 4);
	v = SIM_W;     memcpy(&hdr[18], &v, 4);
	v = SIM_H;     memcpy(&hdr[22], &v, 4);
	hdr[26] = 1;   // planes
	hdr[28] = 24;  // bpp
	v = img;       memcpy(&hdr[34], &v, 4);
	v = 2835;      memcpy(&hdr[38], &v, 4); memcpy(&hdr[42], &v, 4);
	fwrite(hdr, 1, sizeof(hdr), f);

	static uint8_t line[SIM_W * 3];
	for (int y = SIM_H - 1; y >= 0; y--) { // BMP хранится снизу вверх
		for (int x = 0; x < SIM_W; x++) {
			uint16_t c = fb[y * SIM_W + x];
			uint8_t r = (uint8_t) (((c >> 11) & 0x1F) * 255 / 31);
			uint8_t g = (uint8_t) (((c >> 5) & 0x3F) * 255 / 63);
			uint8_t b = (uint8_t) ((c & 0x1F) * 255 / 31);
			line[x * 3 + 0] = b;
			line[x * 3 + 1] = g;
			line[x * 3 + 2] = r;
		}
		fwrite(line, 1, sizeof(line), f);
	}
	fclose(f);
	printf("  snapshot %s\n", path);
	return 0;
}
