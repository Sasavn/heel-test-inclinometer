/*
 * sim_main.c — сценарий симулятора: строит интерфейс из Core/Src/ui, крутит
 * «энкодер» и сохраняет снимки экрана в out/ (BMP, build.sh переводит в PNG).
 *
 * Собирается для обоих видов меню (UI_MENU_STYLE, см. Makefile):
 *   1 (список) — полный сценарий: главный экран, карточки с качкой, меню,
 *                общие экраны (карта памяти, справка, дата, адрес); снимки
 *                <тема>_NN_*, меню — <тема>_menuA_*;
 *   2 (плитки) — только меню: плитки, подменю, правка, самые длинные
 *                значения; снимки <тема>_menuB_*.
 * Сценарий проходится в тёмной (dark_*) и в светлой (light_*) теме; в виде 1
 * тема переключается пунктом меню «Тема» — так проверяется перекраска готовых
 * экранов. Между проходами сравнивается занятая куча LVGL (утечки).
 *
 * На каждом снимке проверяется раскладка (check_bar, check_tree): элементы
 * не наезжают друг на друга, не выходят за родителя, тексты меток с
 * обрезкой (многоточие) помещаются целиком.
 *
 * Запуск: tools/ui_sim/build.sh   (снимки — tools/ui_sim/out/)
 */
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "app.h"
#include "ui.h"
#include "sim.h"

#ifndef UI_MENU_STYLE
#define UI_MENU_STYLE 1
#endif

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
static void check_tree(lv_obj_t *obj, const char *what);

static void shot(const char *name) {
	char path[256];
	snprintf(path, sizeof(path), "%s/%s_%s.bmp", out_dir, prefix, name);
	sim_screenshot(path);
	snprintf(path, sizeof(path), "%s_%s", prefix, name);
	check_bar(path);
	check_tree(lv_screen_active(), path);
}

#if UI_MENU_STYLE != 2
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
#endif

static size_t heap_used(const char *what) {
	lv_mem_monitor_t mon;
	lv_mem_monitor(&mon);
	printf("  куча LVGL (%s): всего %lu, занято %lu, максимум %lu (%u%%), фрагментация %u%%\n",
			what, (unsigned long) mon.total_size,
			(unsigned long) (mon.total_size - mon.free_size), (unsigned long) mon.max_used,
			mon.used_pct, mon.frag_pct);
	return mon.total_size - mon.free_size;
}

/*--------------------------------------------------------------------
 * Проверки раскладки
 *--------------------------------------------------------------------*/
static bool visible(lv_obj_t *o, lv_area_t *a) {
	lv_obj_get_coords(o, a);
	return !lv_obj_is_hidden(o) && lv_area_get_width(a) > 0;
}

// Полоса сверху активного экрана (строка состояния / заголовок) — его первый
// дочерний объект: зазор между элементами не меньше BAR_MIN_GAP, все внутри
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
		if (!visible(a, &ca))
			continue;
		char msg[160];
		if (ca.x1 < ba.x1 || ca.x2 > ba.x2 || ca.y1 < ba.y1 || ca.y2 > ba.y2) {
			snprintf(msg, sizeof(msg), "%s: элемент %lu за краем полосы (x %ld..%ld, y %ld..%ld)",
					what, (unsigned long) i, (long) ca.x1, (long) ca.x2, (long) ca.y1,
					(long) ca.y2);
			check(false, msg);
		}
		for (uint32_t j = 0; j < n; j++) {
			lv_obj_t *b = lv_obj_get_child(bar, (int32_t) j);
			lv_area_t cb;
			if (j == i || !visible(b, &cb) || cb.x1 < ca.x1)
				continue;
			int32_t gap = cb.x1 - ca.x2 - 1; // b правее a
			if (gap < min_gap)
				min_gap = gap;
			if (gap < BAR_MIN_GAP) {
				snprintf(msg, sizeof(msg), "%s: элементы полосы %lu и %lu: зазор %ld px", what,
						(unsigned long) i, (unsigned long) j, (long) gap);
				check(false, msg);
			}
		}
	}
	if (min_gap < 6)
		printf("  [%s] полоса: наименьший зазор %ld px\n", what, (long) min_gap);
}

static const char* obj_text(lv_obj_t *o) {
	return lv_obj_check_type(o, &lv_label_class) ? lv_label_get_text(o) : "";
}

// Все объекты экрана: дети одного родителя не перекрываются, не выходят за
// него по горизонтали (у прокручиваемых — вертикаль не в счёт), метки с
// многоточием/обрезкой показывают текст целиком. Строки меток включают
// пустые поля сверху и снизу, поэтому касание строк по вертикали до
// OVERLAP_TOL px — не наезд (настоящий наезд — почти на всю строку)
#define OVERLAP_TOL 4
static void check_tree(lv_obj_t *obj, const char *what) {
	lv_obj_update_layout(obj);
	char msg[200];
	lv_area_t pa;
	lv_obj_get_coords(obj, &pa);
	uint32_t n = lv_obj_get_child_count(obj);
	for (uint32_t i = 0; i < n; i++) {
		lv_obj_t *a = lv_obj_get_child(obj, (int32_t) i);
		lv_area_t ca;
		if (!visible(a, &ca))
			continue;
		if (ca.x1 < pa.x1 || ca.x2 > pa.x2) {
			snprintf(msg, sizeof(msg), "%s: «%s» выходит за родителя (x %ld..%ld из %ld..%ld)",
					what, obj_text(a), (long) ca.x1, (long) ca.x2, (long) pa.x1, (long) pa.x2);
			check(false, msg);
		}
		if (lv_obj_check_type(a, &lv_label_class)) {
			lv_label_long_mode_t m = lv_label_get_long_mode(a);
			// Многоточие LVGL вписывает прямо в текст метки («...»; в наших
			// строках многоточие — один символ «…»)
			if (m == LV_LABEL_LONG_MODE_DOTS && strstr(lv_label_get_text(a), "...") != NULL) {
				snprintf(msg, sizeof(msg), "%s: «%s» обрезан многоточием", what,
						lv_label_get_text(a));
				check(false, msg);
			}
			if (m == LV_LABEL_LONG_MODE_DOTS || m == LV_LABEL_LONG_MODE_CLIP) {
				lv_point_t sz;
				lv_text_get_size(&sz, lv_label_get_text(a), lv_obj_get_style_text_font(a, 0), 0,
						0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
				if (sz.x > lv_obj_get_content_width(a)) {
					snprintf(msg, sizeof(msg), "%s: «%s» обрезан (%ld > %ld px)", what,
							lv_label_get_text(a), (long) sz.x, (long) lv_obj_get_content_width(a));
					check(false, msg);
				}
			}
		}
		for (uint32_t j = i + 1; j < n; j++) {
			lv_obj_t *b = lv_obj_get_child(obj, (int32_t) j);
			lv_area_t cb;
			if (!visible(b, &cb))
				continue;
			int32_t ov_y = LV_MIN(ca.y2, cb.y2) - LV_MAX(ca.y1, cb.y1) + 1;
			if (ca.x1 <= cb.x2 && cb.x1 <= ca.x2 && ov_y > OVERLAP_TOL) {
				snprintf(msg, sizeof(msg), "%s: «%s» и «%s» перекрываются", what, obj_text(a),
						obj_text(b));
				check(false, msg);
			}
		}
		check_tree(a, what);
	}
}

/*--------------------------------------------------------------------
 * Энкодер
 *--------------------------------------------------------------------*/
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

// Кнопки главного экрана (левая ячейка с корабликом в группу не входит)
enum { C_FREQ = 0, C_ZERO, C_MENU };

// Пункты меню (ui_settings.c)
#if UI_MENU_STYLE == 2
enum { T_SENS = 0, T_ROLL, T_POWER, T_SD, T_DEV, T_BACK };       // плитки
enum { S_FREQ = 0, S_ALPHA, S_GAP, S_ADDR, S_ZERO, S_BACK };     // «Датчики»
enum { R_WIN = 0, R_RATE, R_CALM, R_HYST, R_BACK };              // «Качка»
enum { P_BAT = 0, P_BACK };                                      // «Питание»
enum { D_TIME = 0, D_THEME, D_HELP, D_BACK };                    // «Прибор»
#else
enum { A_TIME = 0, A_SD, A_ALPHA, A_RWIN, A_RRATE, A_RCALM, A_RHYST, A_ADDR, A_ZERO, A_GAP,
	A_BAT, A_THEME, A_HELP, A_BACK };
#endif

static void open_menu(void) {
	focus_to(C_MENU);
	sim_click();
}

static void set_time(uint8_t d, uint8_t mo, uint8_t y, uint8_t h, uint8_t mi, uint8_t s) {
	g_app.time = (app_time_t ) { .year = y, .month = mo, .date = d, .hours = h, .minutes = mi,
			.seconds = s };
}

#if UI_MENU_STYLE != 2
/*--------------------------------------------------------------------
 * Общие экраны (вид 1; в виде 2 — те же, проверяются короче)
 *--------------------------------------------------------------------*/
// Карта памяти: экран уже открыт; готова / идёт запись / нет карты
static void sd_screens(void) {
	printf("sd\n");
	sim_run(200);
	shot("16_sd_ready");
	g_app.sd_state = SD_RECORDING;
	g_app.rec_sensor_mask = 0x01; // Д3 без связи — его файла ещё нет
	g_app.rec_rows = 1340;
	g_app.rec_start_ms = sim_now() - 67000;
	g_app.sd_free_mb = 7398;
	sim_run(300);
	shot("16b_sd_recording");
	g_app.sd_state = SD_NO_CARD;
	g_app.sd_err = 3;
	g_app.file_number = 0;
	g_app.rec_sensor_mask = 0;
	g_app.sd_total_mb = 0;
	g_app.sd_free_mb = APP_SD_FREE_UNKNOWN;
	sim_run(300);
	shot("16c_sd_nocard");
	g_app.sd_state = SD_READY;
	g_app.sd_err = 0;
	g_app.file_number = 7;
	g_app.sd_total_mb = 7580;
	g_app.sd_free_mb = 7412;
	sim_run(200);
	sim_click(); // «Назад»
}

// Справка: экран уже открыт; начало, прокрутка, конец, нажатие — назад
static void help_screens(void) {
	printf("help\n");
	sim_run(200);
	shot("17_help_top");
	sim_rotate(4);
	shot("17b_help_scrolled");
	sim_rotate(40);
	shot("17c_help_end");
	sim_click();
}

/*--------------------------------------------------------------------
 * Главный экран: строка состояния, карточки, качка, органы управления
 *--------------------------------------------------------------------*/
static void scenario_main(void) {
	// 1. Простой: Д2 в покое по обеим осям, Д3 потерян; фокус на «Частота»
	printf("main_idle\n");
	sim_run(300);
	shot("01_main_idle");

	// Нагрузка: идут часы, значения не меняются
	measure_flush("часы", 1000);
	// Нагрузка: меняются сотые обоих датчиков каждые 100 мс + часы
	g_app.sensor[1].status = SENSOR_OK;
	sim_app_roll(1, 0.4f, 0.2f, true, true, 20);
	sim_app_noise = true;
	measure_flush("углы+часы", 1000);
	sim_app_noise = false;
	g_app.sensor[0].x = 0.123f;
	g_app.sensor[0].y = -1.234f;
	g_app.sensor[1].x = 0.31f;
	g_app.sensor[1].y = -1.18f;

	// 2. Идёт запись: мигает ЗАП, оба датчика; качка у Д2 по Y; опрос не успевает
	printf("main_recording\n");
	g_app.sd_state = SD_RECORDING;
	g_app.rec_sensor_mask = 0x03;
	g_app.rec_start_ms = sim_now();
	g_app.rec_switch_on = true;
	sim_app_roll(0, 0.21f, 1.23f, true, false, 20);
	sim_app_roll(1, 0.45f, 0.18f, true, true, 20);
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

	// 3. Качка по осям: Д2 в покое, Д3 качается только по X
	printf("cards\n");
	g_app.sd_state = SD_READY;
	g_app.rec_sensor_mask = 0;
	g_app.rec_switch_on = false;
	g_app.log_freq_hz = 10;
	g_app.actual_rate_hz = 10.0f;
	g_app.diag.cpu_load_pct = 18;
	sim_app_roll(0, 0.21f, 0.34f, true, true, 20);
	sim_app_roll(1, 2.10f, 0.40f, false, true, 20);
	sim_run(300);
	shot("03_cards_mixed");
	check(!g_app.is_stable && g_app.stab_sensor == 1 && g_app.stab_axis == 0,
			"сводка: худшая ось — Д3 X");
	// Самые длинные строки: «−12.75°» и «качка 12.34°»; Д3 — битые ответы
	g_app.sensor[0].x = -12.75f;
	g_app.sensor[0].y = 45.67f;
	sim_app_roll(0, 12.34f, 3.21f, false, false, 20);
	g_app.sensor[1].status = SENSOR_ABSENT;
	g_app.sensor[1].garbled_count = 5;
	g_app.sensor[1].last_garbled_ms = sim_now();
	sim_run(300);
	shot("03b_cards_long");
	// Окно качки ещё набирается (после включения)
	g_app.sensor[1].status = SENSOR_OK;
	g_app.sensor[1].garbled_count = 0;
	g_app.sensor[0].x = 0.123f;
	g_app.sensor[0].y = -1.234f;
	sim_app_roll(0, 0.10f, 0.12f, false, false, 12);
	sim_app_roll(1, 0.30f, 0.05f, false, false, 12);
	sim_run(300);
	shot("03c_cards_filling");
	sim_app_roll(0, 0.21f, 0.34f, true, true, 20);
	sim_app_roll(1, 0.40f, 0.20f, true, true, 20);

	// 4. Правка частоты: нажать, +3 щелчка (10 -> 13 Гц)
	printf("edit_freq\n");
	focus_to(C_FREQ);
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
}

// Главный экран: нет карты / сбой карты / битые ответы
static void scenario_main_variants(void) {
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
	sim_app_roll(1, 12.34f, 0.9f, false, true, 20);
	g_app.sensor[1].x = -88.88f;
	g_app.sensor[1].y = 45.67f;
	sim_run(300);
	shot("11_main_sderror");

	// Д3 не отвечает, но на шине битые ответы — «дубль адреса?»; самые широкие
	// значения строки состояния: «НЕТ SD» здесь не бывает, но «СТОП» +
	// «ЦП 100%» + «12.6 В»
	g_app.sd_state = SD_READY;
	g_app.diag.cpu_load_pct = 100;
	sim_app_battery(12.6f, 100);
	g_app.sensor[0].status = SENSOR_OK;
	sim_app_roll(0, 0.21f, 0.34f, true, true, 20);
	g_app.sensor[1].status = SENSOR_ABSENT;
	g_app.sensor[1].garbled_count = 17;
	g_app.sensor[1].last_garbled_ms = sim_now();
	sim_run(300);
	shot("12_main_garbled");
	// Через UI_GARBLED_HINT_MS без битых ответов подсказка уходит
	sim_run(3500);
	shot("12b_main_absent");
	// Самая длинная плашка записи «НЕТ SD» рядом с «ЦП 100%»
	g_app.sd_state = SD_NO_CARD;
	sim_run(300);
	shot("12c_main_nosd_cpu100");
	g_app.sd_state = SD_READY;
}

/*--------------------------------------------------------------------
 * Меню-список (вид 1) и общие экраны через него
 *--------------------------------------------------------------------*/
static void scenario_menu_list(void) {
	// Меню с главного экрана: начало списка
	printf("menu\n");
	open_menu();
	shot("menuA_01_top");

	// Фильтр α: нажать (правка), +3 (0.15 -> 0.18), снимок, нажать (готово)
	printf("alpha\n");
	focus_to(A_ALPHA);
	sim_click();
	sim_rotate(3);
	shot("menuA_02_edit_alpha");
	sim_click();
	check(g_app.ema_alpha > 0.175f && g_app.ema_alpha < 0.185f, "α должен быть 0.18");

	// Порог покоя: +2 (1.50 -> 1.60°), обратно
	focus_to(A_RCALM);
	sim_click();
	sim_rotate(2);
	shot("menuA_02b_edit_roll_calm");
	sim_click();
	check(g_app.roll_calm_deg > 1.595f && g_app.roll_calm_deg < 1.605f, "порог покоя 1.60°");
	sim_click();
	sim_rotate(-2);
	sim_click();

	// Порог АКБ: -5 (10.0 -> 9.5 В) и обратно
	focus_to(A_BAT);
	sim_click();
	sim_rotate(-5);
	shot("menuA_03_edit_bat");
	sim_click();
	check(g_app.bat_alarm_v > 9.45f && g_app.bat_alarm_v < 9.55f, "порог АКБ должен быть 9.5 В");
	sim_click();
	sim_rotate(5);
	sim_click();

	// Конец списка: «Справка», «Назад»; полоса прокрутки внизу
	focus_to(A_HELP);
	sim_run(100);
	shot("menuA_04_bottom");

	// Справка и карта памяти — общие экраны; «Назад» возвращает в список
	sim_click();
	help_screens();
	check(focused_index() == A_HELP, "после справки фокус на «Справка»");
	focus_to(A_SD);
	sim_click();
	sd_screens();
	check(focused_index() == A_SD, "после карты фокус на «Карта памяти»");

	// Дата и время
	printf("time\n");
	focus_to(A_TIME);
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
	sim_click();   // OK -> app_set_time, назад в меню
	shot("07d_menu_after_ok");

	// Адрес датчика
	printf("addr\n");
	focus_to(A_ADDR);
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

	// Нет часов DS3231: пункт меню и экран даты; АКБ разряжена (тревога)
	printf("no_rtc\n");
	g_app.rtc_present = false;
	sim_app_battery(9.6f, 0);
	focus_to(A_TIME);
	sim_run(200);
	shot("09_menu_no_rtc");
	sim_click();
	shot("09b_time_no_rtc");
	focus_to(6); // «Отмена»
	sim_click();
	g_app.rtc_present = true;
	sim_app_battery(11.8f, 76);

	// Самые длинные значения: дата, «НЕТ КАРТЫ», α 0.99, порог 28.8 В, 50 мс
	printf("menu_longest\n");
	app_time_t saved = g_app.time;
	set_time(31, 12, 99, 23, 59, 0);
	g_app.sd_state = SD_NO_CARD;
	g_app.ema_alpha = 0.99f;
	g_app.bat_alarm_v = 28.8f;
	g_app.bus_gap_ms = 50;
	g_app.roll_window_s = 60;
	g_app.roll_calm_deg = 10.0f;
	g_app.roll_hyst_deg = 2.0f;
	focus_to(A_TIME);
	sim_run(200);
	shot("menuA_05_longest_top");
	focus_to(A_RHYST);
	sim_run(200);
	shot("menuA_05b_longest_middle");
	focus_to(A_BACK);
	sim_run(200);
	shot("menuA_06_longest_bottom");
	g_app.sd_state = SD_ERROR;
	g_app.sd_err = 13;
	focus_to(A_SD);
	sim_run(200);
	shot("menuA_07_sd_error");
	g_app.time = saved;
	g_app.sd_state = SD_READY;
	g_app.sd_err = 0;
	g_app.ema_alpha = 0.18f;
	g_app.bat_alarm_v = 10.0f;
	g_app.bus_gap_ms = 5;
	g_app.roll_window_s = 20;
	g_app.roll_calm_deg = 1.5f;
	g_app.roll_hyst_deg = 0.2f;
	sim_app_battery(11.8f, 76);

	focus_to(A_BACK);
	sim_click(); // «Назад» -> главный экран
}

static void scenario_theme_toggle(void) {
	printf("theme_toggle\n");
	open_menu();
	focus_to(A_THEME);
	sim_stats_t before = sim_stats;
	sim_click();
	printf("  смена темы: %lu пикс на дисплей\n",
			(unsigned long) (sim_stats.pixels - before.pixels));
	shot("13_menu_theme_toggled");
	focus_to(A_BACK);
	sim_click(); // на главный экран — уже в новой теме
	sim_run(200);
	shot("14_main_after_toggle");
}

#else
/*--------------------------------------------------------------------
 * Меню-плитки (вид 2)
 *--------------------------------------------------------------------*/
static void sub_back(int idx) {
	focus_to(idx);
	sim_click();
}

static void scenario_menu_tiles(bool longest) {
	const char *sfx = longest ? "_longest" : "";
	char name[64];
	printf("tiles%s\n", sfx);
	open_menu();
	snprintf(name, sizeof(name), "menuB_01_tiles%s", sfx);
	shot(name);

	// Датчики: правка α
	focus_to(T_SENS);
	sim_click();
	snprintf(name, sizeof(name), "menuB_02_sensors%s", sfx);
	shot(name);
	if (!longest) {
		focus_to(S_ALPHA);
		sim_click();
		sim_rotate(3);
		shot("menuB_02b_sensors_edit_alpha");
		sim_click();
		check(g_app.ema_alpha > 0.175f && g_app.ema_alpha < 0.185f, "α должен быть 0.18");
	}
	sub_back(S_BACK);
	check(focused_index() == T_SENS, "из «Датчиков» фокус на плитке «Датчики»");

	// Качка: правка окна (+1 щелчок = +5 с)
	focus_to(T_ROLL);
	sim_click();
	snprintf(name, sizeof(name), "menuB_02c_roll%s", sfx);
	shot(name);
	if (!longest) {
		focus_to(R_WIN);
		sim_click();
		sim_rotate(1);
		shot("menuB_02d_roll_edit_window");
		sim_click();
		check(g_app.roll_window_s == 25, "окно качки 25 с");
		sim_click();
		sim_rotate(-1);
		sim_click();
	}
	sub_back(R_BACK);

	// Питание: правка порога, тревога
	focus_to(T_POWER);
	sim_click();
	snprintf(name, sizeof(name), "menuB_03_power%s", sfx);
	shot(name);
	if (!longest) {
		focus_to(P_BAT);
		sim_click();
		sim_rotate(-5);
		shot("menuB_03b_power_edit_bat");
		sim_click();
		sim_click();
		sim_rotate(5);
		sim_click();
		sim_app_battery(9.6f, 0);
		sim_run(200);
		shot("menuB_03c_power_alarm");
		sim_app_battery(4.0f, APP_BAT_PCT_NONE);
		sim_run(200);
		shot("menuB_03d_power_usb");
		sim_app_battery(11.8f, 76);
	}
	sub_back(P_BACK);

	// SD-карта: общий экран, «Назад» — к плиткам
	focus_to(T_SD);
	sim_click();
	sim_run(200);
	snprintf(name, sizeof(name), "menuB_04_sd%s", sfx);
	shot(name);
	sim_click();
	check(focused_index() == T_SD, "после карты фокус на плитке «SD-карта»");

	// Прибор: дата и время -> «Отмена» возвращает в «Прибор»; справка
	focus_to(T_DEV);
	sim_click();
	snprintf(name, sizeof(name), "menuB_05_device%s", sfx);
	shot(name);
	if (!longest) {
		focus_to(D_TIME);
		sim_click();
		focus_to(6); // «Отмена»
		sim_click();
		check(focused_index() == D_TIME, "из даты — обратно в «Прибор»");
		focus_to(D_HELP);
		sim_click();
		sim_run(200);
		shot("menuB_06_help");
		sim_click();
		check(focused_index() == D_HELP, "после справки фокус на «Справка»");
	}
	sub_back(D_BACK);

	focus_to(T_BACK);
	sim_click(); // на главный экран
}
#endif

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
	size_t used1 = 0, used2 = 0;

#if UI_MENU_STYLE != 2
	// Проход 1: тёмная тема, в конце переключение на светлую
	printf("=== dark ===\n");
	prefix = "dark";
	scenario_main();
	scenario_menu_list();
	scenario_main_variants();
	scenario_theme_toggle();
	check(g_app.theme == APP_THEME_LIGHT, "после прохода 1 тема должна стать светлой");
	used1 = heap_used("после прохода 1");

	// Проход 2: светлая тема (те же экраны, перекрашены), в конце — обратно в тёмную
	sim_app_init(APP_THEME_LIGHT);
	printf("=== light ===\n");
	prefix = "light";
	scenario_main();
	scenario_menu_list();
	scenario_main_variants();
	scenario_theme_toggle();
	check(g_app.theme == APP_THEME_DARK, "после прохода 2 тема должна стать тёмной");
	used2 = heap_used("после прохода 2");

	// Тема, сменённая не из меню (g_app.theme), применяется на следующем обновлении
	app_set_theme(APP_THEME_LIGHT);
	sim_run(200);
	prefix = "light";
	shot("15_main_external_theme");
#else
	for (int pass = 0; pass < 2; pass++) {
		sim_app_init(pass == 0 ? APP_THEME_DARK : APP_THEME_LIGHT);
		sim_run(300);
		prefix = pass == 0 ? "dark" : "light";
		printf("=== %s (плитки) ===\n", prefix);
		scenario_menu_tiles(false);
		// Самые длинные значения
		set_time(31, 12, 99, 23, 59, 0);
		g_app.sd_state = SD_ERROR;
		g_app.sd_err = 13;
		g_app.ema_alpha = 0.99f;
		g_app.log_freq_hz = 50;
		g_app.bat_alarm_v = 28.8f;
		g_app.bus_gap_ms = 50;
		g_app.roll_window_s = 60;
		g_app.roll_calm_deg = 10.0f;
		g_app.roll_hyst_deg = 2.0f;
		sim_app_battery(12.6f, 100);
		scenario_menu_tiles(true);
		if (pass == 0)
			used1 = heap_used("после прохода 1");
		else
			used2 = heap_used("после прохода 2");
	}
#endif

	printf("  занято кучи: %lu -> %lu -> %lu\n", (unsigned long) used0,
			(unsigned long) used1, (unsigned long) used2);
	check(used2 <= used1 + 64, "куча растёт от прохода к проходу");
	printf(errors ? "done, %d errors\n" : "done\n", errors);
	return errors ? 1 : 0;
}
