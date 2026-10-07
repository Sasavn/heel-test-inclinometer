/*
 * ui_internal.h — общее для файлов интерфейса (ui.c, ui_main.c, ui_settings.c).
 */
#ifndef UI_INTERNAL_H_
#define UI_INTERNAL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"
#include "app.h"

// --- Экран ---
#define UI_W                320
#define UI_H                240

// --- Обновление виджетов из g_app ---
#define UI_UPDATE_MS        100     // ~10 Гц
#define UI_BLINK_MS         500     // мигание индикатора записи
#define UI_TOAST_MS         2000    // сколько висит сообщение («Ноль задан»)

// 1 — в строке под датчиками показывать диагностику суперцикла
// (проходов в секунду и самый долгий проход, мс) вместо подсказок.
// Обычно 0: суперцикл быстрый, а диагностика есть и по USB.
#define UI_DIAG             0

// Сколько мс после битого ответа датчика показывать «дубль адреса?»
#define UI_GARBLED_HINT_MS  3000

// --- Общая геометрия ---
#define UI_BAR_H            28      // заголовок экранов меню
#define UI_RADIUS_CARD      10      // карточки датчиков
#define UI_RADIUS_BTN       8       // кнопки и пункты меню
#define UI_BTN_BORDER       2       // рамка кнопки (всегда 2 px: меняется только цвет)

/*
 * Цвета задаются ролями, а не значениями: настоящий цвет роли берётся из
 * таблицы текущей темы (ui.c). Объекты, покрашенные через ui_set_*_color(),
 * запоминают роль и перекрашиваются при смене темы без пересоздания экранов.
 */
typedef enum {
	UI_C_BG = 0,     // фон экрана
	UI_C_BAR,        // строка состояния, заголовки
	UI_C_CARD,       // карточки и кнопки
	UI_C_CARD_PR,    // кнопка нажата
	UI_C_BORDER,     // рамка карточки/кнопки
	UI_C_LINE,       // разделители
	UI_C_TEXT,       // основной текст
	UI_C_DIM,        // подписи
	UI_C_GREY,       // устаревшие значения, «нет данных»
	UI_C_ACCENT,     // значки, номер замера
	UI_C_FOCUS,      // рамка фокуса
	UI_C_FOCUS_BG,   // фон кнопки в фокусе
	UI_C_EDIT,       // рамка в режиме правки
	UI_C_EDIT_BG,    // фон в режиме правки
	UI_C_EDIT_TEXT,  // текст в режиме правки
	UI_C_RED,
	UI_C_RED_BG,     // подложка красной плашки
	UI_C_GREEN,
	UI_C_GREEN_BG,
	UI_C_ORANGE,
	UI_C_ORANGE_BG,
	UI_C_CHIP_BG,    // подложка нейтральной плашки
	UI_C_REC,        // залитая плашка «ЗАП»
	UI_C_ON_REC,     // текст на ней
	UI_C_COUNT
} ui_col_t;

// --- Шрифты (Core/Src/ui/fonts, генерация: tools/ui_sim/fonts/gen_fonts.py) ---
#define UI_FONT_SMALL       (&ui_font_14)
#define UI_FONT_MID         (&ui_font_20)
#define UI_FONT_NUM         (&ui_font_num)   // Bold 36, только цифры и знаки

// --- Символы (UTF-8) ---
#define UI_MINUS            "\xE2\x88\x92"   // U+2212 «минус» (той же ширины, что '+')
#define UI_DASH             "\xE2\x80\x94"   // U+2014 «—»
#define UI_DOT              "\xE2\x97\x8F"   // U+25CF «●»
#define UI_ALPHA            "\xCE\xB1"       // U+03B1 «α»
#define UI_ELLIPSIS         "\xE2\x80\xA6"   // U+2026 «…»
#define UI_ARROW            "\xE2\x86\x92"   // U+2192 «→»
#define UI_DEG              "\xC2\xB0"       // U+00B0 «°»

// --- Общие объекты ---
extern lv_indev_t *ui_indev;

// --- Тема (ui.c) ---
lv_color_t ui_color(ui_col_t c);
// Применить g_app.theme, если она отличается от текущей (перекрасить все экраны)
void ui_theme_sync(void);

// --- Помощники (ui.c) ---
lv_obj_t* ui_screen_create(void);
// Залитый прямоугольник (полосы, разделители, точки)
lv_obj_t* ui_box_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
		ui_col_t color);
// Карточка: скруглённая подложка с тонкой рамкой
lv_obj_t* ui_card_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);
lv_obj_t* ui_label_create(lv_obj_t *parent, const lv_font_t *font, ui_col_t color);
// Плашка: метка со скруглённой подложкой (цвета — ui_set_chip())
lv_obj_t* ui_chip_create(lv_obj_t *parent, const lv_font_t *font);
// Кнопка с рамкой фокуса/редактирования, добавляется в группу g
lv_obj_t* ui_button_create(lv_obj_t *parent, lv_group_t *g, int32_t x, int32_t y,
		int32_t w, int32_t h);
// Полоса во всю ширину сверху экрана (строка состояния, заголовок)
lv_obj_t* ui_bar_create(lv_obj_t *scr, int32_t h);
// Заголовок экрана (полоса сверху): значок и текст
lv_obj_t* ui_title_create(lv_obj_t *scr, const char *icon, const char *text);
// Поле, редактируемое энкодером: короткое нажатие — вход/выход из
// редактирования, вращение в режиме редактирования вызывает cb(шаг ±1)
typedef void (*ui_step_cb_t)(int32_t step);
void ui_field_attach(lv_obj_t *btn, ui_step_cb_t cb);

// Обновить, только если изменилось (без лишней перерисовки).
// ui_set_text() возвращает true, если текст поменялся
bool ui_set_text(lv_obj_t *label, const char *text);
void ui_set_text_color(lv_obj_t *obj, ui_col_t color);
void ui_set_bg_color(lv_obj_t *obj, ui_col_t color);
void ui_set_border_color(lv_obj_t *obj, ui_col_t color);
void ui_set_chip(lv_obj_t *chip, ui_col_t bg, ui_col_t text);
void ui_set_hidden(lv_obj_t *obj, bool hidden);
void ui_set_y(lv_obj_t *obj, int32_t y);
// Ширина однострочного текста шрифтом font, px
int32_t ui_text_width(const char *text, const lv_font_t *font);

// Перейти на экран scr, энкодер — в группу g (фокус на первом элементе)
void ui_show(lv_obj_t *scr, lv_group_t *g);

// Форматирование без printf с плавающей точкой
void ui_fmt_angle(char *buf, size_t n, float deg);          // "+1.23°", "−0.05°"
void ui_fmt_fixed(char *buf, size_t n, float v, uint8_t decimals, const char *suffix);

// --- Экраны ---
void ui_main_create(void);
void ui_main_show(void);
void ui_main_update(uint32_t now);
void ui_main_toast(const char *text, ui_col_t color);

void ui_settings_create(void);
void ui_menu_show(void);
void ui_settings_update(uint32_t now);

#endif /* UI_INTERNAL_H_ */
