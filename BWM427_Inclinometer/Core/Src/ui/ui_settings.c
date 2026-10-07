/*
 * ui_settings.c — меню «Настройки» и экраны даты/времени и адреса датчика.
 *
 * Вид меню задаётся UI_MENU_STYLE (ui_internal.h):
 *   UI_MENU_LIST  — прокручиваемый список: Дата и время / Карта памяти /
 *                   Фильтр α / Окно качки / Отсчёты качки / Порог покоя /
 *                   Гистерезис / Адрес датчика / Сбросить ноль / Пауза шины /
 *                   Порог АКБ / Тема / Справка / Назад. Фокус ведёт прокрутку,
 *                   полоса справа показывает место в списке;
 *   UI_MENU_TILES — плитки разделов с кратким состоянием, за ними короткие
 *                   подменю без прокрутки:
 *                     Датчики  — Частота, Фильтр α, Пауза шины, Адрес датчика,
 *                                Сбросить ноль;
 *                     Качка    — Окно, Отсчёты, Порог покоя, Гистерезис;
 *                     Питание  — напряжение, заряд, Порог АКБ;
 *                     SD-карта — экран карты памяти;
 *                     Прибор   — Дата и время, Тема, Справка;
 *                     Назад.
 * Экраны «Дата и время», «Адрес датчика», «Карта памяти» (ui_sd.c) и
 * «Справка» (ui_help.c) общие; «Назад» на них возвращает туда, откуда
 * пришли, с фокусом на том же пункте.
 *
 * Корень меню (список или плитки) живёт всё время; подменю и общие экраны
 * временные (ui_screen_create_temp): строятся при входе, удаляются при уходе.
 *
 *   дата и время  — ДД.ММ.ГГ ЧЧ:ММ энкодером, «OK» -> app_set_time(), «Отмена»;
 *   адрес датчика — смена Modbus-адреса BWM427 (app_sensor_set_address()),
 *                   состояние из g_app.svc_state.
 *
 * Поля (Частота, Фильтр α, параметры качки, Порог АКБ) правятся как
 * «Частота» на главном экране: нажатие — правка, вращение — шаг, повторное
 * нажатие — готово. Без часов DS3231 пункт «Дата и время» горит оранжевым,
 * пояснение — на экране даты и времени.
 *
 * В заголовке меню справа — заряд АКБ и часы: «АКБ 76%  14:36:23».
 */
#include "ui_internal.h"
#include "version.h"

// Заголовок меню: часы у правого края, заряд АКБ слева от них
#define TITLE_PAD_R         10
#define TITLE_GAP           12

// Значения по умолчанию для смены адреса: новые датчики приходят с адресом 1,
// второй датчик на шине обычно 2, поэтому новый ставим на 3
#define ADDR_OLD_DEFAULT    1
#define ADDR_NEW_DEFAULT    3
#define ADDR_MIN            1
#define ADDR_MAX            247

// Строка меню: значок, текст (шрифт 20), значение справа (шрифт 14)
#define ROW_ICON_X          10
#define ROW_TEXT_X          40
#define ROW_VALUE_R         10      // от правого края строки

// Список (UI_MENU_LIST): строки по 27 px с зазором 3 px; справа от строк
// полоса прокрутки, поэтому строки уже экрана
#define LIST_ROW_X          6
#define LIST_ROW_W          (UI_W - LIST_ROW_X - 12)
#define LIST_ROW_H          27
#define LIST_STEP           30

// Подменю (UI_MENU_TILES): строки как в прежнем меню, без прокрутки
#define SUB_Y0              (UI_BAR_H + 4)
#define SUB_STEP            34
#define SUB_ROW_H           31

// Плитки: 2 столбца x 3 ряда
#define TILE_GAP            4
#define TILE_W              ((UI_W - 3 * TILE_GAP) / 2)
#define TILE_H              65

// Значок качки — волна
#define ICON_ROLL           "~"

// Шаги полей качки
#define ROLL_WIN_STEP_S     5       // окно — по 5 с
#define ROLL_DEG_STEP       5       // порог и гистерезис — по 0.05° (в сотых)

// Пауза шины RS485 перед запросом, мс: нажатие на пункт меню перебирает значения
static const uint8_t gap_steps[] = { 3, 5, 8, 10, 15, 20, 30, 50 };

/*====================================================================
 * Пункты и страницы меню
 *====================================================================*/
typedef enum {
	IT_TIME = 0, IT_SD, IT_FREQ, IT_ALPHA, IT_RWIN, IT_RRATE, IT_RCALM, IT_RHYST,
	IT_ADDR, IT_ZERO, IT_GAP, IT_BAT, IT_THEME, IT_HELP, IT_BACK,
	IT_PG_SENS, IT_PG_ROLL, IT_PG_POWER, IT_PG_DEV,   // разделы (только плитки)
	IT_COUNT
} item_t;

// Страница меню: экран со своей группой энкодера и заголовком. Корень
// строится один раз, подменю — при каждом входе (build)
typedef struct page page_t;
struct page {
	lv_obj_t *scr;
	lv_group_t *grp;
	lv_obj_t *clock, *bat;     // в заголовке справа
	const char *icon, *title;
	void (*build)(page_t *pg); // строки подменю
	void (*forget)(void);      // подменю удалено: забыть свои объекты (не пункты)
	uint32_t focus;            // пункт в фокусе, когда ушли на общий экран
};

static page_t pg_root;
static page_t *pg_cur = &pg_root;          // где были в меню (ui_menu_back)
#if UI_MENU_STYLE == UI_MENU_LIST
static lv_obj_t *menu_list;                // прокручиваемый список
#endif

// Объекты пунктов (NULL — страница пункта сейчас не построена);
// «Назад» бывает на нескольких страницах — его не храним
static lv_obj_t *it_btn[IT_COUNT], *it_icon[IT_COUNT], *it_value[IT_COUNT];
static page_t *it_page[IT_COUNT];

static void time_show(void);
static void addr_show(void);

// Подменю удалено (ушли с него): забыть его объекты
static void page_delete_cb(lv_event_t *e);

static void page_create(page_t *pg) {
	pg->grp = lv_group_create();
	if (pg->build == NULL) {
		pg->scr = ui_screen_create();
	} else {
		pg->scr = ui_screen_create_temp(pg->grp);
		lv_obj_add_event_cb(pg->scr, page_delete_cb, LV_EVENT_DELETE, pg);
	}
	lv_obj_t *bar = ui_title_create(pg->scr, pg->icon, pg->title);
	pg->clock = ui_label_create(bar, UI_FONT_SMALL, UI_C_DIM);
	lv_obj_align(pg->clock, LV_ALIGN_RIGHT_MID, -TITLE_PAD_R, 0);
	// Ширина часов постоянная (цифры моноширинные) — заряд АКБ не прыгает
	pg->bat = ui_label_create(bar, UI_FONT_SMALL, UI_C_DIM);
	lv_obj_align(pg->bat, LV_ALIGN_RIGHT_MID,
			-(TITLE_PAD_R + ui_text_width("00:00:00", UI_FONT_SMALL) + TITLE_GAP), 0);
	if (pg->build != NULL)
		pg->build(pg);
}

// back: вернуться на страницу с фокусом на том пункте, с которого ушли
static void page_show(page_t *pg, bool back) {
	if (!ui_screen_alive(pg->scr)) {
		page_create(pg);
		ui_show(pg->scr, pg->grp);
		lv_obj_t *f = lv_group_get_obj_by_index(pg->grp, back ? pg->focus : 0);
		if (f != NULL)
			lv_group_focus_obj(f);
	} else if (back) {
		ui_show_back(pg->scr, pg->grp);
	} else {
		ui_show(pg->scr, pg->grp);
	}
	pg_cur = pg;
	ui_settings_update(lv_tick_get());
}

// Уходим со страницы меню на общий экран: запомнить пункт в фокусе
static void page_leave(void) {
	lv_obj_t *f = lv_group_get_focused(pg_cur->grp);
	for (uint32_t i = 0; i < lv_group_get_obj_count(pg_cur->grp); i++) {
		if (lv_group_get_obj_by_index(pg_cur->grp, i) == f)
			pg_cur->focus = i;
	}
}

void ui_menu_show(void) {
#if UI_MENU_STYLE == UI_MENU_LIST
	lv_obj_scroll_to_y(menu_list, 0, LV_ANIM_OFF); // с начала списка
#endif
	page_show(&pg_root, false);
}

void ui_menu_back(void) {
	page_show(pg_cur, true);
}

/*--------------------------------------------------------------------
 * Действия
 *--------------------------------------------------------------------*/
#if UI_MENU_STYLE == UI_MENU_TILES
static page_t pg_sens, pg_roll, pg_power, pg_dev;
#endif

static void menu_event_cb(lv_event_t *e) {
	switch ((item_t) (uintptr_t) lv_event_get_user_data(e)) {
	case IT_TIME:
		page_leave();
		time_show();
		break;
	case IT_SD:
		page_leave();
		ui_sd_show();
		break;
	case IT_ADDR:
		page_leave();
		addr_show();
		break;
	case IT_HELP:
		page_leave();
		ui_help_show();
		break;
	case IT_ZERO:
		app_zero_reset(); // результат виден справа: «задан» -> «не задан»
		ui_settings_update(lv_tick_get());
		break;
	case IT_GAP: {
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
	case IT_THEME:
		// Сразу перекрасить все экраны (сохраняет тему логика)
		app_set_theme(g_app.theme == APP_THEME_LIGHT ? APP_THEME_DARK : APP_THEME_LIGHT);
		ui_theme_sync();
		ui_settings_update(lv_tick_get());
		break;
#if UI_MENU_STYLE == UI_MENU_TILES
	case IT_PG_SENS:
		page_show(&pg_sens, false);
		break;
	case IT_PG_ROLL:
		page_show(&pg_roll, false);
		break;
	case IT_PG_POWER:
		page_show(&pg_power, false);
		break;
	case IT_PG_DEV:
		page_show(&pg_dev, false);
		break;
#endif
	case IT_BACK:
	default:
		if (pg_cur == &pg_root)
			ui_main_show();
		else
			page_show(&pg_root, true); // из подменю — к плиткам
		break;
	}
}

// Шаг поля в целых долях (scale — 10 или 100): без накопления ошибки float
static float step_fixed(float v, int32_t step, float scale, float min, float max) {
	int32_t t = (int32_t) (v * scale + 0.5f) + step;
	if (t < (int32_t) (min * scale + 0.5f))
		t = (int32_t) (min * scale + 0.5f);
	if (t > (int32_t) (max * scale + 0.5f))
		t = (int32_t) (max * scale + 0.5f);
	return (float) t / scale;
}

static int32_t clamp_i(int32_t v, int32_t min, int32_t max) {
	return v < min ? min : (v > max ? max : v);
}

static void alpha_step(int32_t step) {
	app_set_ema_alpha(step_fixed(g_app.ema_alpha, step, 100.0f, APP_ALPHA_MIN, APP_ALPHA_MAX));
	ui_settings_update(lv_tick_get()); // показать сразу
}

static void bat_alarm_step(int32_t step) {
	app_set_bat_alarm(step_fixed(g_app.bat_alarm_v, step, 10.0f, APP_BAT_ALARM_MIN_V,
			APP_BAT_ALARM_MAX_V));
	ui_settings_update(lv_tick_get());
}

// Окно и частота отсчётов качки: смена сбрасывает накопленные окна (логика)
static void roll_win_step(int32_t step) {
	int32_t v = clamp_i((int32_t) g_app.roll_window_s + step * ROLL_WIN_STEP_S,
			APP_ROLL_WIN_MIN_S, APP_ROLL_WIN_MAX_S);
	if (v != g_app.roll_window_s)
		app_set_roll_window((uint8_t) v);
	ui_settings_update(lv_tick_get());
}

static void roll_rate_step(int32_t step) {
	int32_t v = clamp_i((int32_t) g_app.roll_rate_hz + step, APP_ROLL_RATE_MIN_HZ,
			APP_ROLL_RATE_MAX_HZ);
	if (v != g_app.roll_rate_hz)
		app_set_roll_rate((uint8_t) v);
	ui_settings_update(lv_tick_get());
}

static void roll_calm_step(int32_t step) {
	app_set_roll_calm(step_fixed(g_app.roll_calm_deg, step * ROLL_DEG_STEP, 100.0f,
			APP_ROLL_CALM_MIN_DEG, APP_ROLL_CALM_MAX_DEG));
	ui_settings_update(lv_tick_get());
}

static void roll_hyst_step(int32_t step) {
	app_set_roll_hyst(step_fixed(g_app.roll_hyst_deg, step * ROLL_DEG_STEP, 100.0f,
			APP_ROLL_HYST_MIN_DEG, APP_ROLL_HYST_MAX_DEG));
	ui_settings_update(lv_tick_get());
}

#if UI_MENU_STYLE == UI_MENU_TILES
static void freq_step(int32_t step) {
	int32_t hz = clamp_i((int32_t) g_app.log_freq_hz + step, APP_FREQ_MIN_HZ, APP_FREQ_MAX_HZ);
	if (hz != g_app.log_freq_hz)
		app_set_log_freq((uint16_t) hz);
	ui_settings_update(lv_tick_get());
}
#endif

/*--------------------------------------------------------------------
 * Построение
 *--------------------------------------------------------------------*/
static void page_delete_cb(lv_event_t *e) {
	page_t *pg = lv_event_get_user_data(e);
	if (lv_event_get_current_target(e) != pg->scr)
		return; // подменю уже построено заново
	for (int id = 0; id < IT_COUNT; id++) {
		if (it_page[id] == pg) {
			it_btn[id] = it_icon[id] = it_value[id] = NULL;
			it_page[id] = NULL;
		}
	}
	pg->scr = NULL;
	pg->grp = NULL;
	pg->clock = pg->bat = NULL;
	if (pg->forget != NULL)
		pg->forget();
}

// Строка меню: значок, текст, значение справа. Нажатие -> menu_event_cb,
// у полей (field_cb) — правка энкодером
static lv_obj_t* row_create(page_t *pg, lv_obj_t *parent, int32_t x, int32_t y, int32_t w,
		int32_t h, item_t id, const char *icon, const char *text, ui_step_cb_t field_cb) {
	lv_obj_t *b = ui_button_create(parent, pg->grp, x, y, w, h);
	lv_obj_t *ic = ui_label_create(b, UI_FONT_MID, UI_C_ACCENT);
	lv_label_set_text_static(ic, icon);
	lv_obj_align(ic, LV_ALIGN_LEFT_MID, ROW_ICON_X, 0);

	lv_obj_t *l = lv_label_create(b); // цвет наследуется от кнопки
	lv_obj_set_style_text_font(l, UI_FONT_MID, 0);
	lv_label_set_text_static(l, text);
	lv_obj_align(l, LV_ALIGN_LEFT_MID, ROW_TEXT_X, 0);

	lv_obj_t *v = ui_label_create(b, UI_FONT_SMALL, UI_C_DIM);
	lv_obj_align(v, LV_ALIGN_RIGHT_MID, -ROW_VALUE_R, 0);

	if (id != IT_BACK) {
		it_btn[id] = b;
		it_icon[id] = ic;
		it_value[id] = v;
		it_page[id] = pg;
	}
	if (field_cb != NULL)
		ui_field_attach(b, field_cb);
	else
		lv_obj_add_event_cb(b, menu_event_cb, LV_EVENT_SHORT_CLICKED, (void*) (uintptr_t) id);
	return b;
}

#if UI_MENU_STYLE == UI_MENU_LIST
// Фокус на строке — прокрутить список так, чтобы она была видна целиком
static void list_focus_cb(lv_event_t *e) {
	lv_obj_scroll_to_view(lv_event_get_current_target(e), LV_ANIM_OFF);
}

static void list_row(int idx, item_t id, const char *icon, const char *text,
		ui_step_cb_t field_cb) {
	lv_obj_t *b = row_create(&pg_root, menu_list, LIST_ROW_X, idx * LIST_STEP, LIST_ROW_W,
			LIST_ROW_H, id, icon, text, field_cb);
	lv_obj_add_event_cb(b, list_focus_cb, LV_EVENT_FOCUSED, NULL);
}

static void menu_create(void) {
	pg_root.icon = LV_SYMBOL_SETTINGS;
	pg_root.title = "Настройки";
	page_create(&pg_root);
	menu_list = ui_scroll_create(pg_root.scr, 0, UI_BAR_H, UI_W, UI_H - UI_BAR_H);
	lv_obj_set_style_pad_ver(menu_list, 3, 0);

	int n = 0;
	list_row(n++, IT_TIME, LV_SYMBOL_BELL, "Дата и время", NULL);
	list_row(n++, IT_SD, LV_SYMBOL_SD_CARD, "Карта памяти", NULL);
	list_row(n++, IT_ALPHA, UI_ALPHA, "Фильтр " UI_ALPHA, alpha_step);
	list_row(n++, IT_RWIN, ICON_ROLL, "Окно качки", roll_win_step);
	list_row(n++, IT_RRATE, ICON_ROLL, "Отсчёты качки", roll_rate_step);
	list_row(n++, IT_RCALM, ICON_ROLL, "Порог покоя", roll_calm_step);
	list_row(n++, IT_RHYST, ICON_ROLL, "Гистерезис", roll_hyst_step);
	list_row(n++, IT_ADDR, LV_SYMBOL_EDIT, "Адрес датчика", NULL);
	list_row(n++, IT_ZERO, LV_SYMBOL_REFRESH, "Сбросить ноль", NULL);
	list_row(n++, IT_GAP, LV_SYMBOL_PAUSE, "Пауза шины", NULL);
	list_row(n++, IT_BAT, LV_SYMBOL_BATTERY_1, "Порог АКБ", bat_alarm_step);
	list_row(n++, IT_THEME, LV_SYMBOL_TINT, "Тема", NULL);
	list_row(n++, IT_HELP, "?", "Справка", NULL);
	list_row(n++, IT_BACK, LV_SYMBOL_LEFT, "Назад", NULL);
	lv_label_set_text_static(it_value[IT_ADDR], "Modbus");
}

#else /* UI_MENU_TILES */

// Плитка раздела: значок и название сверху, краткое состояние снизу
static void tile_create(int col, int row, item_t id, const char *icon, const char *title) {
	lv_obj_t *b = ui_button_create(pg_root.scr, pg_root.grp,
			TILE_GAP + col * (TILE_W + TILE_GAP),
			UI_BAR_H + TILE_GAP + row * (TILE_H + TILE_GAP), TILE_W, TILE_H);
	lv_obj_t *ic = ui_label_create(b, UI_FONT_MID, UI_C_ACCENT);
	lv_label_set_text_static(ic, icon);
	lv_obj_align(ic, LV_ALIGN_TOP_LEFT, 8, 7);

	lv_obj_t *l = lv_label_create(b); // цвет наследуется от кнопки
	lv_obj_set_style_text_font(l, UI_FONT_MID, 0);
	lv_label_set_text_static(l, title);
	lv_obj_align(l, LV_ALIGN_TOP_LEFT, 36, 7);

	lv_obj_t *v = ui_label_create(b, UI_FONT_SMALL, UI_C_DIM);
	lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
	lv_obj_set_width(v, TILE_W - 2 * UI_BTN_BORDER - 16);
	lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, 8, -7);

	it_btn[id] = b;
	it_icon[id] = ic;
	it_value[id] = v;
	it_page[id] = &pg_root;
	lv_obj_add_event_cb(b, menu_event_cb, LV_EVENT_SHORT_CLICKED, (void*) (uintptr_t) id);
}

static void sub_row(page_t *pg, int idx, item_t id, const char *icon, const char *text,
		ui_step_cb_t field_cb) {
	row_create(pg, pg->scr, 6, SUB_Y0 + idx * SUB_STEP, UI_W - 12, SUB_ROW_H, id, icon, text,
			field_cb);
}

// «Питание»: строки без рамки — подпись и значение шрифтом 20
static lv_obj_t *power_volts, *power_pct;

static lv_obj_t* info_row(page_t *pg, int idx, const char *caption) {
	int32_t y = SUB_Y0 + idx * SUB_STEP + (SUB_ROW_H - lv_font_get_line_height(UI_FONT_MID)) / 2;
	lv_obj_t *c = ui_label_create(pg->scr, UI_FONT_MID, UI_C_DIM);
	lv_label_set_text_static(c, caption);
	lv_obj_set_pos(c, 6 + ROW_TEXT_X, y);
	lv_obj_t *v = ui_label_create(pg->scr, UI_FONT_MID, UI_C_TEXT);
	lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -(6 + ROW_VALUE_R), y);
	return v;
}

static void build_sens(page_t *pg) {
	sub_row(pg, 0, IT_FREQ, LV_SYMBOL_LOOP, "Частота", freq_step);
	sub_row(pg, 1, IT_ALPHA, UI_ALPHA, "Фильтр " UI_ALPHA, alpha_step);
	sub_row(pg, 2, IT_GAP, LV_SYMBOL_PAUSE, "Пауза шины", NULL);
	sub_row(pg, 3, IT_ADDR, LV_SYMBOL_EDIT, "Адрес датчика", NULL);
	sub_row(pg, 4, IT_ZERO, LV_SYMBOL_REFRESH, "Сбросить ноль", NULL);
	sub_row(pg, 5, IT_BACK, LV_SYMBOL_LEFT, "Назад", NULL);
	lv_label_set_text_static(it_value[IT_ADDR], "Modbus");
}

static void build_roll(page_t *pg) {
	sub_row(pg, 0, IT_RWIN, ICON_ROLL, "Окно", roll_win_step);
	sub_row(pg, 1, IT_RRATE, ICON_ROLL, "Отсчёты", roll_rate_step);
	sub_row(pg, 2, IT_RCALM, ICON_ROLL, "Порог покоя", roll_calm_step);
	sub_row(pg, 3, IT_RHYST, ICON_ROLL, "Гистерезис", roll_hyst_step);
	sub_row(pg, 4, IT_BACK, LV_SYMBOL_LEFT, "Назад", NULL);
}

static void forget_power(void) {
	power_volts = power_pct = NULL;
}

static void build_power(page_t *pg) {
	power_volts = info_row(pg, 0, "Напряжение");
	power_pct = info_row(pg, 1, "Заряд");
	sub_row(pg, 2, IT_BAT, LV_SYMBOL_BATTERY_1, "Порог АКБ", bat_alarm_step);
	sub_row(pg, 3, IT_BACK, LV_SYMBOL_LEFT, "Назад", NULL);
}

static void build_dev(page_t *pg) {
	sub_row(pg, 0, IT_TIME, LV_SYMBOL_BELL, "Дата и время", NULL);
	sub_row(pg, 1, IT_THEME, LV_SYMBOL_TINT, "Тема", NULL);
	sub_row(pg, 2, IT_HELP, "?", "Справка", NULL);
	sub_row(pg, 3, IT_BACK, LV_SYMBOL_LEFT, "Назад", NULL);
	lv_label_set_text_static(it_value[IT_HELP], "v" FW_VERSION);
}

static page_t pg_sens = { .icon = LV_SYMBOL_EYE_OPEN, .title = "Датчики", .build = build_sens };
static page_t pg_roll = { .icon = ICON_ROLL, .title = "Качка", .build = build_roll };
static page_t pg_power = { .icon = LV_SYMBOL_BATTERY_1, .title = "Питание",
		.build = build_power, .forget = forget_power };
static page_t pg_dev = { .icon = LV_SYMBOL_SETTINGS, .title = "Прибор", .build = build_dev };

static void menu_create(void) {
	pg_root.icon = LV_SYMBOL_SETTINGS;
	pg_root.title = "Настройки";
	page_create(&pg_root);
	tile_create(0, 0, IT_PG_SENS, LV_SYMBOL_EYE_OPEN, "Датчики");
	tile_create(1, 0, IT_PG_ROLL, ICON_ROLL, "Качка");
	tile_create(0, 1, IT_PG_POWER, LV_SYMBOL_BATTERY_1, "Питание");
	tile_create(1, 1, IT_SD, LV_SYMBOL_SD_CARD, "SD-карта");
	tile_create(0, 2, IT_PG_DEV, LV_SYMBOL_SETTINGS, "Прибор");
	tile_create(1, 2, IT_BACK, LV_SYMBOL_LEFT, "Назад");
	lv_label_set_text_static(it_value[IT_BACK], "главный экран");
	it_btn[IT_BACK] = it_icon[IT_BACK] = it_value[IT_BACK] = NULL; // «Назад» не храним
	it_page[IT_BACK] = NULL;
}
#endif

/*--------------------------------------------------------------------
 * Обновление значений (~10 Гц, пока открыта страница меню)
 *--------------------------------------------------------------------*/
static bool is_page(lv_obj_t *scr) {
	return ui_screen_alive(scr) && pg_cur->scr == scr;
}

static void set_value(item_t id, const char *text) {
	if (it_value[id] != NULL)
		ui_set_text(it_value[id], text);
}

static void set_value_color(item_t id, ui_col_t color) {
	if (it_value[id] != NULL)
		ui_set_text_color(it_value[id], color);
}

// Значение поля: при правке — цветом правки, как текст пункта
static void field_value(item_t id, const char *text) {
	if (it_value[id] == NULL)
		return;
	ui_set_text(it_value[id], text);
	bool edit = lv_group_get_editing(pg_cur->grp) && lv_group_get_focused(pg_cur->grp) == it_btn[id];
	ui_set_text_color(it_value[id], edit ? UI_C_EDIT_TEXT : UI_C_DIM);
}

// Краткое состояние карты: в строке списка и на плитке
static void sd_summary(char *buf, size_t n) {
	switch (g_app.sd_state) {
	case SD_NO_CARD:
		lv_snprintf(buf, n, "НЕТ КАРТЫ");
		break;
	case SD_ERROR:
		lv_snprintf(buf, n, "СБОЙ E:%02u", g_app.sd_err);
		break;
	case SD_RECORDING:
		lv_snprintf(buf, n, "запись M%03u", g_app.file_number);
		break;
	case SD_READY:
	default:
		if (g_app.file_number == 0)
			lv_snprintf(buf, n, "M" UI_DASH);
		else
			lv_snprintf(buf, n, "M%03u", g_app.file_number);
		break;
	}
}

static void menu_update(void) {
	// Кириллица — 2 байта на букву: «НЕТ КАРТЫ» — 17 байт
	char buf[48], num[32];
	const app_time_t *t = &g_app.time;
	page_t *pg = pg_cur;

	lv_snprintf(buf, sizeof(buf), "%02u:%02u:%02u", t->hours, t->minutes, t->seconds);
	ui_set_text(pg->clock, buf);

	// Примерный заряд АКБ: неяркий, при тревоге красный; от USB — «USB»
	bool usb = !g_app.battery_present || g_app.battery_pct > 100U;
	if (usb) {
		ui_set_text(pg->bat, "USB");
		ui_set_text_color(pg->bat, UI_C_DIM);
	} else {
		lv_snprintf(buf, sizeof(buf), "АКБ %u%%", (unsigned) g_app.battery_pct);
		ui_set_text(pg->bat, buf);
		ui_set_text_color(pg->bat, g_app.battery_low ? UI_C_RED : UI_C_DIM);
	}

	// Без DS3231 время после выключения пропадёт — вместо даты предупреждение
	ui_col_t time_col = g_app.rtc_present ? UI_C_DIM : UI_C_ORANGE;
	if (g_app.rtc_present)
		lv_snprintf(buf, sizeof(buf), "%02u.%02u.%02u %02u:%02u", t->date, t->month, t->year,
				t->hours, t->minutes);
	else
		lv_snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " нет часов");
	set_value(IT_TIME, buf);
	set_value_color(IT_TIME, time_col);
	set_value(IT_PG_DEV, buf);
	set_value_color(IT_PG_DEV, time_col);
	if (it_icon[IT_TIME] != NULL)
		ui_set_text_color(it_icon[IT_TIME], g_app.rtc_present ? UI_C_ACCENT : UI_C_ORANGE);

	// Карта памяти: ошибка и «нет карты» — красным; на плитке ещё и место
	sd_summary(num, sizeof(num));
	ui_col_t sd_col = (g_app.sd_state == SD_NO_CARD || g_app.sd_state == SD_ERROR) ? UI_C_RED :
			(g_app.sd_state == SD_RECORDING ? UI_C_TEXT : UI_C_DIM);
#if UI_MENU_STYLE == UI_MENU_TILES
	if (g_app.sd_state == SD_READY && g_app.sd_free_mb != APP_SD_FREE_UNKNOWN) {
		char sp[12];
		if (g_app.sd_free_mb >= 1024U)
			ui_fmt_fixed(sp, sizeof(sp), (float) g_app.sd_free_mb / 1024.0f, 1, " ГБ");
		else
			lv_snprintf(sp, sizeof(sp), "%lu МБ", (unsigned long) g_app.sd_free_mb);
		lv_snprintf(buf, sizeof(buf), "%s " UI_MIDDOT " %s", num, sp);
		set_value(IT_SD, buf);
	} else {
		set_value(IT_SD, num);
	}
#else
	set_value(IT_SD, num);
#endif
	set_value_color(IT_SD, sd_col);

	// Поля
	ui_fmt_fixed(num, sizeof(num), g_app.ema_alpha, 2, "");
	field_value(IT_ALPHA, num);
	lv_snprintf(buf, sizeof(buf), "%u Гц", g_app.log_freq_hz);
	field_value(IT_FREQ, buf);
	lv_snprintf(buf, sizeof(buf), "%u Гц " UI_MIDDOT " " UI_ALPHA " %s", g_app.log_freq_hz, num);
	set_value(IT_PG_SENS, buf);
	ui_fmt_fixed(buf, sizeof(buf), g_app.bat_alarm_v, 1, " В");
	field_value(IT_BAT, buf);

	// Качка
	lv_snprintf(buf, sizeof(buf), "%u с", (unsigned) g_app.roll_window_s);
	field_value(IT_RWIN, buf);
	lv_snprintf(buf, sizeof(buf), "%u Гц", (unsigned) g_app.roll_rate_hz);
	field_value(IT_RRATE, buf);
	ui_fmt_fixed(num, sizeof(num), g_app.roll_calm_deg, 2, UI_DEG);
	field_value(IT_RCALM, num);
	lv_snprintf(buf, sizeof(buf), "%u с " UI_MIDDOT " %u Гц " UI_MIDDOT " %s",
			(unsigned) g_app.roll_window_s, (unsigned) g_app.roll_rate_hz, num);
	set_value(IT_PG_ROLL, buf);
	ui_fmt_fixed(num, sizeof(num), g_app.roll_hyst_deg, 2, UI_DEG);
	field_value(IT_RHYST, num);

	bool zero_set = false;
	for (int i = 0; i < APP_SENSOR_COUNT; i++) {
		if (g_app.sensor[i].off_x != 0.0f || g_app.sensor[i].off_y != 0.0f)
			zero_set = true;
	}
	set_value(IT_ZERO, zero_set ? "задан" : "не задан");
	lv_snprintf(buf, sizeof(buf), "%u мс", g_app.bus_gap_ms);
	set_value(IT_GAP, buf);
	set_value(IT_THEME, g_app.theme == APP_THEME_LIGHT ? "светлая" : "тёмная");
#if UI_MENU_STYLE == UI_MENU_LIST
	set_value(IT_HELP, "v" FW_VERSION);
#endif

#if UI_MENU_STYLE == UI_MENU_TILES
	// Питание: напряжение и заряд (на плитке — вместе)
	ui_col_t pw_col = (!usb && g_app.battery_low) ? UI_C_RED : UI_C_TEXT;
	if (usb) {
		set_value(IT_PG_POWER, "от USB");
	} else {
		ui_fmt_fixed(num, sizeof(num), g_app.battery_v, 1, " В");
		lv_snprintf(buf, sizeof(buf), "%s " UI_MIDDOT " %u%%", num, (unsigned) g_app.battery_pct);
		set_value(IT_PG_POWER, buf);
	}
	set_value_color(IT_PG_POWER, pw_col == UI_C_RED ? UI_C_RED : UI_C_DIM);
	if (power_volts != NULL) {
		if (usb) {
			ui_set_text(power_volts, "USB");
			ui_set_text(power_pct, UI_DASH);
		} else {
			ui_fmt_fixed(num, sizeof(num), g_app.battery_v, 1, " В");
			ui_set_text(power_volts, num);
			lv_snprintf(buf, sizeof(buf), "%u %%", (unsigned) g_app.battery_pct);
			ui_set_text(power_pct, buf);
		}
		ui_set_text_color(power_volts, pw_col);
		ui_set_text_color(power_pct, pw_col);
	}
#endif
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
	ui_menu_back(); // новое время видно в пункте «Дата и время»
}

static void time_cancel_cb(lv_event_t *e) {
	(void) e;
	ui_menu_back();
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

// Экран удалён (ушли с него) — указатели больше не действительны
static void time_delete_cb(lv_event_t *e) {
	if (lv_event_get_current_target(e) != time_scr)
		return; // уже построен новый экран
	time_scr = NULL;
	time_grp = NULL;
	time_rtc = NULL;
	for (int i = 0; i < TF_COUNT; i++)
		time_val[i] = NULL;
}

static void time_create(void) {
	time_grp = lv_group_create();
	time_scr = ui_screen_create_temp(time_grp);
	lv_obj_add_event_cb(time_scr, time_delete_cb, LV_EVENT_DELETE, NULL);
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

	ui_action_button_create(time_scr, time_grp, 8, 190, 148, 44, LV_SYMBOL_OK "  OK",
			time_ok_cb);
	ui_action_button_create(time_scr, time_grp, UI_W - 8 - 148, 190, 148, 44,
			LV_SYMBOL_CLOSE "  Отмена", time_cancel_cb);
}

static void time_show(void) {
	if (!ui_screen_alive(time_scr))
		time_create();
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
	ui_menu_back();
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

static void addr_delete_cb(lv_event_t *e) {
	if (lv_event_get_current_target(e) != addr_scr)
		return;
	addr_scr = NULL;
	addr_grp = NULL;
	addr_old_val = addr_new_val = addr_status = NULL;
}

static void addr_create(void) {
	addr_grp = lv_group_create();
	addr_scr = ui_screen_create_temp(addr_grp);
	lv_obj_add_event_cb(addr_scr, addr_delete_cb, LV_EVENT_DELETE, NULL);
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

	ui_action_button_create(addr_scr, addr_grp, 8, AF_Y_BTN, 148, 42,
			LV_SYMBOL_SAVE "  Записать", addr_write_cb);
	ui_action_button_create(addr_scr, addr_grp, UI_W - 8 - 148, AF_Y_BTN, 148, 42,
			LV_SYMBOL_LEFT "  Назад", addr_back_cb);
}

static void addr_show(void) {
	if (!ui_screen_alive(addr_scr))
		addr_create();
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
	menu_create(); // остальные экраны — временные, строятся при входе
}

void ui_settings_update(uint32_t now) {
	(void) now;
	lv_obj_t *act = lv_screen_active();
	if (is_page(act))
		menu_update();
	else if (act == addr_scr && ui_screen_alive(addr_scr))
		addr_update();
	else
		ui_sd_update(); // сам проверяет, открыт ли экран карты
}
