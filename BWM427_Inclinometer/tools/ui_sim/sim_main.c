/*
 * sim_main.c — сценарий симулятора: строит интерфейс из Core/Src/ui, крутит
 * «энкодер» и сохраняет снимки экрана в out/ (BMP, build.sh переводит в PNG).
 *
 * Сценарий проходится дважды: в тёмной теме (снимки dark_*) и в светлой
 * (light_*). В конце каждого прохода тема переключается пунктом меню «Тема»,
 * второй проход начинается уже в другой теме — так проверяется перекраска
 * готовых экранов. Между проходами сравнивается занятая куча LVGL (утечки).
 *
 * Запуск: tools/ui_sim/build.sh   (снимки — tools/ui_sim/out/)
 */
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "app.h"
#include "ui.h"
#include "sim.h"

static const char *out_dir = "out";
static const char *prefix = "dark";
static int errors;

static void check(bool ok, const char *what) {
	if (!ok) {
		printf("!! %s\n", what);
		errors++;
	}
}

static void check_bar(const char *what);

static void shot(const char *name) {
	char path[256];
	snprintf(path, sizeof(path), "%s/%s_%s.bmp", out_dir, prefix, name);
	sim_screenshot(path);
	snprintf(path, sizeof(path), "%s_%s", prefix, name);
	check_bar(path);
}

// Оценка нагрузки на SPI: пиксели, переданные за period_ms обычной работы
static void measure_flush(const char *what, uint32_t period_ms) {
	sim_stats_t before = sim_stats;
	sim_run(period_ms);
	uint32_t px = sim_stats.pixels - before.pixels;
	uint32_t fl = sim_stats.flushes - before.flushes;
	// SPI 48 МГц: 16 бит на пиксель; HAL_SPI_Transmit побайтно ~ +25 % накладных
	double spi_ms = (double) px * 16.0 / 48e6 * 1000.0 * 1.25;
	printf("  [%s] за %lu мс: %lu flush, %lu пикс (%.1f КБ) -> SPI ~%.1f мс (%.1f мс на 100 мс)\n",
			what, (unsigned long) period_ms, (unsigned long) fl, (unsigned long) px,
			px * 2 / 1024.0, spi_ms, spi_ms * 100.0 / period_ms);
}

static size_t heap_used(const char *what) {
	lv_mem_monitor_t mon;
	lv_mem_monitor(&mon);
	printf("  куча LVGL (%s): всего %lu, занято %lu, максимум %lu (%u%%), фрагментация %u%%\n",
			what, (unsigned long) mon.total_size,
			(unsigned long) (mon.total_size - mon.free_size), (unsigned long) mon.max_used,
			mon.used_pct, mon.frag_pct);
	return mon.total_size - mon.free_size;
}

// Полоса сверху активного экрана (строка состояния / заголовок меню) — его
// первый дочерний объект. Видимые элементы на ней не должны наезжать друг на
// друга (зазор не меньше BAR_MIN_GAP) и выходить за края экрана и полосы
#define BAR_MIN_GAP 3
static void check_bar(const char *what) {
	lv_obj_t *bar = lv_obj_get_child(lv_screen_active(), 0);
	lv_obj_update_layout(bar);
	lv_area_t ba;
	lv_obj_get_coords(bar, &ba);
	int32_t min_gap = INT32_MAX;
	uint32_t n = lv_obj_get_child_count(bar);
	for (uint32_t i = 0; i < n; i++) {
		lv_obj_t *a = lv_obj_get_child(bar, (int32_t) i);
		lv_area_t ca;
		lv_obj_get_coords(a, &ca);
		if (lv_obj_is_hidden(a) || lv_area_get_width(&ca) <= 0)
			continue; // скрыт или пустая метка
		char msg[128];
		if (ca.x1 < ba.x1 || ca.x2 > ba.x2 || ca.y1 < ba.y1 || ca.y2 > ba.y2) {
			snprintf(msg, sizeof(msg), "%s: элемент %lu за краем полосы (x %ld..%ld, y %ld..%ld)",
					what, (unsigned long) i, (long) ca.x1, (long) ca.x2, (long) ca.y1,
					(long) ca.y2);
			check(false, msg);
		}
		for (uint32_t j = 0; j < n; j++) {
			lv_obj_t *b = lv_obj_get_child(bar, (int32_t) j);
			lv_area_t cb;
			lv_obj_get_coords(b, &cb);
			if (j == i || lv_obj_is_hidden(b) || lv_area_get_width(&cb) <= 0
					|| cb.x1 < ca.x1)
				continue;
			int32_t gap = cb.x1 - ca.x2 - 1; // b правее a
			if (gap < min_gap)
				min_gap = gap;
			if (gap < BAR_MIN_GAP) {
				snprintf(msg, sizeof(msg), "%s: элементы %lu и %lu: зазор %ld px", what,
						(unsigned long) i, (unsigned long) j, (long) gap);
				check(false, msg);
			}
		}
	}
	printf("  [%s] полоса: наименьший зазор %ld px\n", what, (long) min_gap);
}

static lv_group_t* cur_group(void) {
	return lv_indev_get_group(lv_indev_get_next(NULL));
}

static int focused_index(void) {
	lv_group_t *g = cur_group();
	lv_obj_t *f = lv_group_get_focused(g);
	for (uint32_t i = 0; i < lv_group_get_obj_count(g); i++) {
		if (lv_group_get_obj_by_index(g, i) == f)
			return (int) i;
	}
	return -1;
}

// Крутить энкодер по щелчку, пока фокус не встанет на элемент idx
static void focus_to(int idx) {
	for (int n = 0; n < 20 && focused_index() != idx; n++)
		sim_rotate(1);
}

// Пункты меню (ui_settings.c) и кнопки главного экрана (ui_main.c)
enum { M_TIME = 0, M_ADDR, M_ZERO, M_GAP, M_BAT, M_THEME, M_BACK };
enum { C_FREQ = 0, C_ALPHA, C_ZERO, C_MENU };

// Один проход сценария. Начинается и заканчивается на главном экране.
static void scenario(void) {
	printf("=== %s ===\n", prefix);

	// 1. Главный экран, запись не идёт, фокус на «Частота» (по умолчанию)
	printf("main_idle\n");
	sim_run(300);
	shot("01_main_idle");

	// Нагрузка: идут часы, значения не меняются
	measure_flush("часы", 1000);
	// Нагрузка: меняются сотые обоих датчиков каждые 100 мс + часы
	g_app.sensor[1].status = SENSOR_OK;
	sim_app_noise = true;
	measure_flush("углы+часы", 1000);
	sim_app_noise = false;
	g_app.sensor[0].x = 0.123f;
	g_app.sensor[0].y = -1.234f;
	g_app.sensor[1].x = 0.31f;
	g_app.sensor[1].y = -1.18f;

	// 2. Идёт запись: мигает ЗАП, пишутся оба датчика, качка, опрос не успевает
	printf("main_recording\n");
	g_app.sd_state = SD_RECORDING;
	g_app.rec_sensor_mask = 0x03;
	g_app.rec_start_ms = sim_now();
	g_app.rec_switch_on = true;
	g_app.is_stable = false;
	g_app.stab_span = 1.23f;
	g_app.log_freq_hz = 20;
	g_app.actual_rate_hz = 14.3f;
	g_app.diag.cpu_load_pct = 64; // оранжевый
	sim_run(65000 - (sim_now() % 1000)); // запись идёт ~1 мин, ЗАП в фазе «горит»
	shot("02_main_recording");
	measure_flush("запись: мигание+часы", 1000);
	g_app.actual_rate_hz = 20.0f;
	sim_run(700); // ЗАП в фазе «погашен», справа время записи
	shot("02b_main_recording_time");

	// Запись на 50 Гц: процессор загружен (красный)
	printf("main_cpu_high\n");
	g_app.log_freq_hz = 50;
	g_app.actual_rate_hz = 50.0f;
	g_app.diag.cpu_load_pct = 91;
	sim_run(1000);
	shot("02c_main_cpu_high");

	// АКБ ниже порога во время записи: плашка напряжения мигает вместе с ЗАП,
	// в строке сообщений — тревога (вместо времени записи)
	printf("main_bat_alarm\n");
	g_app.log_freq_hz = 20;
	g_app.actual_rate_hz = 20.0f;
	g_app.diag.cpu_load_pct = 64;
	sim_app_battery(9.6f, 0);
	check(g_app.battery_low, "9.6 В < 10.0 В: должна быть тревога");
	sim_run(1200 - (sim_now() % 1000)); // фаза «горит»
	shot("02d_main_bat_alarm");
	sim_run(500); // фаза «погашен»
	shot("02e_main_bat_alarm_blink_off");

	// Питание от USB (АКБ не подключена): «USB» серым, тревоги нет
	printf("main_usb\n");
	sim_app_battery(4.0f, APP_BAT_PCT_NONE);
	check(!g_app.battery_low, "от USB тревоги быть не должно");
	sim_run(300);
	shot("02f_main_usb");
	sim_app_battery(11.8f, 76);

	// 3. Фокус: вправо на «Фильтр», потом обратно на «Частота»
	printf("focus_freq\n");
	g_app.sd_state = SD_READY;
	g_app.rec_sensor_mask = 0;
	g_app.rec_switch_on = false;
	g_app.is_stable = true;
	g_app.log_freq_hz = 10;
	g_app.actual_rate_hz = 10.0f;
	g_app.diag.cpu_load_pct = 18;
	sim_rotate(1);
	shot("03a_focus_alpha");
	sim_rotate(-1);
	shot("03_focus_freq");

	// 4. Правка частоты: нажать, +3 щелчка (10 -> 13 Гц)
	printf("edit_freq\n");
	sim_click();
	sim_rotate(3);
	shot("04_edit_freq");
	sim_click(); // выход из правки
	sim_rotate(-1); // в навигации — переход по пунктам, частота не меняется
	sim_rotate(1);
	check(g_app.log_freq_hz == 13, "частота должна быть 13 Гц");

	// 5. Ноль: фокус на «Ноль», нажать (задать), удержать (сброс)
	printf("zero\n");
	focus_to(C_ZERO);
	shot("05_focus_zero");
	sim_click();
	shot("05b_zero_set");
	sim_long_press();
	shot("05c_zero_reset");

	// 6. Меню: на «Меню» и нажать
	printf("menu\n");
	focus_to(C_MENU);
	sim_click();
	shot("06_menu");

	// Порог АКБ: нажать (правка), -5 щелчков (10.0 -> 9.5 В), нажать (готово);
	// потом обратно +5 (10.0 В)
	printf("bat_alarm\n");
	focus_to(M_BAT);
	sim_click();
	sim_rotate(-5);
	shot("06b_menu_edit_bat");
	sim_click();
	sim_run(200);
	shot("06c_menu_bat_set");
	check(g_app.bat_alarm_v > 9.45f && g_app.bat_alarm_v < 9.55f, "порог АКБ должен быть 9.5 В");
	sim_click();
	sim_rotate(5);
	sim_click();
	check(g_app.bat_alarm_v > 9.95f && g_app.bat_alarm_v < 10.05f, "порог АКБ должен быть 10.0 В");
	focus_to(M_TIME);

	// 7. Дата и время: первый пункт меню
	printf("time\n");
	sim_click();
	shot("07_time");
	sim_click();   // правка дня
	sim_rotate(-7);
	shot("07b_time_edit_day");
	sim_click();
	focus_to(4); // «Минуты»
	sim_click();
	sim_rotate(10);
	sim_click();
	focus_to(5); // OK
	shot("07c_time_focus_ok");
	sim_app_battery(4.0f, APP_BAT_PCT_NONE); // в заголовке меню «USB»
	sim_click();   // OK -> app_set_time, назад в меню
	shot("07d_menu_after_ok");

	// 8. Адрес датчика
	printf("addr\n");
	focus_to(M_ADDR);
	sim_click();
	shot("08_addr");
	focus_to(2); // «Записать»
	sim_click();
	shot("08b_addr_busy");
	sim_run(800);
	shot("08c_addr_fail");
	g_app.svc_state = SVC_OK;
	sim_run(200);
	shot("08d_addr_ok");
	focus_to(3); // «Назад»
	sim_click();

	// 9. Нет часов DS3231: пункт меню и экран даты; АКБ разряжена (тревога)
	printf("no_rtc\n");
	g_app.rtc_present = false;
	sim_app_battery(9.6f, 0);
	focus_to(M_GAP);
	sim_run(200);
	shot("09_menu_no_rtc");
	focus_to(M_TIME);
	sim_click();
	shot("09b_time_no_rtc");
	focus_to(6); // «Отмена»
	sim_click();
	g_app.rtc_present = true;
	focus_to(M_BACK);
	sim_click(); // «Назад» -> главный экран

	// 10. Главный экран: нет карты / ошибка SD / битые ответы; загрузка ЦП
	// с разным числом цифр (100 % — самая широкая, рядом «СТОП» и «M007»)
	printf("main_variants\n");
	g_app.sd_state = SD_NO_CARD;
	g_app.sd_err = 3;
	g_app.diag.cpu_load_pct = 7;
	sim_app_battery(11.8f, 76);
	g_app.sensor[1].status = SENSOR_OK;
	g_app.sensor[1].x = -0.5f;
	g_app.sensor[1].y = 0.004f;
	sim_run(300);
	shot("10_main_nosd");

	g_app.sd_state = SD_ERROR;
	g_app.sd_err = 1;
	g_app.diag.cpu_load_pct = 52;
	g_app.sensor[0].status = SENSOR_LOST;
	g_app.stab_sensor = 1; // опорный — Д3
	g_app.is_stable = false;
	g_app.stab_span = 12.34f;
	g_app.sensor[1].x = -88.88f;
	g_app.sensor[1].y = 45.67f;
	sim_run(300);
	shot("11_main_sderror");

	// Д3 не отвечает, но на шине битые ответы — подсказка «дубль адреса?»
	g_app.sd_state = SD_READY;
	g_app.diag.cpu_load_pct = 100;
	sim_app_battery(12.6f, 100); // самое широкое, и в заголовке меню (п. 11)
	g_app.sensor[0].status = SENSOR_OK;
	g_app.stab_sensor = 0;
	g_app.is_stable = true;
	g_app.sensor[1].status = SENSOR_ABSENT;
	g_app.sensor[1].garbled_count = 17;
	g_app.sensor[1].last_garbled_ms = sim_now();
	sim_run(300);
	shot("12_main_garbled");
	// Через UI_GARBLED_HINT_MS без битых ответов подсказка уходит
	sim_run(3500);
	shot("12b_main_absent");

	// 11. Тема пунктом меню: экраны перекрашиваются без пересоздания
	printf("theme_toggle\n");
	focus_to(C_MENU);
	sim_click();
	focus_to(M_THEME);
	sim_stats_t before = sim_stats;
	sim_click();
	printf("  смена темы: %lu пикс на дисплей\n",
			(unsigned long) (sim_stats.pixels - before.pixels));
	shot("13_menu_theme_toggled");
	focus_to(M_BACK);
	sim_click(); // на главный экран — уже в новой теме
	sim_run(200);
	shot("14_main_after_toggle");
}

int main(int argc, char **argv) {
	setvbuf(stdout, NULL, _IONBF, 0); // вывод сразу (если сценарий зависнет)
	if (argc > 1)
		out_dir = argv[1];

	sim_app_init(APP_THEME_DARK);
	ui_init();
	sim_run(300);

	sim_stats_t first = sim_stats;
	printf("  первый кадр: %lu flush, %lu пикс\n", (unsigned long) first.flushes,
			(unsigned long) first.pixels);
	size_t used0 = heap_used("после ui_init");

	// Проход 1: тёмная тема, в конце переключение на светлую
	prefix = "dark";
	scenario();
	check(g_app.theme == APP_THEME_LIGHT, "после прохода 1 тема должна стать светлой");
	size_t used1 = heap_used("после прохода 1");

	// Проход 2: светлая тема (те же экраны, перекрашены), в конце — обратно в тёмную
	sim_app_init(APP_THEME_LIGHT);
	prefix = "light";
	scenario();
	check(g_app.theme == APP_THEME_DARK, "после прохода 2 тема должна стать тёмной");
	size_t used2 = heap_used("после прохода 2");

	// Тема, сменённая не из меню (g_app.theme), применяется на следующем обновлении
	app_set_theme(APP_THEME_LIGHT);
	sim_run(200);
	prefix = "light";
	shot("15_main_external_theme");

	printf("  занято кучи: %lu -> %lu -> %lu\n", (unsigned long) used0,
			(unsigned long) used1, (unsigned long) used2);
	check(used2 <= used1 + 64, "куча растёт от прохода к проходу");
	printf(errors ? "done, %d errors\n" : "done\n", errors);
	return errors ? 1 : 0;
}
