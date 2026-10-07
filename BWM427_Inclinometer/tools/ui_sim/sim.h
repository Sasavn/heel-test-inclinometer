/*
 * sim.h — симулятор интерфейса на ПК (tools/ui_sim).
 */
#ifndef SIM_H_
#define SIM_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	uint32_t flushes; // вызовов flush_cb
	uint32_t pixels;  // пикселей передано «на дисплей»
} sim_stats_t;

extern sim_stats_t sim_stats;

uint32_t sim_now(void);
void sim_run(uint32_t ms);          // прогнать суперцикл ms миллисекунд
void sim_rotate(int32_t steps);     // повернуть энкодер (+ по часовой)
void sim_click(void);               // короткое нажатие
void sim_long_press(void);          // удержание 1.3 с
int sim_screenshot(const char *path);

// sim_app.c
void sim_app_init(uint8_t theme);  // исходное состояние g_app, тема APP_THEME_*
void sim_app_tick(uint32_t now_ms);  // «логика»: часы, шум датчиков
// Напряжение АКБ и заряд, % (APP_BAT_PCT_NONE — нет АКБ); тревога — по порогу
void sim_app_battery(float volts, uint8_t pct);
// Качка датчика i по осям: размах, покой, накоплено окна (сводка — сама)
void sim_app_roll(int i, float roll_x, float roll_y, bool calm_x, bool calm_y, uint8_t fill_s);
extern bool sim_app_noise;           // шевелить значения датчиков

#endif /* SIM_H_ */
