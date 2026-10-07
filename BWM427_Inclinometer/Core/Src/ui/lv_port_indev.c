/*
 * lv_port_indev.c — энкодер LVGL: TIM3 в режиме энкодера + кнопка PA0.
 *
 * TIM3 считает 4 импульса на щелчок. Шаги = разность счётчика / 4, остаток
 * не теряется (last += steps * 4), переполнение int16 учитывается.
 * Кнопка энкодера PA0 активна низким уровнем (подтяжка к питанию), просто
 * читается при каждом опросе LVGL (раз в LV_DEF_REFR_PERIOD мс) — этого
 * достаточно против дребезга.
 */
#include "lv_port.h"
#include "main.h"

// 1 — поменять направление вращения (если энкодер распаян наоборот).
// При 0: счётчик TIM3 растёт -> значения увеличиваются / фокус вперёд.
#define UI_ENC_INVERT       0

#define UI_ENC_COUNTS_STEP  4      // импульсов TIM3 на один щелчок

// Время удержания кнопки для «долгого нажатия» (сброс нуля), мс
#define UI_LONG_PRESS_MS    1000

// Кнопка считается переключившейся, если уровень держится столько мс подряд
#define UI_BTN_DEBOUNCE_MS  15

extern TIM_HandleTypeDef htim3;

static int16_t enc_last;

// Кнопка: состояние без дребезга и счётчик нажатий — ведутся в SysTick
static volatile uint8_t btn_down;     // 1 = нажата (устойчиво)
static volatile uint8_t btn_presses;  // всего устойчивых нажатий (с переполнением)
static uint8_t btn_presses_seen;      // сколько из них уже отдано LVGL
static bool btn_reported;             // что отдали LVGL в прошлый опрос

// Раз в 1 мс (SysTick): антидребезг и подсчёт нажатий. Короткое нажатие,
// уложившееся между двумя опросами LVGL, не теряется — его отдаст enc_read.
void ui_input_tick_1ms(void) {
	static uint8_t stable_ms;
	uint8_t raw = (HAL_GPIO_ReadPin(ENCODER_SW_GPIO_Port, ENCODER_SW_Pin)
			== GPIO_PIN_RESET) ? 1U : 0U;
	if (raw == btn_down) {
		stable_ms = 0;
		return;
	}
	if (++stable_ms >= UI_BTN_DEBOUNCE_MS) {
		stable_ms = 0;
		btn_down = raw;
		if (raw) {
			btn_presses++;
		}
	}
}

static void enc_read(lv_indev_t *indev, lv_indev_data_t *data) {
	(void) indev;

	int16_t cnt = (int16_t) __HAL_TIM_GET_COUNTER(&htim3);
	int16_t diff = (int16_t) (cnt - enc_last); // переполнение 16 бит — корректно
	int16_t steps = diff / UI_ENC_COUNTS_STEP;  // к нулю: остаток копится дальше
	enc_last = (int16_t) (enc_last + steps * UI_ENC_COUNTS_STEP);

#if UI_ENC_INVERT
	steps = (int16_t) -steps;
#endif
	data->enc_diff = steps;

	// Есть не отданное LVGL нажатие — показать «нажато» хотя бы на один опрос
	// (между двумя такими нажатиями — один опрос «отпущено»). Иначе — текущее
	// состояние: так работает и долгое удержание.
	bool pressed;
	uint8_t presses = btn_presses;
	if (presses != btn_presses_seen) {
		if (btn_reported) {
			pressed = false;
		} else {
			pressed = true;
			btn_presses_seen++;
		}
	} else {
		pressed = (btn_down != 0);
	}
	btn_reported = pressed;
	data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
	data->key = LV_KEY_ENTER;
}

lv_indev_t* lv_port_indev_init(void) {
	// main.c уже запускает энкодер; повторный старт вернёт HAL_ERROR — безвредно,
	// зато интерфейс работает и без этого вызова в main.c
	HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);
	enc_last = (int16_t) __HAL_TIM_GET_COUNTER(&htim3);

	lv_indev_t *indev = lv_indev_create();
	lv_indev_set_type(indev, LV_INDEV_TYPE_ENCODER);
	lv_indev_set_read_cb(indev, enc_read);
	lv_indev_set_long_press_time(indev, UI_LONG_PRESS_MS);
	return indev;
}
