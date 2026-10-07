/*
 * ui_settings.c — экраны меню «Настройки»:
 *   меню          — Дата и время / Адрес датчика / Сбросить ноль /
 *                   Пауза шины / Порог АКБ / Тема / Назад;
 *   дата и время  — ДД.ММ.ГГ ЧЧ:ММ энкодером, «OK» -> app_set_time(), «Отмена»;
 *   адрес датчика — смена Modbus-адреса BWM427 (app_sensor_set_address()),
 *                   состояние из g_app.svc_state.
 *
 * Без часов DS3231 пункт «Дата и время» горит оранжевым, пояснение — на
 * экране даты и времени (отдельная строка в меню не помещается).
 *
 * «Порог АКБ» — поле, как «Частота» на главном экране: нажатие — правка,
 * вращение — ±0.1 В (app_set_bat_alarm()), повторное нажатие — готово.
 *
 * В заголовке меню справа — заряд АКБ и часы: «АКБ 76%  14:36:23»
 * (напряжение — в строке состояния главного экрана).
 */
#include "ui_internal.h"

// Заголовок меню: часы у правого края, заряд АКБ слева от них
#define TITLE_PAD_R         10
#define TITLE_GAP           12

// Значения по умолчанию для смены адреса: новые датчики приходят с адресом 1,
// второй датчик на шине обычно 2, поэтому новый ставим на 3
#define ADDR_OLD_DEFAULT    1
#define ADDR_NEW_DEFAULT    3
#define ADDR_MIN            1
#define ADDR_MAX            247

// Пункты меню: под заголовком, 7 пунктов по 27 px с зазором 3 px (до y = 237)
#define MENU_Y0             (UI_BAR_H + 3)
#define MENU_STEP           30
#define MENU_ITEM_H         27

// Пауза шины RS485 перед запросом, мс: нажатие на пункт меню перебирает значения
static const uint8_t gap_steps[] = { 3, 5, 8, 10, 15, 20, 30, 50 };

/*====================================================================
 * Меню
 *====================================================================*/
enum {
	MI_TIME = 0, MI_ADDR, MI_ZERO, MI_GAP, MI_BAT, MI_THEME, MI_BACK, MI_COUNT
};

static lv_obj_t *menu_scr;
static lv_group_t *menu_grp;
static lv_obj_t *menu_clock, *menu_bat;
static lv_obj_t *menu_btn[MI_COUNT], *menu_icon[MI_COUNT], *menu_value[MI_COUNT];

static void time_show(void);
static void addr_show(void);

// Кнопка действия: текст шрифтом 20 по центру, короткое нажатие -> cb
static lv_obj_t* action_button_create(lv_obj_t *parent, lv_group_t *g, int32_t x, int32_t y,
		int32_t w, int32_t h, const char *text, lv_event_cb_t cb) {
	lv_obj_t *b = ui_button_create(parent, g, x, y, w, h);
	lv_obj_t *l = lv_label_create(b);
	lv_obj_set_style_text_font(l, UI_FONT_MID, 0);
	lv_label_set_text_static(l, text);
	lv_obj_center(l);
	lv_obj_add_event_cb(b, cb, LV_EVENT_SHORT_CLICKED, NULL);
	return b;
}

static void menu_event_cb(lv_event_t *e) {
	uintptr_t id = (uintptr_t) lv_event_get_user_data(e);
	switch (id) {
	case MI_TIME:
		time_show();
		break;
	case MI_ADDR:
		addr_show();
		break;
	case MI_ZERO:
		app_zero_reset(); // результат виден справа: «задан» -> «не задан»
		ui_settings_update(lv_tick_get());
		break;
	case MI_GAP: {
		// Следующее значение паузы по кругу
		uint8_t next = gap_steps[0];
		for (size_t k = 0; k < sizeof(gap_steps); k++) {
			if (gap_steps[k] > g_app.bus_gap_ms) {
				next = gap_steps[k];
				break;
			}
		}
		app_set_bus_gap(next);
		ui_settings_update(lv_tick_get());
		break;
	}
	case MI_THEME:
		// Сразу перекрасить все экраны (сохраняет тему логика)
		app_set_theme(g_app.theme == APP_THEME_LIGHT ? APP_THEME_DARK : APP_THEME_LIGHT);
		ui_theme_sync();
		ui_settings_update(lv_tick_get());
		break;
	default:
		ui_main_show();
		break;
	}
}

// Порог тревоги АКБ: считаем в десятых вольта, чтобы не копить ошибку float
static void bat_alarm_step(int32_t step) {
	int32_t t = (int32_t) (g_app.bat_alarm_v * 10.0f + 0.5f) + step;
	if (t < (int32_t) (APP_BAT_ALARM_MIN_V * 10.0f + 0.5f))
		t = (int32_t) (APP_BAT_ALARM_MIN_V * 10.0f + 0.5f);
	if (t > (int32_t) (APP_BAT_ALARM_MAX_V * 10.0f + 0.5f))
		t = (int32_t) (APP_BAT_ALARM_MAX_V * 10.0f + 0.5f);
	app_set_bat_alarm((float) t / 10.0f);
	ui_settings_update(lv_tick_get()); // показать сразу
}

// Пункт меню: значок, текст, необязательное значение справа.
// Короткое нажатие -> menu_event_cb, кроме полей (field_cb != NULL)
static void menu_item_create(int idx, const char *icon, const char *text,
		ui_step_cb_t field_cb) {
	lv_obj_t *b = ui_button_create(menu_scr, menu_grp, 6, MENU_Y0 + idx * MENU_STEP,
			UI_W - 12, MENU_ITEM_H);
	menu_btn[idx] = b;
	lv_obj_t *ic = ui_label_create(b, UI_FONT_MID, UI_C_ACCENT);
	lv_label_set_text_static(ic, icon);
	lv_obj_align(ic, LV_ALIGN_LEFT_MID, 10, 0);
	menu_icon[idx] = ic;

	lv_obj_t *l = lv_label_create(b); // цвет наследуется от кнопки
	lv_obj_set_style_text_font(l, UI_FONT_MID, 0);
	lv_label_set_text_static(l, text);
	lv_obj_align(l, LV_ALIGN_LEFT_MID, 40, 0);

	lv_obj_t *v = ui_label_create(b, UI_FONT_SMALL, UI_C_DIM);
	lv_obj_align(v, LV_ALIGN_RIGHT_MID, -10, 0);
	menu_value[idx] = v;

	if (field_cb != NULL)
		ui_field_attach(b, field_cb);
	else
		lv_obj_add_event_cb(b, menu_event_cb, LV_EVENT_SHORT_CLICKED, (void*) (uintptr_t) idx);
}

static void menu_create(void) {
	menu_scr = ui_screen_create();
	menu_grp = lv_group_create();

	lv_obj_t *bar = ui_title_create(menu_scr, LV_SYMBOL_SETTINGS, "Настройки");
	menu_clock = ui_label_create(bar, UI_FONT_SMALL, UI_C_DIM);
	lv_obj_align(menu_clock, LV_ALIGN_RIGHT_MID, -TITLE_PAD_R, 0);
	// Ширина часов постоянная (цифры моноширинные) — заряд АКБ не прыгает
	menu_bat = ui_label_create(bar, UI_FONT_SMALL, UI_C_DIM);
	lv_obj_align(menu_bat, LV_ALIGN_RIGHT_MID,
			-(TITLE_PAD_R + ui_text_width("00:00:00", UI_FONT_SMALL) + TITLE_GAP), 0);

	menu_item_create(MI_TIME, LV_SYMBOL_BELL, "Дата и время", NULL);
	menu_item_create(MI_ADDR, LV_SYMBOL_EDIT, "Адрес датчика", NULL);
	lv_label_set_text_static(menu_value[MI_ADDR], "Modbus");
	menu_item_create(MI_ZERO, LV_SYMBOL_REFRESH, "Сбросить ноль", NULL);
	menu_item_create(MI_GAP, LV_SYMBOL_PAUSE, "Пауза шины", NULL);
	menu_item_create(MI_BAT, LV_SYMBOL_BATTERY_1, "Порог АКБ", bat_alarm_step);
	menu_item_create(MI_THEME, LV_SYMBOL_TINT, "Тема", NULL);
	menu_item_create(MI_BACK, LV_SYMBOL_LEFT, "Назад", NULL);
}

void ui_menu_show(void) {
	ui_show(menu_scr, menu_grp);
	ui_settings_update(lv_tick_get());
}

static void menu_update(void) {
	char buf[32];
	const app_time_t *t = &g_app.time;

	lv_snprintf(buf, sizeof(buf), "%02u:%02u:%02u", t->hours, t->minutes, t->seconds);
	ui_set_text(menu_clock, buf);

	// Примерный заряд АКБ: неяркий, при тревоге красный; от USB — «USB»
	if (!g_app.battery_present || g_app.battery_pct > 100U) {
		ui_set_text(menu_bat, "USB");
		ui_set_text_color(menu_bat, UI_C_DIM);
	} else {
		lv_snprintf(buf, sizeof(buf), "АКБ %u%%", (unsigned) g_app.battery_pct);
		ui_set_text(menu_bat, buf);
		ui_set_text_color(menu_bat, g_app.battery_low ? UI_C_RED : UI_C_DIM);
	}

	// Порог АКБ: при правке значение цвета правки, как текст пункта
	ui_fmt_fixed(buf, sizeof(buf), g_app.bat_alarm_v, 1, " В");
	ui_set_text(menu_value[MI_BAT], buf);
	bool bat_edit = lv_group_get_editing(menu_grp)
			&& lv_group_get_focused(menu_grp) == menu_btn[MI_BAT];
	ui_set_text_color(menu_value[MI_BAT], bat_edit ? UI_C_EDIT_TEXT : UI_C_DIM);

	// Без DS3231 время после выключения пропадёт — вместо даты предупреждение
	if (g_app.rtc_present) {
		lv_snprintf(buf, sizeof(buf), "%02u.%02u.%02u %02u:%02u", t->date, t->month,
				t->year, t->hours, t->minutes);
		ui_set_text(menu_value[MI_TIME], buf);
		ui_set_text_color(menu_value[MI_TIME], UI_C_DIM);
		ui_set_text_color(menu_icon[MI_TIME], UI_C_ACCENT);
	} else {
		ui_set_text(menu_value[MI_TIME], LV_SYMBOL_WARNING " нет часов");
		ui_set_text_color(menu_value[MI_TIME], UI_C_ORANGE);
		ui_set_text_color(menu_icon[MI_TIME], UI_C_ORANGE);
	}

	bool zero_set = false;
	for (int i = 0; i < APP_SENSOR_COUNT; i++) {
		if (g_app.sensor[i].off_x != 0.0f || g_app.sensor[i].off_y != 0.0f)
			zero_set = true;
	}
	ui_set_text(menu_value[MI_ZERO], zero_set ? "задан" : "не задан");
	lv_snprintf(buf, sizeof(buf), "%u мс", g_app.bus_gap_ms);
	ui_set_text(menu_value[MI_GAP], buf);
	ui_set_text(menu_value[MI_THEME], g_app.theme == APP_THEME_LIGHT ? "светлая" : "тёмная");
}

/*====================================================================
 * Дата и время
 *====================================================================*/
enum {
	TF_DATE = 0, TF_MONTH, TF_YEAR, TF_HOURS, TF_MINUTES, TF_COUNT
};

// Поля: 64x48, шаг 78 (между полями — разделитель)
#define TF_X0       92
#define TF_STEP     78
#define TF_W        64
#define TF_H        48
#define TF_Y_DATE   (UI_BAR_H + 9)
#define TF_Y_TIME   (TF_Y_DATE + TF_H + 8)

static lv_obj_t *time_scr;
static lv_group_t *time_grp;
static lv_obj_t *time_val[TF_COUNT];
static lv_obj_t *time_rtc;
static app_time_t time_edit; // копия только на время правки

static uint8_t days_in_month(uint8_t month, uint8_t year) {
	static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (month < 1 || month > 12)
		return 31;
	if (month == 2 && (year % 4U) == 0)
		return 29; // 2000..2099: каждый 4-й год високосный
	return days[month - 1];
}

// Прибавить step к v в кольце min..max
static uint8_t wrap_add(uint8_t v, int32_t step, uint8_t min, uint8_t max) {
	int32_t span = (int32_t) max - min + 1;
	int32_t r = ((int32_t) v - min + step) % span;
	if (r < 0)
		r += span;
	return (uint8_t) (min + r);
}

static void time_refresh(void) {
	char buf[8];
	const uint8_t v[TF_COUNT] = { time_edit.date, time_edit.month, time_edit.year,
			time_edit.hours, time_edit.minutes };
	for (int i = 0; i < TF_COUNT; i++) {
		lv_snprintf(buf, sizeof(buf), "%02u", v[i]);
		ui_set_text(time_val[i], buf);
	}
}

static void time_step(int field, int32_t step) {
	switch (field) {
	case TF_DATE:
		time_edit.date = wrap_add(time_edit.date, step, 1,
				days_in_month(time_edit.month, time_edit.year));
		break;
	case TF_MONTH:
		time_edit.month = wrap_add(time_edit.month, step, 1, 12);
		break;
	case TF_YEAR:
		time_edit.year = wrap_add(time_edit.year, step, 0, 99);
		break;
	case TF_HOURS:
		time_edit.hours = wrap_add(time_edit.hours, step, 0, 23);
		break;
	default:
		time_edit.minutes = wrap_add(time_edit.minutes, step, 0, 59);
		break;
	}
	uint8_t dim = days_in_month(time_edit.month, time_edit.year);
	if (time_edit.date > dim)
		time_edit.date = dim;
	time_refresh();
}

static void tf_date_step(int32_t s) { time_step(TF_DATE, s); }
static void tf_month_step(int32_t s) { time_step(TF_MONTH, s); }
static void tf_year_step(int32_t s) { time_step(TF_YEAR, s); }
static void tf_hours_step(int32_t s) { time_step(TF_HOURS, s); }
static void tf_minutes_step(int32_t s) { time_step(TF_MINUTES, s); }

static void time_ok_cb(lv_event_t *e) {
	(void) e;
	time_edit.seconds = 0;
	app_set_time(&time_edit);
	ui_menu_show(); // новое время видно в пункте «Дата и время»
}

static void time_cancel_cb(lv_event_t *e) {
	(void) e;
	ui_menu_show();
}

// Поле из двух цифр крупным шрифтом; col — номер столбца (0..2)
static void time_field_create(int col, int32_t y, ui_step_cb_t cb, lv_obj_t **val) {
	lv_obj_t *b = ui_button_create(time_scr, time_grp, TF_X0 + col * TF_STEP, y, TF_W, TF_H);
	ui_field_attach(b, cb);
	lv_obj_t *l = lv_label_create(b); // цвет наследуется: обычный / правка
	lv_obj_set_style_text_font(l, UI_FONT_NUM, 0);
	lv_label_set_text_static(l, "00");
	lv_obj_center(l);
	*val = l;
}

// Разделитель между полями col и col + 1
static void time_sep_create(int col, int32_t y, const char *text) {
	lv_obj_t *l = ui_label_create(time_scr, UI_FONT_NUM, UI_C_DIM);
	lv_label_set_text_static(l, text);
	lv_obj_set_width(l, TF_STEP - TF_W);
	lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_pos(l, TF_X0 + col * TF_STEP + TF_W,
			y + (TF_H - lv_font_get_line_height(UI_FONT_NUM)) / 2);
}

static void time_caption_create(int32_t y, const char *text) {
	lv_obj_t *l = ui_label_create(time_scr, UI_FONT_MID, UI_C_DIM);
	lv_label_set_text_static(l, text);
	lv_obj_set_pos(l, 14, y + (TF_H - lv_font_get_line_height(UI_FONT_MID)) / 2);
}

static void time_create(void) {
	time_scr = ui_screen_create();
	time_grp = lv_group_create();
	ui_title_create(time_scr, LV_SYMBOL_BELL, "Дата и время");

	time_caption_create(TF_Y_DATE, "Дата");
	time_caption_create(TF_Y_TIME, "Время");

	time_field_create(0, TF_Y_DATE, tf_date_step, &time_val[TF_DATE]);
	time_sep_create(0, TF_Y_DATE, ".");
	time_field_create(1, TF_Y_DATE, tf_month_step, &time_val[TF_MONTH]);
	time_sep_create(1, TF_Y_DATE, ".");
	time_field_create(2, TF_Y_DATE, tf_year_step, &time_val[TF_YEAR]);

	time_field_create(0, TF_Y_TIME, tf_hours_step, &time_val[TF_HOURS]);
	time_sep_create(0, TF_Y_TIME, ":");
	time_field_create(1, TF_Y_TIME, tf_minutes_step, &time_val[TF_MINUTES]);

	lv_obj_t *hint = ui_label_create(time_scr, UI_FONT_SMALL, UI_C_DIM);
	lv_label_set_text_static(hint, "Нажатие — правка, повторное — готово");
	lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, TF_Y_TIME + TF_H + 6);

	time_rtc = ui_label_create(time_scr, UI_FONT_SMALL, UI_C_ORANGE);
	lv_label_set_text_static(time_rtc, LV_SYMBOL_WARNING " Нет DS3231: время не сохранится");
	lv_obj_align(time_rtc, LV_ALIGN_TOP_MID, 0, TF_Y_TIME + TF_H + 24);

	action_button_create(time_scr, time_grp, 8, 190, 148, 44, LV_SYMBOL_OK "  OK",
			time_ok_cb);
	action_button_create(time_scr, time_grp, UI_W - 8 - 148, 190, 148, 44,
			LV_SYMBOL_CLOSE "  Отмена", time_cancel_cb);
}

static void time_show(void) {
	time_edit = g_app.time;
	// Защита от мусора в часах (например, DS3231 без батарейки)
	if (time_edit.month < 1 || time_edit.month > 12)
		time_edit.month = 1;
	if (time_edit.date < 1
			|| time_edit.date > days_in_month(time_edit.month, time_edit.year))
		time_edit.date = 1;
	if (time_edit.year > 99)
		time_edit.year = 0;
	if (time_edit.hours > 23)
		time_edit.hours = 0;
	if (time_edit.minutes > 59)
		time_edit.minutes = 0;
	time_refresh();
	ui_set_hidden(time_rtc, g_app.rtc_present);
	ui_show(time_scr, time_grp);
}

/*====================================================================
 * Адрес датчика
 *====================================================================*/
#define AF_W        96
#define AF_H        40
#define AF_Y_OLD    (UI_BAR_H + 6)
#define AF_Y_NEW    (AF_Y_OLD + AF_H + 6)
#define AF_Y_WARN   (AF_Y_NEW + AF_H + 6)   // плашка-предупреждение (2 строки)
#define AF_Y_STATUS 166                     // «Выполняется…» / «Готово»
#define AF_Y_BTN    192

static lv_obj_t *addr_scr;
static lv_group_t *addr_grp;
static lv_obj_t *addr_old_val, *addr_new_val, *addr_status;
static uint8_t addr_old = ADDR_OLD_DEFAULT; // значения правки (не настройки прибора)
static uint8_t addr_new = ADDR_NEW_DEFAULT;
static bool addr_requested;  // «Записать» нажата на этом заходе
static bool addr_same;       // попытка записать тот же адрес

static uint8_t clamp_addr(int32_t v) {
	if (v < ADDR_MIN)
		v = ADDR_MIN;
	if (v > ADDR_MAX)
		v = ADDR_MAX;
	return (uint8_t) v;
}

static void addr_refresh(void) {
	char buf[8];
	lv_snprintf(buf, sizeof(buf), "%u", addr_old);
	ui_set_text(addr_old_val, buf);
	lv_snprintf(buf, sizeof(buf), "%u", addr_new);
	ui_set_text(addr_new_val, buf);
}

static void addr_old_step(int32_t s) {
	addr_old = clamp_addr((int32_t) addr_old + s);
	addr_same = false;
	addr_refresh();
}

static void addr_new_step(int32_t s) {
	addr_new = clamp_addr((int32_t) addr_new + s);
	addr_same = false;
	addr_refresh();
}

static void addr_write_cb(lv_event_t *e) {
	(void) e;
	if (addr_requested && g_app.svc_state == SVC_BUSY)
		return; // предыдущая команда ещё выполняется
	if (addr_old == addr_new) {
		addr_same = true;
	} else {
		addr_same = false;
		app_sensor_set_address(addr_old, addr_new);
		addr_requested = true;
	}
	ui_settings_update(lv_tick_get());
}

static void addr_back_cb(lv_event_t *e) {
	(void) e;
	ui_menu_show();
}

static lv_obj_t* addr_field_create(int32_t y, const char *caption, ui_step_cb_t cb) {
	lv_obj_t *cap = ui_label_create(addr_scr, UI_FONT_MID, UI_C_TEXT);
	lv_label_set_text_static(cap, caption);
	lv_obj_set_pos(cap, 14, y + (AF_H - lv_font_get_line_height(UI_FONT_MID)) / 2);

	lv_obj_t *b = ui_button_create(addr_scr, addr_grp, UI_W - 8 - AF_W, y, AF_W, AF_H);
	ui_field_attach(b, cb);
	lv_obj_t *l = lv_label_create(b); // цвет наследуется: обычный / правка
	lv_obj_set_style_text_font(l, UI_FONT_NUM, 0);
	lv_label_set_text_static(l, "");
	lv_obj_center(l);
	return l;
}

static void addr_create(void) {
	addr_scr = ui_screen_create();
	addr_grp = lv_group_create();
	ui_title_create(addr_scr, LV_SYMBOL_EDIT, "Адрес датчика");

	addr_old_val = addr_field_create(AF_Y_OLD, "Старый адрес", addr_old_step);
	addr_new_val = addr_field_create(AF_Y_NEW, "Новый адрес", addr_new_step);

	// Предупреждение — оранжевая плашка во всю ширину
	lv_obj_t *warn = ui_chip_create(addr_scr, UI_FONT_SMALL);
	ui_set_chip(warn, UI_C_ORANGE_BG, UI_C_ORANGE);
	lv_obj_set_style_radius(warn, UI_RADIUS_BTN, 0);
	lv_obj_set_style_pad_ver(warn, 2, 0);
	lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(warn, UI_W - 16);
	lv_label_set_text_static(warn, LV_SYMBOL_WARNING " На шине должен быть\nтолько этот датчик");
	lv_obj_set_pos(warn, 8, AF_Y_WARN);

	addr_status = ui_label_create(addr_scr, UI_FONT_MID, UI_C_DIM);
	lv_obj_align(addr_status, LV_ALIGN_TOP_MID, 0, AF_Y_STATUS);

	action_button_create(addr_scr, addr_grp, 8, AF_Y_BTN, 148, 42,
			LV_SYMBOL_SAVE "  Записать", addr_write_cb);
	action_button_create(addr_scr, addr_grp, UI_W - 8 - 148, AF_Y_BTN, 148, 42,
			LV_SYMBOL_LEFT "  Назад", addr_back_cb);
}

static void addr_show(void) {
	addr_requested = false;
	addr_same = false;
	addr_refresh();
	ui_show(addr_scr, addr_grp);
	ui_settings_update(lv_tick_get());
}

static void addr_update(void) {
	if (addr_same) {
		ui_set_text(addr_status, "Адреса совпадают");
		ui_set_text_color(addr_status, UI_C_ORANGE);
		return;
	}
	if (!addr_requested) {
		ui_set_text(addr_status, "");
		return;
	}
	switch (g_app.svc_state) {
	case SVC_BUSY:
		ui_set_text(addr_status, "Выполняется" UI_ELLIPSIS);
		ui_set_text_color(addr_status, UI_C_ACCENT);
		break;
	case SVC_OK:
		ui_set_text(addr_status, LV_SYMBOL_OK " Готово");
		ui_set_text_color(addr_status, UI_C_GREEN);
		break;
	case SVC_FAIL:
		ui_set_text(addr_status, LV_SYMBOL_CLOSE " Нет ответа");
		ui_set_text_color(addr_status, UI_C_RED);
		break;
	case SVC_IDLE:
	default:
		ui_set_text(addr_status, "");
		break;
	}
}

/*====================================================================
 * Общее
 *====================================================================*/
void ui_settings_create(void) {
	menu_create();
	time_create();
	addr_create();
}

void ui_settings_update(uint32_t now) {
	(void) now;
	lv_obj_t *act = lv_screen_active();
	if (act == menu_scr)
		menu_update();
	else if (act == addr_scr)
		addr_update();
}
