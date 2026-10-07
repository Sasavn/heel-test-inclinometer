/*
 * ui_sd.c — экран «Карта памяти»: состояние карты, номер замера, имена
 * файлов текущего (идёт запись) или следующего замера, время и строки
 * записи, свободное место. Только просмотр; «Назад» — туда, откуда пришли.
 * Экран временный: строится при входе, удаляется при уходе.
 *
 *  Состояние  [готова]
 *  Замер      M007 · следующий
 *  Файлы      2026-10-07_M007_D2.CSV
 *             2026-10-07_M007_D3.CSV
 *  Запись     01:07 · 1340 строк
 *  Место      7.2 ГБ свободно из 7.4 ГБ
 *                     [ < Назад ]
 *
 * Место считает логика (g_app.sd_total_mb, sd_free_mb) при монтировании и
 * после записи; во время записи — по счётчику FatFs, без чтения карты.
 */
#include "ui_internal.h"
#include "sd_logger.h"

#define SD_CAP_X        12      // подписи
#define SD_VAL_X        104     // значения
#define SD_Y0           (UI_BAR_H + 8)
#define SD_STEP         24
#define SD_FILE_STEP    19      // вторая строка файлов — ближе к первой
#define SD_LOW_MB       100     // меньше — место оранжевым

static lv_obj_t *sd_scr;
static lv_group_t *sd_grp;
static lv_obj_t *sd_state, *sd_num, *sd_file[APP_SENSOR_COUNT], *sd_rec, *sd_space;

static void sd_back_cb(lv_event_t *e) {
	(void) e;
	ui_menu_back();
}

// Подпись слева, значение — метка справа от неё
static lv_obj_t* sd_row(int32_t y, const char *caption) {
	if (caption != NULL) {
		lv_obj_t *c = ui_label_create(sd_scr, UI_FONT_SMALL, UI_C_DIM);
		lv_label_set_text_static(c, caption);
		lv_obj_set_pos(c, SD_CAP_X, y);
	}
	lv_obj_t *v = ui_label_create(sd_scr, UI_FONT_SMALL, UI_C_TEXT);
	lv_obj_set_pos(v, SD_VAL_X, y);
	return v;
}

// Экран удалён (ушли с него) — указатели больше не действительны
static void sd_delete_cb(lv_event_t *e) {
	if (lv_event_get_current_target(e) != sd_scr)
		return; // уже построен новый экран
	sd_scr = NULL;
	sd_grp = NULL;
}

static void sd_create(void) {
	sd_grp = lv_group_create();
	sd_scr = ui_screen_create_temp(sd_grp);
	lv_obj_add_event_cb(sd_scr, sd_delete_cb, LV_EVENT_DELETE, NULL);
	ui_title_create(sd_scr, LV_SYMBOL_SD_CARD, "Карта памяти");

	int32_t y = SD_Y0;
	lv_obj_t *c = ui_label_create(sd_scr, UI_FONT_SMALL, UI_C_DIM);
	lv_label_set_text_static(c, "Состояние");
	lv_obj_set_pos(c, SD_CAP_X, y);
	sd_state = ui_chip_create(sd_scr, UI_FONT_SMALL);
	lv_obj_set_pos(sd_state, SD_VAL_X - 7, y - 1); // текст плашки — вровень со значениями
	y += SD_STEP;
	sd_num = sd_row(y, "Замер");
	y += SD_STEP;
	for (int i = 0; i < APP_SENSOR_COUNT; i++) {
		sd_file[i] = sd_row(y, i == 0 ? "Файлы" : NULL);
		y += SD_FILE_STEP;
	}
	y += SD_STEP - SD_FILE_STEP;
	sd_rec = sd_row(y, "Запись");
	y += SD_STEP;
	sd_space = sd_row(y, "Место");

	ui_action_button_create(sd_scr, sd_grp, (UI_W - 148) / 2, 192, 148, 42,
			LV_SYMBOL_LEFT "  Назад", sd_back_cb);
}

void ui_sd_show(void) {
	if (!ui_screen_alive(sd_scr))
		sd_create();
	ui_show(sd_scr, sd_grp);
	ui_sd_update();
}

// МБ -> «7.2 ГБ» (от 1 ГБ) или «512 МБ»
static void fmt_mb(char *buf, size_t n, uint32_t mb) {
	if (mb >= 1024U)
		ui_fmt_fixed(buf, n, (float) mb / 1024.0f, 1, " ГБ");
	else
		lv_snprintf(buf, n, "%lu МБ", (unsigned long) mb);
}

void ui_sd_update(void) {
	char buf[48], a[16], b[16];
	if (!ui_screen_alive(sd_scr) || lv_screen_active() != sd_scr)
		return;

	// Состояние
	switch (g_app.sd_state) {
	case SD_NO_CARD:
		// Код 3 (FR_NOT_READY) — просто нет карты; другой — карта не читается
		if (g_app.sd_err != 0 && g_app.sd_err != 3)
			lv_snprintf(buf, sizeof(buf), "не читается E:%02u", g_app.sd_err);
		else
			lv_snprintf(buf, sizeof(buf), "нет карты");
		ui_set_chip(sd_state, UI_C_RED_BG, UI_C_RED);
		break;
	case SD_ERROR:
		lv_snprintf(buf, sizeof(buf), "сбой записи E:%02u", g_app.sd_err);
		ui_set_chip(sd_state, UI_C_REC, UI_C_ON_REC);
		break;
	case SD_RECORDING:
		lv_snprintf(buf, sizeof(buf), "идёт запись");
		ui_set_chip(sd_state, UI_C_RED_BG, UI_C_RED);
		break;
	case SD_READY:
	default:
		lv_snprintf(buf, sizeof(buf), "готова");
		ui_set_chip(sd_state, UI_C_GREEN_BG, UI_C_GREEN);
		break;
	}
	ui_set_text(sd_state, buf);

	// Номер замера и имена файлов
	bool rec = (g_app.sd_state == SD_RECORDING);
	if (g_app.file_number != 0) {
		lv_snprintf(buf, sizeof(buf), "M%03u " UI_MIDDOT " %s", g_app.file_number,
				rec ? "идёт" : "следующий");
		ui_set_text(sd_num, buf);
		ui_set_text_color(sd_num, UI_C_TEXT);
	} else {
		bool card = (g_app.sd_state == SD_READY);
		ui_set_text(sd_num, card ? "номера кончились" : UI_DASH);
		ui_set_text_color(sd_num, card ? UI_C_ORANGE : UI_C_GREY);
	}
	for (int i = 0; i < APP_SENSOR_COUNT; i++) {
		char name[SD_NAME_LEN];
		if (sd_logger_file_name((uint8_t) i, name)) {
			ui_set_text(sd_file[i], name);
			// Во время записи файл датчика без связи ещё не создан — серым
			bool open = !rec || (g_app.rec_sensor_mask & (1U << i)) != 0;
			ui_set_text_color(sd_file[i], open ? UI_C_TEXT : UI_C_GREY);
		} else {
			ui_set_text(sd_file[i], i == 0 ? UI_DASH : "");
			ui_set_text_color(sd_file[i], UI_C_GREY);
		}
	}

	// Запись: время и строки
	if (rec) {
		uint32_t s = lv_tick_diff(lv_tick_get(), g_app.rec_start_ms) / 1000U;
		lv_snprintf(buf, sizeof(buf), "%02lu:%02lu " UI_MIDDOT " %lu строк",
				(unsigned long) (s / 60U), (unsigned long) (s % 60U),
				(unsigned long) g_app.rec_rows);
		ui_set_text(sd_rec, buf);
		ui_set_text_color(sd_rec, UI_C_TEXT);
	} else {
		ui_set_text(sd_rec, g_app.sd_state == SD_ERROR ? "тумблер " UI_ARROW " СТОП" : "нет");
		ui_set_text_color(sd_rec, g_app.sd_state == SD_ERROR ? UI_C_RED : UI_C_DIM);
	}

	// Место
	if (g_app.sd_total_mb == 0) {
		ui_set_text(sd_space, UI_DASH);
		ui_set_text_color(sd_space, UI_C_GREY);
	} else if (g_app.sd_free_mb == APP_SD_FREE_UNKNOWN) {
		fmt_mb(b, sizeof(b), g_app.sd_total_mb);
		lv_snprintf(buf, sizeof(buf), "свободно ? из %s", b);
		ui_set_text(sd_space, buf);
		ui_set_text_color(sd_space, UI_C_DIM);
	} else {
		fmt_mb(a, sizeof(a), g_app.sd_free_mb);
		fmt_mb(b, sizeof(b), g_app.sd_total_mb);
		lv_snprintf(buf, sizeof(buf), "%s свободно из %s", a, b);
		ui_set_text(sd_space, buf);
		ui_set_text_color(sd_space, g_app.sd_free_mb < SD_LOW_MB ? UI_C_ORANGE : UI_C_TEXT);
	}
}
