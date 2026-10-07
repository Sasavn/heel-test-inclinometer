/*
 * ui_help.c — экран «Справка»: кратко о функциях прошивки этой версии.
 *
 * Разделы: заголовок (шрифт 20) и текст (шрифт 14) друг под другом в
 * прокручиваемой области. Область — единственный элемент группы энкодера и
 * сразу в режиме правки: вращение прокручивает её (LVGL: клавиши
 * влево/вправо у прокручиваемого объекта — на четверть высоты), нажатие —
 * назад в меню. Экран временный: строится при входе, удаляется при уходе.
 *
 * Строки текста — между метками FONT_SMALL_ONLY: генератор шрифтов
 * (tools/ui_sim/fonts/gen_fonts.py) берёт их символы только в шрифт 14.
 */
#include "ui_internal.h"
#include "version.h"

#define HELP_PAD_L      10
#define HELP_PAD_R      14      // справа — полоса прокрутки
#define HELP_HEAD_GAP   1       // заголовок -> текст
#define HELP_PARA_GAP   8       // текст -> следующий заголовок

static lv_obj_t *help_scr, *help_area;
static lv_group_t *help_grp;

static const char *const help_head[] = {
	"Прибор",
	"Датчики Д2 и Д3",
	"Качка",
	"Управление",
	"Строка состояния",
	"Запись на карту",
	"Настройки",
	"Тревога АКБ",
	"USB",
};

/* FONT_SMALL_ONLY_BEGIN */
static const char *const help_body[] = {
	// Прибор (первая строка — версия, см. ui_help_create)
	"Инклинометр для кренования: два датчика наклона BWM427 на одной шине "
	"RS485 (Modbus RTU). Углы X и Y в градусах — на экране и в файлах на "
	"SD-карте.",
	// Датчики
	"Modbus-адреса 2 и 3. В шапке карточки — связь: OK, нет связи (последние "
	"значения серым), не подключен, битые ответы (два датчика с одним "
	"адресом?). Точка " UI_DOT " — датчик пишется на карту.",
	// Качка
	"Считается по каждой оси каждого датчика: размах угла (максимум минус "
	"минимум) за окно, по умолчанию 20 с. Справа от угла: «покой» — размах "
	"меньше порога (1.5°; уходит при пороге плюс гистерезис 0.2°), «качка "
	"1.23°» — больше, «сбор 12 с» — окно ещё не набрано. Внизу: «ГОТОВ» — в "
	"покое все оси, иначе худшая ось, например «КАЧКА 2.34° Д3 X». Окно, "
	"отсчёты в секунду, порог покоя и гистерезис меняются в меню.",
	// Управление
	"Энкодер: вращение — выбор, нажатие — действие. На поле нажатие включает "
	"правку (жёлтая рамка), вращение меняет значение, повторное нажатие — "
	"готово. Частота — опрос и запись, 1–50 Гц. Ноль: нажатие — задать по "
	"текущим углам, удержание 1 с — сбросить. Тумблер: REC — запись, STOP — "
	"стоп.",
	// Строка состояния
	"Дата, время; запись: ЗАП (мигает), СТОП, СБОЙ (ошибка карты — вернуть "
	"тумблер в STOP), НЕТ SD; ЦП — загрузка процессора; напряжение питания "
	"(USB — питание от USB).",
	// Запись
	"Каждый датчик — в свой файл ГГГГ-ММ-ДД_MNNN_Dk.CSV: дата начала, "
	"сквозной номер замера, адрес датчика. Колонки: время, углы до нуля, "
	"ноль, углы с нулём, напряжение, мс от начала. На карту — раз в секунду.",
	// Настройки
	"Карта памяти — состояние, номер замера, файлы, место. Фильтр " UI_ALPHA
	" — сглаживание (меньше — плавнее). Качка — окно, отсчёты, порог покоя, "
	"гистерезис. Адрес датчика — смена адреса (на шине только он). Пауза "
	"шины — тишина перед запросом. Порог АКБ, тема, дата и время, сброс нуля. "
	"Настройки сохраняются и после выключения.",
	// Тревога АКБ
	"Ниже порога (по умолчанию 10.0 В) напряжение в строке состояния мигает, "
	"внизу — «" LV_SYMBOL_WARNING " АКБ 9.6 В < 10.0 В». От USB тревоги нет. "
	"Заряд 3S: 9.9 В — 0%, 12.6 В — 100%.",
	// USB
	"Виртуальный COM-порт: ver — версия, diag — состояние, stream N — строка "
	"каждые N мс, set freq | alpha | gap | theme | batalarm — настройки, "
	"boot — загрузчик DFU, reset, help.",
};
/* FONT_SMALL_ONLY_END */

#define HELP_SECTIONS   (sizeof(help_head) / sizeof(help_head[0]))

static char help_version[48];

static void help_click_cb(lv_event_t *e) {
	(void) e;
	ui_menu_back();
}

// Экран удалён (ушли с него) — указатели больше не действительны
static void help_delete_cb(lv_event_t *e) {
	if (lv_event_get_current_target(e) != help_scr)
		return; // уже построен новый экран
	help_scr = NULL;
	help_area = NULL;
	help_grp = NULL;
}

// Метка шириной w в области справки на высоте y; возвращает её высоту
static int32_t help_label(const char *text, const lv_font_t *font, ui_col_t color, int32_t y,
		int32_t w) {
	lv_obj_t *l = ui_label_create(help_area, font, color);
	lv_label_set_text_static(l, text);
	lv_obj_set_width(l, w);
	lv_obj_set_pos(l, 0, y);
	lv_point_t sz;
	lv_text_get_size(&sz, text, font, 0, 0, w, LV_TEXT_FLAG_NONE);
	return sz.y;
}

static void help_create(void) {
	help_grp = lv_group_create();
	help_scr = ui_screen_create_temp(help_grp);
	lv_obj_add_event_cb(help_scr, help_delete_cb, LV_EVENT_DELETE, NULL);
	lv_obj_t *bar = ui_title_create(help_scr, "?", "Справка");
	lv_obj_t *hint = ui_label_create(bar, UI_FONT_SMALL, UI_C_DIM);
	lv_label_set_text_static(hint, "нажатие " UI_DASH " назад");
	lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -10, 0);

	help_area = ui_scroll_create(help_scr, 0, UI_BAR_H, UI_W, UI_H - UI_BAR_H);
	lv_obj_set_style_pad_left(help_area, HELP_PAD_L, 0);
	lv_obj_set_style_pad_right(help_area, HELP_PAD_R, 0);
	lv_obj_set_style_pad_top(help_area, 6, 0);
	lv_obj_set_style_pad_bottom(help_area, 10, 0);
	lv_obj_set_clickable(help_area, true);
	lv_group_add_obj(help_grp, help_area);
	lv_obj_add_event_cb(help_area, help_click_cb, LV_EVENT_SHORT_CLICKED, NULL);

	// Версия и дата сборки (из __DATE__, без названий месяцев — только цифры)
	app_time_t b;
	if (app_time_from_build(__DATE__, __TIME__, &b))
		lv_snprintf(help_version, sizeof(help_version), "Версия " FW_VERSION
				", сборка %02u.%02u.20%02u", b.date, b.month, b.year);
	else
		lv_snprintf(help_version, sizeof(help_version), "Версия " FW_VERSION);

	int32_t w = UI_W - HELP_PAD_L - HELP_PAD_R;
	int32_t y = 0;
	for (size_t i = 0; i < HELP_SECTIONS; i++) {
		y += help_label(help_head[i], UI_FONT_MID, UI_C_ACCENT, y, w) + HELP_HEAD_GAP;
		if (i == 0)
			y += help_label(help_version, UI_FONT_SMALL, UI_C_DIM, y, w);
		y += help_label(help_body[i], UI_FONT_SMALL, UI_C_TEXT, y, w) + HELP_PARA_GAP;
	}
}

void ui_help_show(void) {
	if (!ui_screen_alive(help_scr))
		help_create();
	ui_show(help_scr, help_grp);
	// Сразу в режиме правки: вращение энкодера прокручивает текст
	lv_group_set_editing(help_grp, true);
}
