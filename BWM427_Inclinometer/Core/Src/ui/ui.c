/*
 * ui.c — интерфейс прибора на LVGL v9: инициализация, суперцикл, темы,
 * общие стили и помощники. Экраны: ui_main.c (главный), ui_settings.c (меню,
 * дата/время, смена Modbus-адреса), ui_sd.c (карта памяти), ui_help.c (справка).
 *
 * Интерфейс только читает g_app и вызывает app_*(); своих копий настроек не
 * держит (кроме значений, которые прямо сейчас редактируются).
 *
 * Управление энкодером (как в старой прошивке):
 *   вращение          — переход между пунктами (рамка цвета фокуса);
 *   нажатие на поле   — режим правки (жёлтая рамка и подложка), вращение
 *                       меняет значение, повторное нажатие — выход из правки;
 *   нажатие на кнопку — действие; на «Ноль» удержание 1 с — сброс нуля.
 *
 * Экраны: главный и корень меню живут всё время; остальные (дата и время,
 * адрес, карта памяти, справка, подменю плиток) временные — строятся при
 * входе и удаляются вместе со своей группой энкодера при уходе с них
 * (ui_screen_create_temp): так куча LVGL держит не больше трёх экранов.
 *
 * Темы (тёмная / светлая): все цвета — роли ui_col_t. Общие стили красятся
 * заново и рассылаются через lv_obj_report_style_change(), а объекты со
 * своими цветами (ui_set_*_color) помнят роль в user_data и перекрашиваются
 * обходом дерева. Экраны не пересоздаются: ни мигания, ни расхода кучи.
 */
#include <string.h>
#include "ui.h"
#include "ui_internal.h"
#include "lv_port.h"

lv_indev_t *ui_indev;

/*--------------------------------------------------------------------
 * Таблица тем: цвет каждой роли, 0xRRGGBB
 *
 * Светлая рассчитана на дешёвую TN-матрицу при дневном свете: светло-серый
 * фон и белые карточки (нет сплошного белого), почти чёрный текст, тёмные
 * подписи и насыщенные цвета состояний (на белом читаются и под углом).
 *--------------------------------------------------------------------*/
static const uint32_t theme_tab[2][UI_C_COUNT] = {
	[APP_THEME_DARK] = {
		[UI_C_BG]        = 0x000000,
		[UI_C_BAR]       = 0x15191E,
		[UI_C_CARD]      = 0x171B21,
		[UI_C_CARD_PR]   = 0x2C333B,
		[UI_C_BORDER]    = 0x30373F,
		[UI_C_LINE]      = 0x2A3038,
		[UI_C_TEXT]      = 0xFFFFFF,
		[UI_C_DIM]       = 0x9AA4AF,
		[UI_C_GREY]      = 0x5F6771,
		[UI_C_ACCENT]    = 0x22D3EE,
		[UI_C_FOCUS]     = 0x00E0FF,
		[UI_C_FOCUS_BG]  = 0x0A2C36,
		[UI_C_EDIT]      = 0xFFD400,
		[UI_C_EDIT_BG]   = 0x3A3100,
		[UI_C_EDIT_TEXT] = 0xFFD400,
		[UI_C_RED]       = 0xFF453A,
		[UI_C_RED_BG]    = 0x3F1513,
		[UI_C_GREEN]     = 0x30D158,
		[UI_C_GREEN_BG]  = 0x0E3A1C,
		[UI_C_ORANGE]    = 0xFF9F0A,
		[UI_C_ORANGE_BG] = 0x3F2B06,
		[UI_C_CHIP_BG]   = 0x262C33,
		[UI_C_REC]       = 0xE0261B,
		[UI_C_ON_REC]    = 0xFFFFFF,
	},
	[APP_THEME_LIGHT] = {
		[UI_C_BG]        = 0xD8DDE3,
		[UI_C_BAR]       = 0xFFFFFF,
		[UI_C_CARD]      = 0xFFFFFF,
		[UI_C_CARD_PR]   = 0xD5DCE4,
		[UI_C_BORDER]    = 0xA9B3BE,
		[UI_C_LINE]      = 0xBCC4CD,
		[UI_C_TEXT]      = 0x0B0E12,
		[UI_C_DIM]       = 0x46515E,
		[UI_C_GREY]      = 0x8A949F,
		[UI_C_ACCENT]    = 0x0057C2,
		[UI_C_FOCUS]     = 0x0062E0,
		[UI_C_FOCUS_BG]  = 0xDCEAFF,
		[UI_C_EDIT]      = 0xE89400,
		[UI_C_EDIT_BG]   = 0xFFEDB8,
		[UI_C_EDIT_TEXT] = 0x7A4300,
		[UI_C_RED]       = 0xCC0A1E,
		[UI_C_RED_BG]    = 0xFCDCDF,
		[UI_C_GREEN]     = 0x07803A,
		[UI_C_GREEN_BG]  = 0xD2F1DD,
		[UI_C_ORANGE]    = 0xBF4E00,
		[UI_C_ORANGE_BG] = 0xFFE4C9,
		[UI_C_CHIP_BG]   = 0xE4E8ED,
		[UI_C_REC]       = 0xD7141E,
		[UI_C_ON_REC]    = 0xFFFFFF,
	},
};

static uint8_t theme_cur = APP_THEME_DARK;

// Действующие экраны (для перекраски при смене темы); временный экран
// убирается отсюда сразу, как с него ушли, а удаляется чуть позже
#define UI_SCREEN_MAX 8
static lv_obj_t *screens[UI_SCREEN_MAX];
static bool screen_temp[UI_SCREEN_MAX];
static uint8_t screen_cnt;

static lv_style_t st_screen;    // фон экрана
static lv_style_t st_bar;       // полоса заголовка / строки состояния
static lv_style_t st_card;      // карточка датчика
static lv_style_t st_btn;       // кнопка: фон, рамка
static lv_style_t st_btn_pr;    // нажата
static lv_style_t st_btn_focus; // в фокусе
static lv_style_t st_btn_edit;  // редактируется
static lv_style_t st_box;       // простой залитый прямоугольник
static lv_style_t st_chip;      // плашка (скруглённая подложка метки)
static lv_style_t st_scrollbar; // полоса прокрутки списков

static uint32_t last_update_ms;

lv_color_t ui_color(ui_col_t c) {
	return lv_color_hex(theme_tab[theme_cur][c]);
}

/*--------------------------------------------------------------------
 * Стили
 *--------------------------------------------------------------------*/
// Геометрия — один раз
static void styles_init(void) {
	lv_style_init(&st_screen);
	lv_style_set_bg_opa(&st_screen, LV_OPA_COVER);
	lv_style_set_text_font(&st_screen, UI_FONT_SMALL);

	lv_style_init(&st_bar);
	lv_style_set_bg_opa(&st_bar, LV_OPA_COVER);
	lv_style_set_radius(&st_bar, 0);
	lv_style_set_border_width(&st_bar, 1);
	lv_style_set_border_side(&st_bar, LV_BORDER_SIDE_BOTTOM);
	lv_style_set_pad_all(&st_bar, 0);

	lv_style_init(&st_card);
	lv_style_set_bg_opa(&st_card, LV_OPA_COVER);
	lv_style_set_radius(&st_card, UI_RADIUS_CARD);
	lv_style_set_border_width(&st_card, 1);
	lv_style_set_pad_all(&st_card, 0);

	lv_style_init(&st_btn);
	lv_style_set_bg_opa(&st_btn, LV_OPA_COVER);
	lv_style_set_radius(&st_btn, UI_RADIUS_BTN);
	// Рамка всегда одной толщины, меняется только цвет — содержимое не сдвигается
	lv_style_set_border_width(&st_btn, UI_BTN_BORDER);
	lv_style_set_pad_all(&st_btn, 0);

	lv_style_init(&st_btn_pr);
	lv_style_init(&st_btn_focus);
	lv_style_init(&st_btn_edit);

	lv_style_init(&st_box);
	lv_style_set_bg_opa(&st_box, LV_OPA_COVER);
	lv_style_set_radius(&st_box, 0);
	lv_style_set_border_width(&st_box, 0);
	lv_style_set_pad_all(&st_box, 0);

	lv_style_init(&st_chip);
	lv_style_set_bg_opa(&st_chip, LV_OPA_COVER);
	lv_style_set_radius(&st_chip, 9);
	lv_style_set_pad_hor(&st_chip, 7);
	lv_style_set_pad_ver(&st_chip, 1);

	// Узкая полоса у правого края: где мы в списке и что он длиннее экрана
	lv_style_init(&st_scrollbar);
	lv_style_set_bg_opa(&st_scrollbar, LV_OPA_COVER);
	lv_style_set_width(&st_scrollbar, 3);
	lv_style_set_radius(&st_scrollbar, 2);
	lv_style_set_pad_right(&st_scrollbar, 2);
	lv_style_set_pad_ver(&st_scrollbar, 4);
}

// Цвета общих стилей — из текущей темы (при старте и при смене темы)
static void styles_set_colors(void) {
	lv_style_set_bg_color(&st_screen, ui_color(UI_C_BG));
	lv_style_set_text_color(&st_screen, ui_color(UI_C_TEXT));

	lv_style_set_bg_color(&st_bar, ui_color(UI_C_BAR));
	lv_style_set_border_color(&st_bar, ui_color(UI_C_LINE));

	lv_style_set_bg_color(&st_card, ui_color(UI_C_CARD));
	lv_style_set_border_color(&st_card, ui_color(UI_C_BORDER));

	lv_style_set_bg_color(&st_btn, ui_color(UI_C_CARD));
	lv_style_set_border_color(&st_btn, ui_color(UI_C_BORDER));
	lv_style_set_text_color(&st_btn, ui_color(UI_C_TEXT));

	lv_style_set_bg_color(&st_btn_pr, ui_color(UI_C_CARD_PR));

	lv_style_set_border_color(&st_btn_focus, ui_color(UI_C_FOCUS));
	lv_style_set_bg_color(&st_btn_focus, ui_color(UI_C_FOCUS_BG));

	lv_style_set_border_color(&st_btn_edit, ui_color(UI_C_EDIT));
	lv_style_set_bg_color(&st_btn_edit, ui_color(UI_C_EDIT_BG));
	lv_style_set_text_color(&st_btn_edit, ui_color(UI_C_EDIT_TEXT));

	lv_style_set_bg_color(&st_scrollbar, ui_color(UI_C_GREY));
}

/*--------------------------------------------------------------------
 * Цвета по ролям
 *
 * В user_data объекта упакованы роли его собственных цветов: по байту на
 * свойство (0 — не задан, иначе роль + 1). Сравнение роли заменяет проверку
 * «изменилось ли» и позволяет перекрасить объект при смене темы.
 *--------------------------------------------------------------------*/
enum {
	ROLE_TEXT = 0, ROLE_BG = 8, ROLE_BORDER = 16
};

static void role_apply(lv_obj_t *obj, unsigned prop, ui_col_t c) {
	switch (prop) {
	case ROLE_TEXT:
		lv_obj_set_style_text_color(obj, ui_color(c), 0);
		break;
	case ROLE_BG:
		lv_obj_set_style_bg_color(obj, ui_color(c), 0);
		break;
	default:
		lv_obj_set_style_border_color(obj, ui_color(c), 0);
		break;
	}
}

static void role_set(lv_obj_t *obj, unsigned prop, ui_col_t c) {
	uint32_t roles = (uint32_t) (uintptr_t) lv_obj_get_user_data(obj);
	uint32_t v = (uint32_t) c + 1U;
	if (((roles >> prop) & 0xFFU) == v)
		return;
	roles = (roles & ~(0xFFUL << prop)) | (v << prop);
	lv_obj_set_user_data(obj, (void*) (uintptr_t) roles);
	role_apply(obj, prop, c);
}

void ui_set_text_color(lv_obj_t *obj, ui_col_t color) {
	role_set(obj, ROLE_TEXT, color);
}

void ui_set_bg_color(lv_obj_t *obj, ui_col_t color) {
	role_set(obj, ROLE_BG, color);
}

void ui_set_border_color(lv_obj_t *obj, ui_col_t color) {
	role_set(obj, ROLE_BORDER, color);
}

void ui_set_chip(lv_obj_t *chip, ui_col_t bg, ui_col_t text) {
	role_set(chip, ROLE_BG, bg);
	role_set(chip, ROLE_TEXT, text);
}

/*--------------------------------------------------------------------
 * Смена темы
 *--------------------------------------------------------------------*/
static lv_obj_tree_walk_res_t recolor_cb(lv_obj_t *obj, void *user_data) {
	(void) user_data;
	uint32_t roles = (uint32_t) (uintptr_t) lv_obj_get_user_data(obj);
	for (unsigned prop = ROLE_TEXT; prop <= ROLE_BORDER; prop += 8) {
		uint32_t v = (roles >> prop) & 0xFFU;
		if (v != 0)
			role_apply(obj, prop, (ui_col_t) (v - 1U));
	}
	return LV_OBJ_TREE_WALK_NEXT;
}

static void theme_apply(uint8_t theme) {
	theme_cur = theme;
	styles_set_colors();
	lv_obj_report_style_change(NULL); // все объекты с общими стилями, на всех экранах
	for (uint8_t i = 0; i < screen_cnt; i++)
		lv_obj_tree_walk(screens[i], recolor_cb, NULL);
}

void ui_theme_sync(void) {
	uint8_t t = (g_app.theme == APP_THEME_LIGHT) ? APP_THEME_LIGHT : APP_THEME_DARK;
	if (t != theme_cur)
		theme_apply(t);
}

/*--------------------------------------------------------------------
 * Создание объектов
 *--------------------------------------------------------------------*/
static int screen_index(const lv_obj_t *scr) {
	for (uint8_t i = 0; i < screen_cnt; i++) {
		if (screens[i] == scr)
			return i;
	}
	return -1;
}

static void screen_forget(int i) {
	screen_cnt--;
	screens[i] = screens[screen_cnt];
	screen_temp[i] = screen_temp[screen_cnt];
}

// Экран удаляется: удалить и его группу (user_data). Событие приходит
// раньше, чем удаляются дети, поэтому группа удаляется, пока они в ней
static void screen_delete_cb(lv_event_t *e) {
	int i = screen_index(lv_event_get_current_target(e));
	if (i >= 0)
		screen_forget(i);
	lv_group_t *g = lv_event_get_user_data(e);
	if (g != NULL)
		lv_group_delete(g);
}

static lv_obj_t* screen_new(lv_group_t *temp_grp) {
	lv_obj_t *scr = lv_obj_create(NULL);
	lv_obj_add_style(scr, &st_screen, 0);
	lv_obj_set_scrollable(scr, false);
	// Одновременно живут главный экран, корень меню и один временный — с
	// запасом; при переполнении экран просто не перекрасится при смене темы
	// (LV_ASSERT здесь пустой: LV_USE_ASSERT выключен)
	if (screen_cnt < UI_SCREEN_MAX) {
		screens[screen_cnt] = scr;
		screen_temp[screen_cnt] = (temp_grp != NULL);
		screen_cnt++;
	}
	lv_obj_add_event_cb(scr, screen_delete_cb, LV_EVENT_DELETE, temp_grp);
	return scr;
}

lv_obj_t* ui_screen_create(void) {
	return screen_new(NULL);
}

lv_obj_t* ui_screen_create_temp(lv_group_t *g) {
	return screen_new(g);
}

bool ui_screen_alive(const lv_obj_t *scr) {
	return scr != NULL && screen_index(scr) >= 0;
}

// Ушли с временного экрана — удалить его. Обычно мы ещё внутри обработки
// нажатия на нём (LVGL после обработчика обращается к его группе), поэтому
// удаление — отложенное, а из списка действующих экран убирается сразу
static void leave_screen(lv_obj_t *old, lv_obj_t *scr) {
	int i = screen_index(old);
	if (old != scr && i >= 0 && screen_temp[i]) {
		screen_forget(i);
		lv_obj_delete_async(old);
	}
}

lv_obj_t* ui_box_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
		ui_col_t color) {
	lv_obj_t *o = lv_obj_create(parent);
	lv_obj_add_style(o, &st_box, 0);
	ui_set_bg_color(o, color);
	lv_obj_set_scrollable(o, false);
	lv_obj_set_clickable(o, false);
	lv_obj_set_pos(o, x, y);
	lv_obj_set_size(o, w, h);
	return o;
}

lv_obj_t* ui_card_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h) {
	lv_obj_t *o = lv_obj_create(parent);
	lv_obj_add_style(o, &st_card, 0);
	lv_obj_set_scrollable(o, false);
	lv_obj_set_clickable(o, false);
	lv_obj_set_pos(o, x, y);
	lv_obj_set_size(o, w, h);
	return o;
}

lv_obj_t* ui_label_create(lv_obj_t *parent, const lv_font_t *font, ui_col_t color) {
	lv_obj_t *l = lv_label_create(parent);
	lv_obj_set_style_text_font(l, font, 0);
	ui_set_text_color(l, color);
	lv_label_set_text_static(l, "");
	return l;
}

lv_obj_t* ui_chip_create(lv_obj_t *parent, const lv_font_t *font) {
	lv_obj_t *l = lv_label_create(parent);
	lv_obj_add_style(l, &st_chip, 0);
	lv_obj_set_style_text_font(l, font, 0);
	ui_set_chip(l, UI_C_CHIP_BG, UI_C_DIM);
	lv_label_set_text_static(l, "");
	return l;
}

lv_obj_t* ui_button_create(lv_obj_t *parent, lv_group_t *g, int32_t x, int32_t y,
		int32_t w, int32_t h) {
	lv_obj_t *b = lv_button_create(parent);
	lv_obj_add_style(b, &st_btn, 0);
	lv_obj_add_style(b, &st_btn_focus, LV_STATE_FOCUSED);
	lv_obj_add_style(b, &st_btn_edit, LV_STATE_EDITED);
	lv_obj_add_style(b, &st_btn_pr, LV_STATE_PRESSED);
	lv_obj_set_scrollable(b, false);
	lv_obj_set_scroll_on_focus(b, false);
	lv_obj_set_pos(b, x, y);
	lv_obj_set_size(b, w, h);
	if (g)
		lv_group_add_obj(g, b);
	return b;
}

lv_obj_t* ui_bar_create(lv_obj_t *scr, int32_t h) {
	lv_obj_t *bar = lv_obj_create(scr);
	lv_obj_add_style(bar, &st_bar, 0);
	lv_obj_set_scrollable(bar, false);
	lv_obj_set_clickable(bar, false);
	lv_obj_set_pos(bar, 0, 0);
	lv_obj_set_size(bar, UI_W, h);
	return bar;
}

lv_obj_t* ui_title_create(lv_obj_t *scr, const char *icon, const char *text) {
	lv_obj_t *bar = ui_bar_create(scr, UI_BAR_H);

	lv_obj_t *ic = ui_label_create(bar, UI_FONT_MID, UI_C_ACCENT);
	lv_label_set_text_static(ic, icon);
	lv_obj_align(ic, LV_ALIGN_LEFT_MID, 10, 0);

	// Текст — с 38 px, а после широкого значка (батарея) — с отступом 6 px
	int32_t x = 10 + ui_text_width(icon, UI_FONT_MID) + 6;
	lv_obj_t *l = ui_label_create(bar, UI_FONT_MID, UI_C_TEXT);
	lv_label_set_text_static(l, text);
	lv_obj_align(l, LV_ALIGN_LEFT_MID, x > 38 ? x : 38, 0);
	return bar;
}

lv_obj_t* ui_action_button_create(lv_obj_t *parent, lv_group_t *g, int32_t x, int32_t y,
		int32_t w, int32_t h, const char *text, lv_event_cb_t cb) {
	lv_obj_t *b = ui_button_create(parent, g, x, y, w, h);
	lv_obj_t *l = lv_label_create(b);
	lv_obj_set_style_text_font(l, UI_FONT_MID, 0);
	lv_label_set_text_static(l, text);
	lv_obj_center(l);
	lv_obj_add_event_cb(b, cb, LV_EVENT_SHORT_CLICKED, NULL);
	return b;
}

lv_obj_t* ui_scroll_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h) {
	lv_obj_t *o = lv_obj_create(parent); // без темы: прозрачный, без рамки и полей
	lv_obj_add_style(o, &st_scrollbar, LV_PART_SCROLLBAR);
	lv_obj_set_scroll_dir(o, LV_DIR_VER);
	lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_AUTO);
	lv_obj_set_clickable(o, false);
	lv_obj_set_pos(o, x, y);
	lv_obj_set_size(o, w, h);
	return o;
}

/*--------------------------------------------------------------------
 * Поле с правкой энкодером
 *--------------------------------------------------------------------*/
static void field_event_cb(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_current_target(e);
	lv_group_t *g = lv_obj_get_group(obj);
	const ui_step_cb_t *cb = lv_event_get_user_data(e);

	if (g == NULL)
		return;
	if (code == LV_EVENT_SHORT_CLICKED) {
		// Кнопка не «editable» для LVGL, поэтому режим правки переключаем сами
		lv_group_set_editing(g, !lv_group_get_editing(g));
	} else if (code == LV_EVENT_KEY && lv_group_get_editing(g)) {
		uint32_t key = lv_event_get_key(e);
		if (key == LV_KEY_RIGHT || key == LV_KEY_UP)
			(*cb)(1);
		else if (key == LV_KEY_LEFT || key == LV_KEY_DOWN)
			(*cb)(-1);
	}
}

// Указатели на функции нельзя класть в void* user_data (ISO C), поэтому
// храним их в статической таблице и передаём адрес ячейки. Временные экраны
// создают свои поля при каждом входе — ячейка одной функции общая, таблица
// не растёт (UI_FIELD_MAX — число разных функций шага во всём интерфейсе)
#define UI_FIELD_MAX 16
static ui_step_cb_t field_cbs[UI_FIELD_MAX];
static uint8_t field_cnt;

void ui_field_attach(lv_obj_t *btn, ui_step_cb_t cb) {
	uint8_t i = 0;
	while (i < field_cnt && field_cbs[i] != cb)
		i++;
	if (i == field_cnt) {
		LV_ASSERT(field_cnt < UI_FIELD_MAX);
		if (field_cnt >= UI_FIELD_MAX)
			return; // поле не будет правиться, но и память не испорчена
		field_cbs[field_cnt++] = cb;
	}
	lv_obj_add_event_cb(btn, field_event_cb, LV_EVENT_SHORT_CLICKED, &field_cbs[i]);
	lv_obj_add_event_cb(btn, field_event_cb, LV_EVENT_KEY, &field_cbs[i]);
}

/*--------------------------------------------------------------------
 * Обновление без лишней перерисовки
 *--------------------------------------------------------------------*/
bool ui_set_text(lv_obj_t *label, const char *text) {
	const char *old = lv_label_get_text(label);
	if (old != NULL && strcmp(old, text) == 0)
		return false;
	lv_label_set_text(label, text);
	return true;
}

int32_t ui_text_width(const char *text, const lv_font_t *font) {
	lv_point_t sz;
	lv_text_get_size(&sz, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
	return sz.x;
}

void ui_set_hidden(lv_obj_t *obj, bool hidden) {
	if (lv_obj_is_hidden(obj) != hidden)
		lv_obj_set_hidden(obj, hidden);
}

void ui_set_y(lv_obj_t *obj, int32_t y) {
	if (lv_obj_get_y_aligned(obj) != y)
		lv_obj_set_y(obj, y);
}

void ui_show(lv_obj_t *scr, lv_group_t *g) {
	lv_group_t *old = lv_indev_get_group(ui_indev);
	if (old)
		lv_group_set_editing(old, false);
	lv_group_set_editing(g, false);
	lv_indev_set_group(ui_indev, g);
	// Фокус на первый элемент группы
	lv_obj_t *first = lv_group_get_obj_by_index(g, 0);
	if (first)
		lv_group_focus_obj(first);
	lv_indev_wait_release(ui_indev); // не считать текущее нажатие новым экраном
	lv_obj_t *prev = lv_screen_active();
	lv_screen_load(scr);
	leave_screen(prev, scr);
}

void ui_show_back(lv_obj_t *scr, lv_group_t *g) {
	lv_group_t *old = lv_indev_get_group(ui_indev);
	if (old)
		lv_group_set_editing(old, false);
	lv_group_set_editing(g, false);
	lv_indev_set_group(ui_indev, g);
	lv_indev_wait_release(ui_indev);
	lv_obj_t *prev = lv_screen_active();
	lv_screen_load(scr);
	leave_screen(prev, scr);
}

/*--------------------------------------------------------------------
 * Форматирование чисел (целочисленное, без %f)
 *--------------------------------------------------------------------*/
// Округлить v * 10^decimals до целого
static int32_t scale_round(float v, uint8_t decimals) {
	float k = 1.0f;
	for (uint8_t i = 0; i < decimals; i++)
		k *= 10.0f;
	float s = v * k;
	if (s > 2.0e9f)
		s = 2.0e9f;
	if (s < -2.0e9f)
		s = -2.0e9f;
	return (int32_t) (s >= 0.0f ? s + 0.5f : s - 0.5f);
}

void ui_fmt_fixed(char *buf, size_t n, float v, uint8_t decimals, const char *suffix) {
	if (v != v) { // NaN
		lv_snprintf(buf, n, "%s%s", UI_DASH, suffix);
		return;
	}
	int32_t s = scale_round(v, decimals);
	const char *sign = "";
	if (s < 0) {
		sign = UI_MINUS;
		s = -s;
	}
	if (decimals == 0) {
		lv_snprintf(buf, n, "%s%ld%s", sign, (long) s, suffix);
		return;
	}
	int32_t k = 1;
	for (uint8_t i = 0; i < decimals; i++)
		k *= 10;
	lv_snprintf(buf, n, "%s%ld.%0*ld%s", sign, (long) (s / k), (int) decimals,
			(long) (s % k), suffix);
}

void ui_fmt_angle(char *buf, size_t n, float deg) {
	if (deg != deg) {
		lv_snprintf(buf, n, "%s", UI_DASH);
		return;
	}
	// Как "%+.2f°": разрешение датчика 0.01°, третий знак — только шум фильтра
	int32_t s = scale_round(deg, 2);
	if (s > 99999)
		s = 99999;
	if (s < -99999)
		s = -99999;
	const char *sign = (s < 0) ? UI_MINUS : "+";
	if (s < 0)
		s = -s;
	lv_snprintf(buf, n, "%s%ld.%02ld" UI_DEG, sign, (long) (s / 100), (long) (s % 100));
}

/*--------------------------------------------------------------------
 * Точки входа (ui.h)
 *--------------------------------------------------------------------*/
void ui_init(void) {
	lv_init();
	lv_port_disp_init();
	ui_indev = lv_port_indev_init();

	// Тема из сохранённых настроек (app_init() уже прочитал их из флеша)
	theme_cur = (g_app.theme == APP_THEME_LIGHT) ? APP_THEME_LIGHT : APP_THEME_DARK;
	styles_init();
	styles_set_colors();
	ui_settings_create();
	ui_main_create();
	ui_main_show();

	last_update_ms = lv_tick_get();
	ui_main_update(last_update_ms);
	lv_refr_now(NULL); // сразу нарисовать экран (после ILI9341_Init в GRAM мусор)
}

void ui_task(void) {
	uint32_t now = lv_tick_get();
	if (lv_tick_diff(now, last_update_ms) >= UI_UPDATE_MS) {
		last_update_ms = now;
		ui_theme_sync(); // тему могли сменить не из меню (например, по USB)
		ui_main_update(now);
		ui_settings_update(now);
	}
	lv_timer_handler();
}
