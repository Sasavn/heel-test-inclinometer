/*
 * ui_main.c — главный экран: строка состояния, карточки двух датчиков,
 * качка, органы управления (Частота, Фильтр α, Ноль, Меню).
 *
 *  y   0..21   14:36 [ЗАП] [SD M007]         ЦП 23%  11.8 В
 *  y  26..91   ┌ Д2 ●              X   +0.12° ┐   карточка датчика:
 *              └ [OK]              Y   −1.23° ┘   имя, плашка состояния, X/Y
 *  y  95..160  ┌ Д3 ... ┐
 *  y 164..185  [✓ ГОТОВ] / [КАЧКА 1.23°]           запись 01:05
 *  y 190..235  [Частота][Фильтр][Ноль][Меню]
 *
 * В строке состояния нет места для даты и секунд (они — в меню и на экране
 * даты и времени). Напряжение питания ниже порога (g_app.battery_low) —
 * тревога: плашка напряжения мигает, как «ЗАП», а в строке сообщений
 * красная надпись (важнее всех, кроме только что показанного сообщения).
 */
#include "ui_internal.h"

// --- Раскладка ---
#define BAR_H           22
#define REC_X           50      // плашка записи: правее часов «00:00» (5 + 39 + 6)
// Справа налево: плашка напряжения, загрузка ЦП, плашка карты
#define BAT_PAD         4       // поля плашки напряжения (залита при тревоге)
#define BAT_R           2       // от правого края экрана (текст — в 6 px, как часы слева)
#define BAT_GAP         8       // между загрузкой ЦП и плашкой напряжения
#define CPU_CAP_GAP     4       // между «ЦП» и числом (ширина пробела)
#define SD_GAP          6       // между плашкой карты и загрузкой ЦП
#define CARD_X          4
#define CARD_W          (UI_W - 2 * CARD_X)
#define CARD_H          66
#define CARD_Y0         26
#define CARD_STEP       (CARD_H + 3)
// Внутри карточки (координаты от внутреннего края рамки 1 px)
#define NAME_X          9
#define VAL_RIGHT       302     // правый край чисел
#define VAL_W           146     // "−88.88°" шрифтом ui_font_num
#define VAL_X           (VAL_RIGHT - VAL_W)
#define CAP_X           (VAL_X - 20)  // подпись X / Y слева от чисел
#define ROW_X_Y         4       // строка X
#define ROW_Y_Y         35      // строка Y
#define CHIP_Y          37      // плашка состояния под именем
#define CHIP_Y_2LINE    28      // ... двухстрочная
#define STAB_Y          164
#define STAB_PAD        9       // поля плашки качки по горизонтали
#define INFO_GAP        8       // зазор между плашкой качки и сообщением
#define CTRL_Y          190
#define CTRL_H          46
#define CTRL_W          75
#define CTRL_GAP        4

// --- Загрузка процессора: пороги цвета, % ---
#define CPU_WARN_PCT    50      // от него — оранжевый
#define CPU_HIGH_PCT    80      // от него — красный

// Частота опроса считается «не успевает», если ниже уставки на 10 %
#define RATE_LOW_RATIO  0.9f

// Что сейчас показывает плашка состояния датчика
typedef enum {
	CHIP_NONE = 0, CHIP_OK, CHIP_LOST, CHIP_ABSENT, CHIP_GARBLED
} chip_state_t;

typedef struct {
	lv_obj_t *card;
	lv_obj_t *name;     // Д2, Д3
	lv_obj_t *dot;      // ● — по этому датчику идёт запись
	lv_obj_t *chip;     // OK / нет связи / не подключен / битые ответы
	lv_obj_t *val_x;
	lv_obj_t *val_y;
	chip_state_t shown; // что уже отрисовано
} sensor_card_t;

static lv_obj_t *scr;
static lv_group_t *grp;

static lv_obj_t *lbl_time, *pill_rec, *chip_sd, *lbl_cpu_cap, *lbl_cpu, *chip_bat;
static int32_t cpu_r;   // от правого края экрана до правого края числа загрузки ЦП
static sensor_card_t cards[APP_SENSOR_COUNT];
static lv_obj_t *pill_stab, *lbl_info;
static lv_obj_t *btn_freq, *btn_alpha, *btn_zero, *btn_menu;
static lv_obj_t *val_freq, *val_alpha, *val_zero;

static const char *toast_text;
static ui_col_t toast_color;
static uint32_t toast_until;

/*--------------------------------------------------------------------
 * Действия
 *--------------------------------------------------------------------*/
static void freq_step(int32_t step) {
	int32_t hz = (int32_t) g_app.log_freq_hz + step;
	if (hz < APP_FREQ_MIN_HZ)
		hz = APP_FREQ_MIN_HZ;
	if (hz > APP_FREQ_MAX_HZ)
		hz = APP_FREQ_MAX_HZ;
	if (hz != g_app.log_freq_hz)
		app_set_log_freq((uint16_t) hz);
	ui_main_update(lv_tick_get()); // показать сразу, не ждать 100 мс
}

static void alpha_step(int32_t step) {
	// Считаем в сотых, чтобы не копить ошибку float
	int32_t a = (int32_t) (g_app.ema_alpha * 100.0f + 0.5f) + step;
	if (a < (int32_t) (APP_ALPHA_MIN * 100.0f + 0.5f))
		a = (int32_t) (APP_ALPHA_MIN * 100.0f + 0.5f);
	if (a > (int32_t) (APP_ALPHA_MAX * 100.0f + 0.5f))
		a = (int32_t) (APP_ALPHA_MAX * 100.0f + 0.5f);
	app_set_ema_alpha((float) a / 100.0f);
	ui_main_update(lv_tick_get());
}

static void zero_event_cb(lv_event_t *e) {
	if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
		app_zero_all();
		ui_main_toast("Ноль задан", UI_C_GREEN);
	} else if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
		app_zero_reset();
		ui_main_toast("Ноль сброшен", UI_C_ORANGE);
	}
}

static void menu_event_cb(lv_event_t *e) {
	(void) e;
	ui_menu_show();
}

void ui_main_toast(const char *text, ui_col_t color) {
	toast_text = text;
	toast_color = color;
	toast_until = lv_tick_get() + UI_TOAST_MS;
	if (lv_screen_active() == scr)
		ui_main_update(lv_tick_get());
}

/*--------------------------------------------------------------------
 * Построение
 *--------------------------------------------------------------------*/
// Кнопка управления: подпись сверху, значение снизу
static lv_obj_t* ctrl_create(int idx, const char *caption, lv_obj_t **value) {
	lv_obj_t *b = ui_button_create(scr, grp, CTRL_GAP + idx * (CTRL_W + CTRL_GAP),
			CTRL_Y, CTRL_W, CTRL_H);
	lv_obj_t *cap = ui_label_create(b, UI_FONT_SMALL, UI_C_DIM);
	lv_label_set_text_static(cap, caption);
	lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 3);

	lv_obj_t *v = lv_label_create(b); // цвет наследуется от кнопки (обычный / правка)
	lv_obj_set_style_text_font(v, UI_FONT_MID, 0);
	lv_label_set_text_static(v, "");
	lv_obj_align(v, LV_ALIGN_BOTTOM_MID, 0, -3);
	*value = v;
	return b;
}

// Число угла: правый край на VAL_RIGHT, ширина под самое длинное значение
static lv_obj_t* value_label_create(lv_obj_t *parent, int32_t y) {
	lv_obj_t *l = ui_label_create(parent, UI_FONT_NUM, UI_C_TEXT);
	lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
	lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_size(l, VAL_W, lv_font_get_line_height(UI_FONT_NUM));
	lv_obj_set_pos(l, VAL_X, y);
	return l;
}

// Подпись строки (X / Y) — по высоте посередине цифр
static void caption_create(lv_obj_t *parent, const char *text, int32_t row_y) {
	lv_obj_t *l = ui_label_create(parent, UI_FONT_MID, UI_C_DIM);
	lv_label_set_text_static(l, text);
	lv_obj_set_pos(l, CAP_X, row_y + (lv_font_get_line_height(UI_FONT_NUM)
			- lv_font_get_line_height(UI_FONT_MID)) / 2);
}

static void card_create(int i) {
	sensor_card_t *c = &cards[i];
	c->card = ui_card_create(scr, CARD_X, CARD_Y0 + i * CARD_STEP, CARD_W, CARD_H);

	c->name = ui_label_create(c->card, UI_FONT_MID, UI_C_TEXT);
	lv_label_set_text_fmt(c->name, "Д%d", APP_SENSOR_ADDR(i));
	lv_obj_set_pos(c->name, NAME_X, 4);

	// Точка записи — справа от имени («Д2» и «Д3» одной ширины: цифры моноширинные)
	c->dot = ui_box_create(c->card, NAME_X + 37, 12, 10, 10, UI_C_RED);
	lv_obj_set_style_radius(c->dot, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_hidden(c->dot, true);

	// Ширина плашки — не дальше подписей X/Y («не подключен» — самая длинная строка)
	c->chip = ui_chip_create(c->card, UI_FONT_SMALL);
	lv_obj_set_style_pad_hor(c->chip, 6, 0);
	lv_obj_set_style_text_line_space(c->chip, -3, 0); // для двухстрочной плашки
	lv_obj_set_pos(c->chip, NAME_X - 2, CHIP_Y);

	caption_create(c->card, "X", ROW_X_Y);
	caption_create(c->card, "Y", ROW_Y_Y);
	c->val_x = value_label_create(c->card, ROW_X_Y);
	c->val_y = value_label_create(c->card, ROW_Y_Y);

	c->shown = CHIP_NONE;
}

void ui_main_create(void) {
	scr = ui_screen_create();
	grp = lv_group_create();

	// --- Строка состояния ---
	lv_obj_t *bar = ui_bar_create(scr, BAR_H);

	lbl_time = ui_label_create(bar, UI_FONT_SMALL, UI_C_TEXT);
	lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 5, 0);

	pill_rec = ui_chip_create(bar, UI_FONT_SMALL);
	lv_obj_align(pill_rec, LV_ALIGN_LEFT_MID, REC_X, 0);

	// Справа элементы прижаты к правому краю, и каждому оставлено место под
	// самое длинное значение («28.8 В», «ЦП 100%»): соседи не сдвигаются.
	// Напряжение — плашка без подложки (цвет полосы), при тревоге залита
	chip_bat = ui_chip_create(bar, UI_FONT_SMALL);
	lv_obj_set_style_pad_hor(chip_bat, BAT_PAD, 0);
	lv_obj_align(chip_bat, LV_ALIGN_RIGHT_MID, -BAT_R, 0);
	int32_t bat_w = ui_text_width("28.8 В", UI_FONT_SMALL) + 2 * BAT_PAD;

	// Загрузка процессора: число справа, «ЦП» — слева от числа (update_cpu)
	cpu_r = BAT_R + bat_w + BAT_GAP;
	lbl_cpu = ui_label_create(bar, UI_FONT_SMALL, UI_C_TEXT);
	lv_obj_align(lbl_cpu, LV_ALIGN_RIGHT_MID, -cpu_r, 0);
	lbl_cpu_cap = ui_label_create(bar, UI_FONT_SMALL, UI_C_DIM);
	lv_label_set_text_static(lbl_cpu_cap, "ЦП");
	int32_t cpu_w = ui_text_width("ЦП", UI_FONT_SMALL) + CPU_CAP_GAP
			+ ui_text_width("100%", UI_FONT_SMALL);

	chip_sd = ui_chip_create(bar, UI_FONT_SMALL);
	lv_obj_set_style_pad_hor(chip_sd, 6, 0);
	lv_obj_align(chip_sd, LV_ALIGN_RIGHT_MID, -(cpu_r + cpu_w + SD_GAP), 0);

	// --- Карточки датчиков ---
	for (int i = 0; i < APP_SENSOR_COUNT; i++)
		card_create(i);

	// --- Качка и сообщения ---
	pill_stab = ui_chip_create(scr, UI_FONT_MID);
	lv_obj_set_style_pad_hor(pill_stab, STAB_PAD, 0);
	lv_obj_set_style_pad_ver(pill_stab, 0, 0);
	lv_obj_set_style_radius(pill_stab, 11, 0);
	lv_obj_set_pos(pill_stab, CARD_X, STAB_Y);
	// Сообщение справа: ширина — сколько осталось от плашки качки (update_stab),
	// не влезающий текст обрезается многоточием, а не наезжает на плашку
	lbl_info = ui_label_create(scr, UI_FONT_SMALL, UI_C_DIM);
	lv_label_set_long_mode(lbl_info, LV_LABEL_LONG_MODE_DOTS);
	lv_obj_set_style_text_align(lbl_info, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_size(lbl_info, UI_W / 2, lv_font_get_line_height(UI_FONT_SMALL));
	lv_obj_align(lbl_info, LV_ALIGN_TOP_RIGHT, -(CARD_X + 2),
			STAB_Y + (lv_font_get_line_height(UI_FONT_MID)
					- lv_font_get_line_height(UI_FONT_SMALL)) / 2);

	// --- Органы управления (группа энкодера) ---
	btn_freq = ctrl_create(0, "Частота", &val_freq);
	ui_field_attach(btn_freq, freq_step);

	btn_alpha = ctrl_create(1, "Фильтр", &val_alpha);
	ui_field_attach(btn_alpha, alpha_step);

	btn_zero = ctrl_create(2, "Ноль", &val_zero);
	lv_obj_add_event_cb(btn_zero, zero_event_cb, LV_EVENT_SHORT_CLICKED, NULL);
	lv_obj_add_event_cb(btn_zero, zero_event_cb, LV_EVENT_LONG_PRESSED, NULL);

	lv_obj_t *val_menu;
	btn_menu = ctrl_create(3, "Меню", &val_menu);
	lv_label_set_text_static(val_menu, LV_SYMBOL_SETTINGS);
	lv_obj_add_event_cb(btn_menu, menu_event_cb, LV_EVENT_SHORT_CLICKED, NULL);
}

void ui_main_show(void) {
	ui_show(scr, grp);
	ui_main_update(lv_tick_get());
}

/*--------------------------------------------------------------------
 * Обновление из g_app (~10 Гц)
 *--------------------------------------------------------------------*/
// Загрузка процессора за последнюю секунду (логика обновляет раз в секунду)
static void update_cpu(void) {
	char buf[8];
	unsigned pct = g_app.diag.cpu_load_pct;
	if (pct > 100U)
		pct = 100U;

	lv_snprintf(buf, sizeof(buf), "%u%%", pct);
	if (ui_set_text(lbl_cpu, buf)) {
		// Ширина числа меняется только с числом цифр (цифры моноширинные)
		lv_obj_align(lbl_cpu_cap, LV_ALIGN_RIGHT_MID,
				-(cpu_r + ui_text_width(buf, UI_FONT_SMALL) + CPU_CAP_GAP), 0);
	}
	ui_set_text_color(lbl_cpu, pct >= CPU_HIGH_PCT ? UI_C_RED :
			pct >= CPU_WARN_PCT ? UI_C_ORANGE : UI_C_TEXT);
}

// Тревога АКБ: логика поднимает её только при подключённой АКБ, но от USB
// (напряжение — не АКБ) не тревожим в любом случае
static bool bat_alarm(void) {
	return g_app.battery_present && g_app.battery_low;
}

// Напряжение питания. Тревога — плашка мигает заливкой, как «ЗАП»;
// питание от USB — «USB» серым
static void update_battery(bool blink_on) {
	char buf[16];

	if (!g_app.battery_present) {
		ui_set_text(chip_bat, "USB");
		ui_set_chip(chip_bat, UI_C_BAR, UI_C_DIM);
		return;
	}
	ui_fmt_fixed(buf, sizeof(buf), g_app.battery_v, 1, " В");
	ui_set_text(chip_bat, buf);
	if (!bat_alarm())
		ui_set_chip(chip_bat, UI_C_BAR, UI_C_TEXT); // подложка не видна
	else if (blink_on)
		ui_set_chip(chip_bat, UI_C_REC, UI_C_ON_REC);
	else
		ui_set_chip(chip_bat, UI_C_RED_BG, UI_C_RED);
}

static void update_status_bar(uint32_t now) {
	char buf[32];
	const app_time_t *t = &g_app.time;

	lv_snprintf(buf, sizeof(buf), "%02u:%02u", t->hours, t->minutes);
	ui_set_text(lbl_time, buf);
	// Без DS3231 время после включения неверное — подсвечиваем
	ui_set_text_color(lbl_time, g_app.rtc_present ? UI_C_TEXT : UI_C_ORANGE);

	// Плашка записи (мигает заливкой) и плашка карты с номером замера
	bool blink_on = ((now / UI_BLINK_MS) & 1U) == 0;
	switch (g_app.sd_state) {
	case SD_NO_CARD:
		ui_set_hidden(pill_rec, true);
		ui_set_text(chip_sd, LV_SYMBOL_SD_CARD " НЕТ SD");
		ui_set_chip(chip_sd, UI_C_RED_BG, UI_C_RED);
		ui_set_hidden(chip_sd, false);
		break;
	case SD_READY:
	case SD_RECORDING:
		if (g_app.sd_state == SD_RECORDING) {
			ui_set_text(pill_rec, "ЗАП");
			if (blink_on)
				ui_set_chip(pill_rec, UI_C_REC, UI_C_ON_REC);
			else
				ui_set_chip(pill_rec, UI_C_RED_BG, UI_C_RED);
		} else {
			ui_set_text(pill_rec, "СТОП");
			ui_set_chip(pill_rec, UI_C_CHIP_BG, UI_C_DIM);
		}
		ui_set_hidden(pill_rec, false);
		// Номер замера как в имени файла (2026-10-06_M007_D2.CSV); 0 — номера кончились
		if (g_app.file_number != 0) {
			lv_snprintf(buf, sizeof(buf), LV_SYMBOL_SD_CARD " M%03u", g_app.file_number);
			ui_set_text(chip_sd, buf);
			ui_set_chip(chip_sd, UI_C_CHIP_BG, UI_C_TEXT);
		} else {
			ui_set_text(chip_sd, LV_SYMBOL_SD_CARD " M" UI_DASH);
			ui_set_chip(chip_sd, UI_C_ORANGE_BG, UI_C_ORANGE);
		}
		ui_set_hidden(chip_sd, false);
		break;
	case SD_ERROR:
	default:
		// Длинная плашка занимает и место карты; код ошибки — в строке сообщений
		ui_set_text(pill_rec, "ОШИБКА SD");
		ui_set_chip(pill_rec, UI_C_REC, UI_C_ON_REC);
		ui_set_hidden(pill_rec, false);
		ui_set_hidden(chip_sd, true);
		break;
	}

	update_cpu();
	update_battery(blink_on);
}

static void update_sensor_card(int i, uint32_t now) {
	sensor_card_t *c = &cards[i];
	const app_sensor_t *s = &g_app.sensor[i];
	char buf[24];

	// Битые ответы при отсутствии связи обычно значат, что на шине два датчика
	// с одним адресом (их ответы накладываются)
	chip_state_t st;
	if (s->status == SENSOR_OK)
		st = CHIP_OK;
	else if (s->garbled_count > 0 && lv_tick_diff(now, s->last_garbled_ms) < UI_GARBLED_HINT_MS)
		st = CHIP_GARBLED;
	else if (s->status == SENSOR_LOST)
		st = CHIP_LOST;
	else
		st = CHIP_ABSENT;

	if (c->shown != st) {
		c->shown = st;
		ui_col_t val = UI_C_GREY;
		switch (st) {
		case CHIP_OK:
			ui_set_text(c->chip, LV_SYMBOL_OK " OK");
			ui_set_chip(c->chip, UI_C_GREEN_BG, UI_C_GREEN);
			ui_set_border_color(c->card, UI_C_BORDER);
			val = UI_C_TEXT;
			break;
		case CHIP_GARBLED:
			ui_set_text(c->chip, "битые ответы:\nдубль адреса?");
			ui_set_chip(c->chip, UI_C_RED_BG, UI_C_RED);
			ui_set_border_color(c->card, UI_C_RED);
			break;
		case CHIP_LOST:
			// Последние значения серым
			ui_set_text(c->chip, "нет связи");
			ui_set_chip(c->chip, UI_C_ORANGE_BG, UI_C_ORANGE);
			ui_set_border_color(c->card, UI_C_ORANGE);
			break;
		case CHIP_ABSENT:
		default:
			ui_set_text(c->chip, "не подключен");
			ui_set_chip(c->chip, UI_C_CHIP_BG, UI_C_DIM);
			ui_set_border_color(c->card, UI_C_BORDER);
			break;
		}
		// Двухстрочная плашка поднимается ближе к имени
		ui_set_y(c->chip, st == CHIP_GARBLED ? CHIP_Y_2LINE : CHIP_Y);
		ui_set_text_color(c->name, s->status == SENSOR_ABSENT ? UI_C_GREY : UI_C_TEXT);
		ui_set_text_color(c->val_x, val);
		ui_set_text_color(c->val_y, val);
	}

	if (s->status == SENSOR_ABSENT) {
		ui_set_text(c->val_x, UI_DASH);
		ui_set_text(c->val_y, UI_DASH);
	} else {
		ui_fmt_angle(buf, sizeof(buf), s->x);
		ui_set_text(c->val_x, buf);
		ui_fmt_angle(buf, sizeof(buf), s->y);
		ui_set_text(c->val_y, buf);
	}
	ui_set_hidden(c->dot, (g_app.rec_sensor_mask & (1U << i)) == 0);
}

// Сообщению справа — всё место правее плашки качки
static void fit_info_width(void) {
	int32_t tw = ui_text_width(lv_label_get_text(pill_stab), UI_FONT_MID);
	int32_t w = UI_W - (CARD_X + 2) - (CARD_X + tw + 2 * STAB_PAD) - INFO_GAP;
	if (w < 0)
		w = 0;
	if (lv_obj_get_style_width(lbl_info, LV_PART_MAIN) != w)
		lv_obj_set_width(lbl_info, w);
}

static void update_stab(void) {
	char buf[40];
	bool changed;

	// Готовность к отсчёту по качке опорного датчика (первого отвечающего);
	// если опорный не первый (Д2), его номер — в скобках
	uint8_t ref = g_app.stab_sensor;
	char who[12] = "";
	if (ref != APP_STAB_NONE && ref != 0)
		lv_snprintf(who, sizeof(who), " (Д%u)", (unsigned) APP_SENSOR_ADDR(ref));
	if (ref == APP_STAB_NONE) {
		changed = ui_set_text(pill_stab, "КАЧКА " UI_DASH);
		ui_set_chip(pill_stab, UI_C_CHIP_BG, UI_C_GREY);
	} else if (g_app.is_stable) {
		lv_snprintf(buf, sizeof(buf), LV_SYMBOL_OK " ГОТОВ%s", who);
		changed = ui_set_text(pill_stab, buf);
		ui_set_chip(pill_stab, UI_C_GREEN_BG, UI_C_GREEN);
	} else {
		char span[16];
		ui_fmt_fixed(span, sizeof(span), g_app.stab_span, 2, UI_DEG);
		lv_snprintf(buf, sizeof(buf), "КАЧКА %s%s", span, who);
		changed = ui_set_text(pill_stab, buf);
		ui_set_chip(pill_stab, UI_C_RED_BG, UI_C_RED);
	}
	if (changed)
		fit_info_width();
}

static void update_info(uint32_t now) {
	char buf[48];

	// Справа (по важности): сообщение, тревога АКБ, нехватка частоты опроса,
	// ошибка SD, время записи, подсказка для кнопки «Ноль»
	if (toast_text != NULL && (int32_t) (toast_until - now) > 0) {
		ui_set_text(lbl_info, toast_text);
		ui_set_text_color(lbl_info, toast_color);
		return;
	}
	toast_text = NULL;

	if (bat_alarm()) {
		// «⚠ АКБ 9.6 В < 10.0 В»: коротко, чтобы влезло и рядом с «КАЧКА 1.23°»
		char v[12], lim[12];
		ui_fmt_fixed(v, sizeof(v), g_app.battery_v, 1, " В");
		ui_fmt_fixed(lim, sizeof(lim), g_app.bat_alarm_v, 1, " В");
		lv_snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " АКБ %s < %s", v, lim);
		ui_set_text(lbl_info, buf);
		ui_set_text_color(lbl_info, UI_C_RED);
		return;
	}

#if UI_DIAG
	// Диагностика: проходов суперцикла в секунду / самый долгий проход
	// (из него — отрисовка LVGL), мс
	{
		const app_diag_t *d = &g_app.diag;
		lv_snprintf(buf, sizeof(buf), "%u/с  %u мс (ui %u)", (unsigned) d->loops_per_s,
				(unsigned) d->loop_max_ms, (unsigned) d->ui_max_ms);
		ui_set_text(lbl_info, buf);
		ui_set_text_color(lbl_info,
				d->loop_max_ms > 100 ? UI_C_RED :
				d->loop_max_ms > 30 ? UI_C_ORANGE : UI_C_DIM);
		return;
	}
#endif

	if (g_app.actual_rate_hz > 0.0f
			&& g_app.actual_rate_hz < (float) g_app.log_freq_hz * RATE_LOW_RATIO) {
		char hz[16];
		ui_fmt_fixed(hz, sizeof(hz), g_app.actual_rate_hz, 1, " Гц");
		lv_snprintf(buf, sizeof(buf), "опрос %s", hz);
		ui_set_text(lbl_info, buf);
		ui_set_text_color(lbl_info, UI_C_ORANGE);
	} else if (g_app.sd_state == SD_ERROR) {
		// Запись прервана: продолжится после возврата тумблера в STOP
		lv_snprintf(buf, sizeof(buf), "E:%02u тумблер " UI_ARROW " СТОП", g_app.sd_err);
		ui_set_text(lbl_info, buf);
		ui_set_text_color(lbl_info, UI_C_RED);
	} else if (g_app.sd_state == SD_RECORDING) {
		uint32_t s = lv_tick_diff(now, g_app.rec_start_ms) / 1000U;
		if (s >= 3600U)
			lv_snprintf(buf, sizeof(buf), "запись %lu:%02lu:%02lu", (unsigned long) (s / 3600U),
					(unsigned long) (s / 60U % 60U), (unsigned long) (s % 60U));
		else
			lv_snprintf(buf, sizeof(buf), "запись %02lu:%02lu", (unsigned long) (s / 60U),
					(unsigned long) (s % 60U));
		ui_set_text(lbl_info, buf);
		ui_set_text_color(lbl_info, UI_C_TEXT);
	} else if (lv_group_get_focused(grp) == btn_zero) {
		ui_set_text(lbl_info, "держать: сброс");
		ui_set_text_color(lbl_info, UI_C_DIM);
	} else if (g_app.sd_state == SD_NO_CARD && g_app.sd_err != 0) {
		lv_snprintf(buf, sizeof(buf), "SD E:%02u", g_app.sd_err);
		ui_set_text(lbl_info, buf);
		ui_set_text_color(lbl_info, UI_C_DIM);
	} else {
		ui_set_text(lbl_info, "");
	}
}

static void update_controls(void) {
	char buf[24], num[12];

	lv_snprintf(buf, sizeof(buf), "%u Гц", g_app.log_freq_hz);
	ui_set_text(val_freq, buf);

	ui_fmt_fixed(num, sizeof(num), g_app.ema_alpha, 2, "");
	lv_snprintf(buf, sizeof(buf), UI_ALPHA " %s", num); // "α 0.15"
	ui_set_text(val_alpha, buf);

	bool zero_set = false;
	for (int i = 0; i < APP_SENSOR_COUNT; i++) {
		if (g_app.sensor[i].off_x != 0.0f || g_app.sensor[i].off_y != 0.0f)
			zero_set = true;
	}
	ui_set_text(val_zero, zero_set ? "задан" : "нет");
}

void ui_main_update(uint32_t now) {
	if (lv_screen_active() != scr)
		return;
	update_status_bar(now);
	for (int i = 0; i < APP_SENSOR_COUNT; i++)
		update_sensor_card(i, now);
	update_stab();
	update_info(now);
	update_controls();
}
