/*
 * ui_main.c — главный экран: строка состояния, карточки двух датчиков,
 * качка, органы управления (кораблик, Частота, Ноль, Меню).
 *
 *  y   0..21   06.10.26 14:36 [ЗАП]          ЦП 23%  11.8 В
 *  y  26..91   ┌ Д2 ●                         ✓ OK ┐  карточка датчика: шапка
 *              │ покой              X    +0.12°    │  (имя, запись, связь),
 *              └ качка 1.23°        Y    −1.23°    ┘  по осям — слева качка,
 *                                                     справа угол
 *  y  95..160  ┌ Д3 ... ┐
 *  y 164..185  [✓ ГОТОВ] / [КАЧКА 1.23° Д3 X]      запись 01:05
 *  y 190..235  [кораблик][Частота][Ноль][Меню]
 *
 * Качка — по каждой оси каждого датчика (логика: sensor[i].roll_x/roll_y —
 * размах за окно g_app.roll_window_s, calm_x/calm_y — покой с гистерезисом):
 * «покой» (зелёный) / «качка 1.23°» (оранжевый) / «сбор 12 с» (серый, окно
 * ещё не набрано). Плашка внизу — сводка: ГОТОВ, только если в покое все оси
 * всех отвечающих датчиков, иначе худшая ось или сбор окна.
 *
 * Плашка записи: ЗАП (мигает) / СТОП / СБОЙ (ошибка SD, код — в строке
 * сообщений) / НЕТ SD; подробности о карте — в меню «Карта памяти».
 * Напряжение питания ниже порога (g_app.battery_low) — тревога: плашка
 * напряжения мигает, как «ЗАП», а в строке сообщений красная надпись (важнее
 * всех, кроме только что показанного сообщения).
 *
 * Левая ячейка органов управления — место под будущую функцию; пока в ней
 * кораблик (рисуется примитивами LVGL в цветах темы), в фокус она не попадает.
 */
#include "ui_internal.h"

// --- Раскладка ---
#define BAR_H           22
// Слева направо: дата, время, плашка записи
#define BAR_PAD_L       5
#define BAR_GAP_L       6       // между датой, временем и плашкой записи
// Справа налево: плашка напряжения, загрузка ЦП
#define BAT_PAD         4       // поля плашки напряжения (залита при тревоге)
#define BAT_R           2       // от правого края экрана (текст — в 6 px, как дата слева)
#define BAT_GAP         8       // между загрузкой ЦП и плашкой напряжения
#define CPU_CAP_GAP     4       // между «ЦП» и числом (ширина пробела)
#define CARD_X          4
#define CARD_W          (UI_W - 2 * CARD_X)
#define CARD_H          66
#define CARD_Y0         26
#define CARD_STEP       (CARD_H + 3)
// Внутри карточки (координаты от внутреннего края рамки 1 px, высота 64):
// шапка шрифтом 14 (строка 17 px, заглавные — с 3-го пикселя, поэтому метка
// на 1 px выше края) и две строки цифр ui_font_num (22 px, шаг 24)
#define HDR_X           8       // имя датчика
#define HDR_Y           (-1)
#define HDR_R           8       // состояние связи — у правого края
#define ROLL_X          8       // «покой» / «качка 1.23°» — у левого края
#define VAL_R           8       // поле угла — у правого края (ширина под "−88.88°", val_w)
#define CAP_GAP         8       // от подписи X / Y до поля угла
#define ROW_X_Y         16      // строка X
#define ROW_Y_Y         40      // строка Y
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

// Качка по оси: выше этого размаха — красным, а не оранжевым
#define ROLL_RED_DEG    3.0f

// Что сейчас показывает надпись состояния связи датчика
typedef enum {
	CHIP_NONE = 0, CHIP_OK, CHIP_LOST, CHIP_ABSENT, CHIP_GARBLED
} chip_state_t;

typedef struct {
	lv_obj_t *card;
	lv_obj_t *name;     // Д2, Д3
	lv_obj_t *dot;      // ● — по этому датчику идёт запись
	lv_obj_t *link;     // ✓ OK / нет связи / не подключен / битые ответы
	lv_obj_t *val_x;
	lv_obj_t *val_y;
	lv_obj_t *roll_x;   // покой / качка 1.23° / сбор 12 с
	lv_obj_t *roll_y;
	chip_state_t shown; // что уже отрисовано
} sensor_card_t;

static lv_obj_t *scr;
static lv_group_t *grp;

static lv_obj_t *lbl_date, *lbl_time, *pill_rec, *lbl_cpu_cap, *lbl_cpu, *chip_bat;
static int32_t cpu_r;   // от правого края экрана до правого края числа загрузки ЦП
static int32_t val_w;   // ширина поля угла: "−88.88°" шрифтом ui_font_num
static sensor_card_t cards[APP_SENSOR_COUNT];
static lv_obj_t *pill_stab, *lbl_info;
static lv_obj_t *btn_freq, *btn_zero, *btn_menu;
static lv_obj_t *val_freq, *val_zero;

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

/*--------------------------------------------------------------------
 * Кораблик: корпус с лицом, мачта, два паруса, флаг, волны — примитивы
 * LVGL (треугольники, прямоугольники, дуги, линия) в цветах темы, без
 * картинок. Рисуется только при перерисовке ячейки (она не меняется).
 * Координаты — в рамке SHIP_W x SHIP_H по центру ячейки.
 *--------------------------------------------------------------------*/
#define SHIP_W          44
#define SHIP_H          34

static void ship_tri(lv_layer_t *layer, int32_t x0, int32_t y0, ui_col_t c,
		int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy) {
	lv_draw_triangle_dsc_t d;
	lv_draw_triangle_dsc_init(&d);
	d.color = ui_color(c);
	d.p[0].x = x0 + ax;
	d.p[0].y = y0 + ay;
	d.p[1].x = x0 + bx;
	d.p[1].y = y0 + by;
	d.p[2].x = x0 + cx;
	d.p[2].y = y0 + cy;
	lv_draw_triangle(layer, &d);
}

static void ship_rect(lv_layer_t *layer, int32_t x0, int32_t y0, ui_col_t c, int32_t radius,
		int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
	lv_draw_rect_dsc_t d;
	lv_draw_rect_dsc_init(&d);
	d.bg_color = ui_color(c);
	d.radius = radius;
	lv_area_t a = { x0 + x1, y0 + y1, x0 + x2, y0 + y2 };
	lv_draw_rect(layer, &d, &a);
}

// Дуга толщиной 2 px: углы в градусах, 0 — вправо, 90 — вниз
static void ship_arc(lv_layer_t *layer, int32_t x0, int32_t y0, ui_col_t c, int32_t cx,
		int32_t cy, int32_t r, int32_t start, int32_t end) {
	lv_draw_arc_dsc_t d;
	lv_draw_arc_dsc_init(&d);
	d.color = ui_color(c);
	d.width = 2;
	d.rounded = 1;
	d.center.x = x0 + cx;
	d.center.y = y0 + cy;
	d.radius = (uint16_t) r;
	d.start_angle = start;
	d.end_angle = end;
	lv_draw_arc(layer, &d);
}

static void ship_draw_cb(lv_event_t *e) {
	lv_obj_t *obj = lv_event_get_current_target(e);
	lv_layer_t *layer = lv_event_get_layer(e);
	lv_area_t a;
	lv_obj_get_coords(obj, &a);
	int32_t x0 = a.x1 + (lv_area_get_width(&a) - SHIP_W) / 2;
	int32_t y0 = a.y1 + (lv_area_get_height(&a) - SHIP_H) / 2;

	// Волны под корпусом
	for (int32_t cx = 6; cx < SHIP_W; cx += 11)
		ship_arc(layer, x0, y0, UI_C_ACCENT, cx, 35, 5, 205, 335);

	// Мачта и флаг (развевается влево)
	lv_draw_line_dsc_t l;
	lv_draw_line_dsc_init(&l);
	l.color = ui_color(UI_C_DIM);
	l.width = 2;
	l.p1.x = x0 + 22;
	l.p1.y = y0 + 1;
	l.p2.x = x0 + 22;
	l.p2.y = y0 + 17;
	lv_draw_line(layer, &l);
	ship_tri(layer, x0, y0, UI_C_ACCENT, 21, 0, 21, 7, 13, 3);

	// Паруса: большой справа от мачты, маленький слева
	ship_tri(layer, x0, y0, UI_C_EDIT, 24, 3, 24, 15, 37, 15);
	ship_tri(layer, x0, y0, UI_C_EDIT, 20, 7, 20, 15, 11, 15);

	// Корпус: трапеция = прямоугольник + два треугольника (с нахлёстом 1 px)
	ship_rect(layer, x0, y0, UI_C_RED, 2, 8, 17, 36, 28);
	ship_tri(layer, x0, y0, UI_C_RED, 1, 17, 9, 17, 9, 28);
	ship_tri(layer, x0, y0, UI_C_RED, 43, 17, 35, 17, 35, 28);

	// Лицо: два иллюминатора-глаза и улыбка цветом карточки
	ship_rect(layer, x0, y0, UI_C_CARD, LV_RADIUS_CIRCLE, 12, 19, 15, 22);
	ship_rect(layer, x0, y0, UI_C_CARD, LV_RADIUS_CIRCLE, 29, 19, 32, 22);
	ship_arc(layer, x0, y0, UI_C_CARD, 22, 21, 5, 25, 155);
}

// Число угла: у правого края карточки, ширина под самое длинное значение,
// выравнивание по правому краю (знаки и точки стоят друг под другом)
static lv_obj_t* value_label_create(lv_obj_t *parent, int32_t y) {
	lv_obj_t *l = ui_label_create(parent, UI_FONT_NUM, UI_C_TEXT);
	lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
	lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_size(l, val_w, lv_font_get_line_height(UI_FONT_NUM));
	lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -VAL_R, y);
	return l;
}

// Надпись шрифтом 20 (качка по оси, подпись X / Y) — по высоте посередине цифр
static int32_t row_label_y(int32_t row_y) {
	return row_y + (lv_font_get_line_height(UI_FONT_NUM) - lv_font_get_line_height(UI_FONT_MID)) / 2;
}

// ... от левого края
static lv_obj_t* row_label_create(lv_obj_t *parent, int32_t x, int32_t row_y, ui_col_t color) {
	lv_obj_t *l = ui_label_create(parent, UI_FONT_MID, color);
	lv_obj_set_pos(l, x, row_label_y(row_y));
	return l;
}

// ... правым краем в right пикселях от правого края карточки
static lv_obj_t* row_label_create_r(lv_obj_t *parent, int32_t right, int32_t row_y, ui_col_t color) {
	lv_obj_t *l = ui_label_create(parent, UI_FONT_MID, color);
	lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -right, row_label_y(row_y));
	return l;
}

static void card_create(int i) {
	sensor_card_t *c = &cards[i];
	c->card = ui_card_create(scr, CARD_X, CARD_Y0 + i * CARD_STEP, CARD_W, CARD_H);

	// Шапка: имя, точка записи, справа — состояние связи
	c->name = ui_label_create(c->card, UI_FONT_SMALL, UI_C_TEXT);
	lv_label_set_text_fmt(c->name, "Д%d", APP_SENSOR_ADDR(i));
	lv_obj_set_pos(c->name, HDR_X, HDR_Y);
	int32_t name_w = ui_text_width("Д0", UI_FONT_SMALL); // цифры моноширинные
	c->dot = ui_box_create(c->card, HDR_X + name_w + 6, 4, 8, 8, UI_C_RED);
	lv_obj_set_style_radius(c->dot, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_hidden(c->dot, true);
	c->link = ui_label_create(c->card, UI_FONT_SMALL, UI_C_DIM);
	lv_obj_align(c->link, LV_ALIGN_TOP_RIGHT, -HDR_R, HDR_Y);

	// Строки осей: слева качка, справа подпись X / Y и угол
	c->roll_x = row_label_create(c->card, ROLL_X, ROW_X_Y, UI_C_DIM);
	c->roll_y = row_label_create(c->card, ROLL_X, ROW_Y_Y, UI_C_DIM);
	int32_t cap_r = VAL_R + val_w + CAP_GAP;
	lv_label_set_text_static(row_label_create_r(c->card, cap_r, ROW_X_Y, UI_C_DIM), "X");
	lv_label_set_text_static(row_label_create_r(c->card, cap_r, ROW_Y_Y, UI_C_DIM), "Y");
	c->val_x = value_label_create(c->card, ROW_X_Y);
	c->val_y = value_label_create(c->card, ROW_Y_Y);

	c->shown = CHIP_NONE;
}

void ui_main_create(void) {
	scr = ui_screen_create();
	grp = lv_group_create();

	// --- Строка состояния ---
	lv_obj_t *bar = ui_bar_create(scr, BAR_H);

	// Дата ДД.ММ.ГГ и время ЧЧ:ММ: цифры моноширинные, поэтому места под
	// «00.00.00» и «00:00» хватает при любом значении, и плашка записи не
	// сдвигается
	int32_t x = BAR_PAD_L;
	lbl_date = ui_label_create(bar, UI_FONT_SMALL, UI_C_DIM);
	lv_obj_align(lbl_date, LV_ALIGN_LEFT_MID, x, 0);
	x += ui_text_width("00.00.00", UI_FONT_SMALL) + BAR_GAP_L;
	lbl_time = ui_label_create(bar, UI_FONT_SMALL, UI_C_TEXT);
	lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, x, 0);
	x += ui_text_width("00:00", UI_FONT_SMALL) + BAR_GAP_L;

	// Самая длинная надпись — «НЕТ SD»: до загрузки ЦП («ЦП 100%») остаётся
	// не меньше 6 px (проверка в симуляторе)
	pill_rec = ui_chip_create(bar, UI_FONT_SMALL);
	lv_obj_align(pill_rec, LV_ALIGN_LEFT_MID, x, 0);

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

	// --- Карточки датчиков ---
	val_w = ui_text_width(UI_MINUS "88.88" UI_DEG, UI_FONT_NUM);
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
	// Левая ячейка: рамка как у кнопок, без группы и нажатий — с корабликом
	lv_obj_t *slot = ui_button_create(scr, NULL, CTRL_GAP, CTRL_Y, CTRL_W, CTRL_H);
	lv_obj_set_clickable(slot, false);
	lv_obj_add_event_cb(slot, ship_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

	btn_freq = ctrl_create(1, "Частота", &val_freq);
	ui_field_attach(btn_freq, freq_step);

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

	lv_snprintf(buf, sizeof(buf), "%02u.%02u.%02u", t->date, t->month, t->year);
	ui_set_text(lbl_date, buf);
	lv_snprintf(buf, sizeof(buf), "%02u:%02u", t->hours, t->minutes);
	ui_set_text(lbl_time, buf);
	// Без DS3231 время после включения неверное — подсвечиваем
	ui_set_text_color(lbl_time, g_app.rtc_present ? UI_C_TEXT : UI_C_ORANGE);

	// Плашка записи; запись мигает заливкой
	bool blink_on = ((now / UI_BLINK_MS) & 1U) == 0;
	switch (g_app.sd_state) {
	case SD_NO_CARD:
		ui_set_text(pill_rec, "НЕТ SD");
		ui_set_chip(pill_rec, UI_C_RED_BG, UI_C_RED);
		break;
	case SD_READY:
		ui_set_text(pill_rec, "СТОП");
		ui_set_chip(pill_rec, UI_C_CHIP_BG, UI_C_DIM);
		break;
	case SD_RECORDING:
		ui_set_text(pill_rec, "ЗАП");
		if (blink_on)
			ui_set_chip(pill_rec, UI_C_REC, UI_C_ON_REC);
		else
			ui_set_chip(pill_rec, UI_C_RED_BG, UI_C_RED);
		break;
	case SD_ERROR:
	default:
		// Запись прервана ошибкой карты; код и что делать — в строке сообщений
		ui_set_text(pill_rec, "СБОЙ");
		ui_set_chip(pill_rec, UI_C_REC, UI_C_ON_REC);
		break;
	}

	update_cpu();
	update_battery(blink_on);
}

// Качка по оси датчика: только пока он отвечает
static void update_roll(lv_obj_t *l, const app_sensor_t *s, float span, bool calm) {
	char buf[24], num[16];
	if (s->status != SENSOR_OK) {
		ui_set_text(l, "");
	} else if (s->roll_fill_s < g_app.roll_window_s) {
		lv_snprintf(buf, sizeof(buf), "сбор %u с", (unsigned) s->roll_fill_s);
		ui_set_text(l, buf);
		ui_set_text_color(l, UI_C_DIM);
	} else if (calm) {
		ui_set_text(l, "покой");
		ui_set_text_color(l, UI_C_GREEN);
	} else {
		ui_fmt_fixed(num, sizeof(num), span, 2, UI_DEG);
		lv_snprintf(buf, sizeof(buf), "качка %s", num);
		ui_set_text(l, buf);
		ui_set_text_color(l, span > ROLL_RED_DEG ? UI_C_RED : UI_C_ORANGE);
	}
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
			ui_set_text(c->link, LV_SYMBOL_OK " OK");
			ui_set_text_color(c->link, UI_C_GREEN);
			ui_set_border_color(c->card, UI_C_BORDER);
			val = UI_C_TEXT;
			break;
		case CHIP_GARBLED:
			ui_set_text(c->link, LV_SYMBOL_WARNING " битые ответы: дубль адреса?");
			ui_set_text_color(c->link, UI_C_RED);
			ui_set_border_color(c->card, UI_C_RED);
			break;
		case CHIP_LOST:
			// Последние значения серым
			ui_set_text(c->link, "нет связи");
			ui_set_text_color(c->link, UI_C_ORANGE);
			ui_set_border_color(c->card, UI_C_ORANGE);
			break;
		case CHIP_ABSENT:
		default:
			ui_set_text(c->link, "не подключен");
			ui_set_text_color(c->link, UI_C_DIM);
			ui_set_border_color(c->card, UI_C_BORDER);
			break;
		}
		ui_set_text_color(c->name, s->status == SENSOR_ABSENT ? UI_C_GREY : UI_C_TEXT);
		ui_set_text_color(c->val_x, val);
		ui_set_text_color(c->val_y, val);
	}
	update_roll(c->roll_x, s, s->roll_x, s->calm_x);
	update_roll(c->roll_y, s, s->roll_y, s->calm_y);

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

	// Сводка по всем осям всех отвечающих датчиков: ГОТОВ — все в покое;
	// иначе худшая ось «КАЧКА 2.34° Д3 X»; пока окна не набраны — сбор
	uint8_t ref = g_app.stab_sensor;
	if (ref >= APP_SENSOR_COUNT) {
		changed = ui_set_text(pill_stab, "КАЧКА " UI_DASH);
		ui_set_chip(pill_stab, UI_C_CHIP_BG, UI_C_GREY);
	} else if (g_app.stab_fill_s < g_app.roll_window_s) {
		lv_snprintf(buf, sizeof(buf), "сбор %u/%u с", (unsigned) g_app.stab_fill_s,
				(unsigned) g_app.roll_window_s);
		changed = ui_set_text(pill_stab, buf);
		ui_set_chip(pill_stab, UI_C_CHIP_BG, UI_C_DIM);
	} else if (g_app.is_stable) {
		changed = ui_set_text(pill_stab, LV_SYMBOL_OK " ГОТОВ");
		ui_set_chip(pill_stab, UI_C_GREEN_BG, UI_C_GREEN);
	} else {
		// 10° и больше — с одним знаком: плашка не теснит сообщение справа
		char span[16];
		ui_fmt_fixed(span, sizeof(span), g_app.stab_span, g_app.stab_span < 9.995f ? 2 : 1,
				UI_DEG);
		lv_snprintf(buf, sizeof(buf), "КАЧКА %s Д%u %s", span, (unsigned) APP_SENSOR_ADDR(ref),
				g_app.stab_axis == 0 ? "X" : "Y");
		changed = ui_set_text(pill_stab, buf);
		ui_set_chip(pill_stab, UI_C_RED_BG, UI_C_RED);
	}
	if (changed)
		fit_info_width();
}

// Помещается ли текст в строку сообщений (ширина — от плашки качки)
static bool info_fits(const char *text) {
	return ui_text_width(text, UI_FONT_SMALL) <= lv_obj_get_style_width(lbl_info, LV_PART_MAIN);
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
		// «⚠ АКБ 9.6 В < 10.0 В», а если рядом широкая плашка качки и не
		// влезает — без порога: «⚠ АКБ 9.6 В»
		char v[12], lim[12];
		ui_fmt_fixed(v, sizeof(v), g_app.battery_v, 1, " В");
		ui_fmt_fixed(lim, sizeof(lim), g_app.bat_alarm_v, 1, " В");
		lv_snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " АКБ %s < %s", v, lim);
		if (!info_fits(buf))
			lv_snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " АКБ %s", v);
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
		// Запись прервана (плашка «СБОЙ»): продолжится после возврата тумблера в
		// STOP; рядом с широкой плашкой качки — короче
		lv_snprintf(buf, sizeof(buf), "SD E:%02u тумблер " UI_ARROW " СТОП", g_app.sd_err);
		if (!info_fits(buf))
			lv_snprintf(buf, sizeof(buf), "E:%02u тумблер " UI_ARROW " СТОП", g_app.sd_err);
		if (!info_fits(buf))
			lv_snprintf(buf, sizeof(buf), "E:%02u " UI_ARROW " СТОП", g_app.sd_err);
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
	char buf[24];

	lv_snprintf(buf, sizeof(buf), "%u Гц", g_app.log_freq_hz);
	ui_set_text(val_freq, buf);

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
