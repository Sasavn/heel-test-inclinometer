/*
 * sim_app.c — поддельная логика прибора для симулятора: g_app и app_*().
 * Датчик Д2 отвечает, Д3 потерян (сценарий меняет состояние сам).
 */
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "sim.h"

app_state_t g_app;
bool sim_app_noise;

static uint32_t last_sec_ms;
static uint32_t svc_done_ms;

void sim_app_init(uint8_t theme) {
	memset(&g_app, 0, sizeof(g_app));
	for (int i = 0; i < APP_SENSOR_COUNT; i++)
		g_app.sensor[i].addr = APP_SENSOR_ADDR(i);

	g_app.sensor[0].status = SENSOR_OK;
	g_app.sensor[0].x = 0.123f;
	g_app.sensor[0].y = -1.234f;
	g_app.sensor[1].status = SENSOR_LOST;
	g_app.sensor[1].x = 2.506f;
	g_app.sensor[1].y = -12.75f;

	g_app.log_freq_hz = 10;
	g_app.ema_alpha = 0.15f;
	g_app.actual_rate_hz = 10.0f;
	g_app.bus_gap_ms = 5;
	g_app.theme = theme;
	g_app.sd_state = SD_READY;
	g_app.file_number = 7;
	g_app.bat_alarm_v = APP_BAT_ALARM_DEFAULT_V;
	sim_app_battery(11.8f, 76);
	g_app.is_stable = true;
	g_app.stab_span = 0.08f;
	g_app.stab_sensor = 0;
	g_app.time = (app_time_t ) { .year = 26, .month = 10, .date = 6, .hours = 14,
			.minutes = 35, .seconds = 12 };
	g_app.rtc_present = true;
	g_app.diag.cpu_load_pct = 18; // простой: опрос 10 Гц, запись не идёт
	sim_app_noise = false;
}

// АКБ: present и тревога — как в логике (порог с гистерезисом, от USB тревоги
// нет); заряд задаёт сценарий (в логике он по кривой разряда 18650)
static void battery_eval(void) {
	g_app.battery_present = g_app.battery_v >= APP_BAT_PRESENT_MIN_V;
	if (!g_app.battery_present) {
		g_app.battery_low = false;
		g_app.battery_pct = APP_BAT_PCT_NONE;
	} else if (g_app.battery_v < g_app.bat_alarm_v) {
		g_app.battery_low = true;
	} else if (g_app.battery_v > g_app.bat_alarm_v + APP_BAT_ALARM_HYST_V) {
		g_app.battery_low = false;
	}
}

void sim_app_battery(float volts, uint8_t pct) {
	g_app.battery_v = volts;
	g_app.battery_pct = pct;
	battery_eval();
}

void sim_app_tick(uint32_t now) {
	// Часы
	if (now - last_sec_ms >= 1000) {
		last_sec_ms = now;
		app_time_t *t = &g_app.time;
		if (++t->seconds >= 60) {
			t->seconds = 0;
			if (++t->minutes >= 60) {
				t->minutes = 0;
				t->hours = (uint8_t) ((t->hours + 1) % 24);
			}
		}
	}
	// «Шум» датчиков — меняются сотые, как в жизни
	if (sim_app_noise && now % 100 == 0) {
		g_app.sensor[0].x = 0.12f + (float) ((now / 100) % 7) * 0.01f;
		g_app.sensor[0].y = -1.23f - (float) ((now / 100) % 5) * 0.01f;
		g_app.sensor[1].x = 0.31f - (float) ((now / 100) % 3) * 0.01f;
		g_app.sensor[1].y = -1.18f + (float) ((now / 100) % 4) * 0.01f;
	}
	// Смена адреса «выполняется» 600 мс, потом нет ответа
	if (g_app.svc_state == SVC_BUSY && now - svc_done_ms >= 600)
		g_app.svc_state = SVC_FAIL;
}

void app_set_log_freq(uint16_t hz) {
	printf("  app_set_log_freq(%u)\n", hz);
	g_app.log_freq_hz = hz;
	g_app.actual_rate_hz = (float) hz; // шина успевает
}

void app_set_bus_gap(uint8_t ms) {
	printf("  app_set_bus_gap(%u)\n", ms);
	g_app.bus_gap_ms = ms;
}

void app_set_bat_alarm(float volts) {
	printf("  app_set_bat_alarm(%.1f)\n", (double) volts);
	g_app.bat_alarm_v = volts;
	battery_eval();
}

void app_set_theme(uint8_t theme) {
	printf("  app_set_theme(%u)\n", theme);
	g_app.theme = theme;
}

void app_set_ema_alpha(float alpha) {
	printf("  app_set_ema_alpha(%.2f)\n", (double) alpha);
	g_app.ema_alpha = alpha;
}

void app_zero_all(void) {
	printf("  app_zero_all()\n");
	for (int i = 0; i < APP_SENSOR_COUNT; i++) {
		if (g_app.sensor[i].status == SENSOR_OK) {
			g_app.sensor[i].off_x = g_app.sensor[i].x;
			g_app.sensor[i].off_y = g_app.sensor[i].y;
			g_app.sensor[i].x = 0.0f;
			g_app.sensor[i].y = 0.0f;
		}
	}
}

void app_zero_reset(void) {
	printf("  app_zero_reset()\n");
	for (int i = 0; i < APP_SENSOR_COUNT; i++) {
		g_app.sensor[i].x += g_app.sensor[i].off_x;
		g_app.sensor[i].y += g_app.sensor[i].off_y;
		g_app.sensor[i].off_x = 0.0f;
		g_app.sensor[i].off_y = 0.0f;
	}
}

void app_set_time(const app_time_t *t) {
	printf("  app_set_time(%02u.%02u.%02u %02u:%02u:%02u)\n", t->date, t->month, t->year,
			t->hours, t->minutes, t->seconds);
	g_app.time = *t;
}

void app_sensor_set_address(uint8_t old_addr, uint8_t new_addr) {
	printf("  app_sensor_set_address(%u -> %u)\n", old_addr, new_addr);
	g_app.svc_state = SVC_BUSY;
	svc_done_ms = sim_now();
}
