/*
 * app.c — логика прибора: планировщик опроса датчиков, фильтры и статусы,
 * стабильность, часы (DS3231 или программные), батарея, тумблер записи,
 * настройки (хранятся во флеше, settings.c).
 *
 * Всё вызывается из суперцикла (app_task) и не блокирует дольше нескольких
 * миллисекунд, кроме записи на SD и сохранения настроек во флеш (~0,2 мс, а
 * раз в 4096 сохранений — стирание сектора, 1-2 с; во время записи замера
 * не сохраняется). Транзакции Modbus идут в прерываниях
 * (bwm427.c), здесь только решается, кого и когда опрашивать.
 */
#include "app.h"
#include "main.h"
#include "ui.h"
#include "bwm427.h"
#include "sd_logger.h"
#include "settings.h"
#include <string.h>

app_state_t g_app;

extern ADC_HandleTypeDef hadc1;
extern I2C_HandleTypeDef hi2c1;

// --- Настройки по умолчанию (пока во флеше нет записи, см. settings.h) ---
#define APP_DEF_FREQ_HZ      10
#define APP_DEF_ALPHA        0.15f
#define APP_DEF_GAP_MS       BWM427_GAP_DEFAULT_MS
#define APP_DEF_THEME        APP_THEME_DARK
#define APP_DEF_BAT_ALARM_V  APP_BAT_ALARM_DEFAULT_V

// --- Опрос датчиков ---
#define APP_FAIL_LIMIT       3      // OK -> LOST после стольких неудач подряд
#define APP_PROBE_MS         1000   // проба неотвечающего датчика не чаще раза в секунду
#define APP_PROBE_BOOT_MS    200    // ...а первые APP_BOOT_FAST_MS после включения чаще
#define APP_BOOT_FAST_MS     3000   //    (датчику нужно ~50 мс на запуск)
#define APP_RATE_ALPHA       0.2f   // сглаживание фактической частоты циклов
#define APP_RATE_TIMEOUT_MS  2000   // нет циклов дольше — частота 0

// --- Таймаут ответа датчика, который уже отвечает (короче BWM427_TIMEOUT_US) ---
// 2 × наибольшая задержка до первого байта + время кадра ответа + запас, в
// пределах APP_TO_MIN_US..BWM427_TIMEOUT_US. Полный таймаут, пока замеров
// меньше APP_TO_LEARN_N, и после любой неудачи датчика (до следующего ответа).
#define APP_TO_LEARN_N       20
#define APP_TO_MARGIN_US     2000u
#define APP_TO_MIN_US        10000u

// --- Тревога АКБ ---
#define APP_BAT_LOW_READINGS 3      // подряд средних ниже порога (~1 с) — тревога

// --- Качка: размах по каждой оси каждого датчика за последние 20 с ---
// Окно задано по времени, а не числом отсчётов: период бортовой качки судна
// 5–20 с, и окно должно вмещать хотя бы один период при любой частоте опроса.
// В окно идёт не больше 5 отсчётов в секунду (прореживание по времени): при
// опросе 10 Гц — каждый второй, при 1 Гц — каждый. Буфер на 305 отсчётов
// (наибольшее окно 60 с по 5 Гц + запас на дрожание моментов опроса).
// Окно, частота отсчётов, порог и гистерезис — настройки (g_app.roll_*), по
// умолчанию 20 с, 5 Гц, 1,5°, 0,2°. Буфер рассчитан на наибольшее окно.
#define APP_STAB_SAMPLE_TOL_MS 20u  // допуск: при 10 Гц отсчёты идут через 100 ± единицы мс
#define APP_STAB_N           (APP_ROLL_WIN_MAX_S * APP_ROLL_RATE_MAX_HZ + 5u) // 305

// --- Тумблер записи (PA3, 0 = запись) ---
#define APP_SW_DEBOUNCE_MS   50

// --- Батарея: делитель на PA1, VDDA уточняется по VREFINT ---
#define APP_BAT_PERIOD_MS    20     // одно преобразование раз в 20 мс (по очереди батарея / VREFINT)
#define APP_BAT_AVG_N        8      // пар в среднем: новое значение каждые 8 * 40 = 320 мс
#define APP_BAT_DIVIDER      4.03f
#define APP_BAT_ADC_SAT      4090   // отсчёт АЦП батареи у предела 4095: вход выше шкалы
#define APP_VDDA_NOMINAL     3.3f
#ifndef APP_VREFINT_CAL // (host-тесты подставляют своё значение)
#define APP_VREFINT_CAL      (*(const uint16_t*) 0x1FFF7A2AU) // VREFINT при VDDA = 3,3 В (заводская калибровка)
#endif

// --- DS3231 ---
#define DS3231_ADDR          0xD0
#define DS3231_TIMEOUT_MS    5
#define DS3231_REG_TIME      0x00
#define DS3231_REG_STATUS    0x0F
#define DS3231_OSF           0x80   // генератор останавливался — время недостоверно
#define APP_RTC_FAIL_LIMIT   3      // подряд неудачных чтений -> программные часы

// Внутреннее состояние датчика (интерфейсу не нужно)
typedef struct {
	bwm427_filter_t flt;
	uint8_t fail_streak;        // неудачных ответов подряд
	uint32_t last_probe_ms;     // последняя проба, пока датчик не OK
} sensor_priv_t;

// Шаги сервисной команды смены адреса
typedef enum {
	SVC_STEP_NONE = 0,
	SVC_STEP_WRITE,     // 0x000D = new по старому адресу (эхо с old или new)
	SVC_STEP_SAVE_NEW,  // 0x000F = 0 по новому адресу
	SVC_STEP_SAVE_OLD,  // ...нет ответа — по старому
} svc_step_t;

static sensor_priv_t s_priv[APP_SENSOR_COUNT];

// Пакет транзакций для bwm427 (принадлежит драйверу между start и take)
static bwm427_xfer_t s_xfer[BWM427_MAX_XFER];
static uint8_t s_xfer_n;
static bool s_batch_svc;          // текущий пакет — сервисная команда
static bool s_batch_measure;      // в цикле были датчики со статусом OK
static uint16_t s_batch_hz;       // частота, с которой цикл был запланирован

// Расписание циклов: цикл n стартует в s_epoch_ms + n * 1000 / log_freq_hz
static uint32_t s_epoch_ms;
static uint16_t s_epoch_n;
static uint8_t s_probe_rr;        // с какого датчика искать следующую пробу

// Фактическая частота
static bool s_rate_valid;
static uint32_t s_rate_last_ms;
static float s_rate_dt_ms;

// Качка: по кольцевому буферу на датчик — углы X, Y (сотые градуса, int16)
// и время отсчёта: 305 * 8 = 2440 байт на датчик, 4880 байт на два
typedef struct {
	int16_t v[2][APP_STAB_N]; // [0] = X, [1] = Y, сотые градуса
	uint32_t t[APP_STAB_N];
	uint16_t n, i;            // отсчётов в буфере, место следующего
	bool used;                // в буфере есть отсчёты (due_ms действителен)
	uint32_t due_ms;          // когда брать следующий отсчёт в окно
} roll_buf_t;
static roll_buf_t s_roll[APP_SENSOR_COUNT];

// Сервисная команда
static svc_step_t s_svc_step;
static uint8_t s_svc_old, s_svc_new;

// Тумблер
static bool s_sw_raw;
static uint32_t s_sw_ms;

// Часы
static uint32_t s_clock_ms;
static uint8_t s_rtc_fail;

// Батарея
static uint32_t s_bat_ms;
static uint32_t s_bat_sum, s_ref_sum;
static uint8_t s_bat_n;
static bool s_bat_phase_ref;      // идёт преобразование VREFINT
static uint8_t s_bat_low_n;       // средних ниже порога подряд
static uint32_t s_bat_max;        // наибольший отсчёт батареи в текущем среднем

// Интерфейс может отсутствовать (сборка UI=0)
__attribute__((weak)) void ui_init(void) {
}

__attribute__((weak)) void ui_task(void) {
}

__attribute__((weak)) void ui_input_tick_1ms(void) {
}

/* ------------------------------------------------------------------------ */
/* Диагностика суперцикла                                                   */
/* ------------------------------------------------------------------------ */

static uint32_t s_diag_ms;
static uint32_t s_diag_loops;
static uint32_t s_diag_loop_max, s_diag_app_max, s_diag_ui_max; // такты

// Загрузка процессора. Суперцикл крутится без остановки, поэтому «простой» —
// это проходы, в которых делать было нечего: каждая задача только проверила,
// не пора ли ей, и вернулась. Такой холостой проход стоит почти постоянное
// число тактов C, а всё сверх него в любом проходе — работа (в том числе
// прерывания USB, USART, SysTick, TIM5: они удлиняют проход, в котором
// случились). За окно в 1 с из W тактов и P проходов:
//     простой = P × C,  загрузка = 1 − P × C / W   (0..100 %).
// C — самый короткий проход окна (в окне почти всегда есть холостые проходы).
// Окно, где коротких проходов не было (всё время занято: стирание сектора
// флеша, долгая отрисовка), дало бы завышенное C и заниженную загрузку,
// поэтому новое C принимается, только если оно не больше 2 × прежней оценки
// (так она следует за медленным ростом стоимости холостого прохода, например
// при другом экране LVGL); иначе остаётся прежняя. Первая оценка — по первому
// окну после включения (суперцикл тогда почти свободен).
static uint32_t s_cpu_last_cyc;   // DWT->CYCCNT прошлого вызова app_diag_loop
static uint32_t s_cpu_win_cyc;    // начало окна
static uint32_t s_cpu_passes;     // проходов в окне
static uint32_t s_cpu_win_min;    // самый короткий проход окна
static uint32_t s_cpu_idle_cyc;   // оценка холостого прохода, 0 — ещё нет
static bool s_cpu_started;

uint8_t app_cpu_load_calc(uint32_t window_cyc, uint32_t passes, uint32_t win_min_cyc,
		uint32_t *idle_cyc) {
	if (passes == 0 || window_cyc == 0) {
		return 100; // за всё окно ни одного полного прохода — суперцикл стоял
	}
	if (*idle_cyc == 0 || win_min_cyc <= 2u * *idle_cyc) {
		*idle_cyc = win_min_cyc;
	}
	uint64_t idle = (uint64_t) passes * *idle_cyc;
	if (idle >= window_cyc) {
		return 0;
	}
	return (uint8_t) ((((uint64_t) window_cyc - idle) * 100u + window_cyc / 2u)
			/ window_cyc);
}

void app_diag_loop(uint32_t app_cycles, uint32_t ui_cycles) {
	uint32_t cyc = DWT->CYCCNT;
	if (s_cpu_started) {
		uint32_t pass = cyc - s_cpu_last_cyc; // проход целиком, с прерываниями
		if (pass < s_cpu_win_min) {
			s_cpu_win_min = pass;
		}
		s_cpu_passes++;
	} else {
		s_cpu_started = true;
		s_cpu_win_cyc = cyc;
		s_cpu_win_min = UINT32_MAX;
	}
	s_cpu_last_cyc = cyc;

	uint32_t loop = app_cycles + ui_cycles;
	if (loop > s_diag_loop_max) {
		s_diag_loop_max = loop;
	}
	if (app_cycles > s_diag_app_max) {
		s_diag_app_max = app_cycles;
	}
	if (ui_cycles > s_diag_ui_max) {
		s_diag_ui_max = ui_cycles;
	}
	s_diag_loops++;
	uint32_t now = HAL_GetTick();
	if (now - s_diag_ms >= 1000) {
		uint32_t per_ms = SystemCoreClock / 1000U;
		app_diag_t *d = &g_app.diag;
		d->loops_per_s = s_diag_loops;
		d->loop_max_ms = (uint16_t) (s_diag_loop_max / per_ms);
		d->app_max_ms = (uint16_t) (s_diag_app_max / per_ms);
		d->ui_max_ms = (uint16_t) (s_diag_ui_max / per_ms);
		d->uptime_s = now / 1000U;
		d->cpu_load_pct = app_cpu_load_calc(cyc - s_cpu_win_cyc, s_cpu_passes,
				s_cpu_win_min, &s_cpu_idle_cyc);
		d->idle_pass_cyc = s_cpu_idle_cyc;
		d->pass_min_cyc = s_cpu_passes ? s_cpu_win_min : 0u;
		s_cpu_win_cyc = cyc;
		s_cpu_passes = 0;
		s_cpu_win_min = UINT32_MAX;
		s_diag_ms = now;
		s_diag_loops = 0;
		s_diag_loop_max = s_diag_app_max = s_diag_ui_max = 0;
	}
}

/* ------------------------------------------------------------------------ */
/* Статистика шины                                                          */
/* ------------------------------------------------------------------------ */

static void stat_add(app_stat_t *s, uint32_t v) {
	if (s->n == 0 || v < s->min_us) {
		s->min_us = v;
	}
	if (v > s->max_us) {
		s->max_us = v;
	}
	s->last_us = v;
	s->sum_us += v;
	s->n++;
}

uint32_t app_stat_avg_us(const app_stat_t *s) {
	return s->n ? (uint32_t) ((s->sum_us + s->n / 2u) / s->n) : 0u;
}

void app_bus_stats_reset(void) {
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		app_sensor_t *s = &g_app.sensor[i];
		s->timeout_count = 0;
		s->crc_count = 0;
		s->frame_count = 0;
		memset(&s->lat_first, 0, sizeof(s->lat_first));
		memset(&s->lat_done, 0, sizeof(s->lat_done));
	}
	uint32_t fb = g_app.bus.timer_fallbacks;
	memset(&g_app.bus, 0, sizeof(g_app.bus));
	g_app.bus.timer_fallbacks = fb;
}

/* ------------------------------------------------------------------------ */
/* Календарь (чистые функции)                                               */
/* ------------------------------------------------------------------------ */

uint8_t app_days_in_month(uint8_t month, uint8_t year) {
	static const uint8_t dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (month < 1 || month > 12) {
		return 31;
	}
	// 2000..2099: високосный каждый 4-й год (2000 — тоже)
	if (month == 2 && (year % 4) == 0) {
		return 29;
	}
	return dim[month - 1];
}

bool app_time_valid(const app_time_t *t) {
	return t->year <= 99 && t->month >= 1 && t->month <= 12 && t->date >= 1
			&& t->date <= app_days_in_month(t->month, t->year) && t->hours <= 23
			&& t->minutes <= 59 && t->seconds <= 59;
}

void app_time_add_second(app_time_t *t) {
	if (++t->seconds < 60) {
		return;
	}
	t->seconds = 0;
	if (++t->minutes < 60) {
		return;
	}
	t->minutes = 0;
	if (++t->hours < 24) {
		return;
	}
	t->hours = 0;
	if (++t->date <= app_days_in_month(t->month, t->year)) {
		return;
	}
	t->date = 1;
	if (++t->month <= 12) {
		return;
	}
	t->month = 1;
	t->year = (uint8_t) ((t->year + 1) % 100);
}

static uint8_t parse_2digits(const char *s) {
	uint8_t hi = (s[0] >= '0' && s[0] <= '9') ? (uint8_t) (s[0] - '0') : 0;
	return (uint8_t) (hi * 10 + (s[1] - '0'));
}

bool app_time_from_build(const char *date, const char *time, app_time_t *t) {
	static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
	if (strlen(date) != 11 || strlen(time) != 8) {
		return false;
	}
	app_time_t v;
	v.month = 0;
	for (uint8_t m = 0; m < 12; m++) {
		if (strncmp(date, &months[m * 3], 3) == 0) {
			v.month = (uint8_t) (m + 1);
			break;
		}
	}
	v.date = parse_2digits(&date[4]);     // "Oct  6 2026": день с пробелом впереди
	v.year = parse_2digits(&date[9]);     // две последние цифры года
	v.hours = parse_2digits(&time[0]);
	v.minutes = parse_2digits(&time[3]);
	v.seconds = parse_2digits(&time[6]);
	if (!app_time_valid(&v)) {
		return false;
	}
	*t = v;
	return true;
}

/* ------------------------------------------------------------------------ */
/* DS3231                                                                   */
/* ------------------------------------------------------------------------ */

static uint8_t bcd_to_dec(uint8_t v) {
	return (uint8_t) ((v >> 4) * 10 + (v & 0x0F));
}

static uint8_t dec_to_bcd(uint8_t v) {
	return (uint8_t) (((v / 10) << 4) | (v % 10));
}

// День недели 1..7 (понедельник = 1) — для регистра DS3231
static uint8_t weekday(const app_time_t *t) {
	static const uint8_t k[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	uint16_t y = (uint16_t) (2000 + t->year - (t->month < 3));
	uint8_t dow = (uint8_t) ((y + y / 4 - y / 100 + y / 400 + k[t->month - 1]
			+ t->date) % 7); // 0 = воскресенье
	return (uint8_t) (dow == 0 ? 7 : dow);
}

static bool ds3231_read(app_time_t *t) {
	uint8_t r[7];
	if (HAL_I2C_Mem_Read(&hi2c1, DS3231_ADDR, DS3231_REG_TIME,
			I2C_MEMADD_SIZE_8BIT, r, sizeof(r), DS3231_TIMEOUT_MS) != HAL_OK) {
		return false;
	}
	app_time_t v;
	v.seconds = bcd_to_dec(r[0] & 0x7F);
	v.minutes = bcd_to_dec(r[1] & 0x7F);
	if (r[2] & 0x40) { // 12-часовой режим (сами пишем 24-часовой, но мало ли)
		uint8_t h = (uint8_t) (bcd_to_dec(r[2] & 0x1F) % 12);
		v.hours = (r[2] & 0x20) ? (uint8_t) (h + 12) : h;
	} else {
		v.hours = bcd_to_dec(r[2] & 0x3F);
	}
	v.date = bcd_to_dec(r[4] & 0x3F);
	v.month = bcd_to_dec(r[5] & 0x1F);
	v.year = bcd_to_dec(r[6]);
	if (!app_time_valid(&v)) {
		return false;
	}
	*t = v;
	return true;
}

static bool ds3231_write(const app_time_t *t) {
	uint8_t r[7];
	r[0] = dec_to_bcd(t->seconds);
	r[1] = dec_to_bcd(t->minutes);
	r[2] = dec_to_bcd(t->hours); // 24-часовой режим
	r[3] = weekday(t);
	r[4] = dec_to_bcd(t->date);
	r[5] = dec_to_bcd(t->month);
	r[6] = dec_to_bcd(t->year);
	if (HAL_I2C_Mem_Write(&hi2c1, DS3231_ADDR, DS3231_REG_TIME,
			I2C_MEMADD_SIZE_8BIT, r, sizeof(r), DS3231_TIMEOUT_MS) != HAL_OK) {
		return false;
	}
	// Время задано — снять флаг остановки генератора
	uint8_t st;
	if (HAL_I2C_Mem_Read(&hi2c1, DS3231_ADDR, DS3231_REG_STATUS,
			I2C_MEMADD_SIZE_8BIT, &st, 1, DS3231_TIMEOUT_MS) == HAL_OK) {
		st &= (uint8_t) ~DS3231_OSF;
		HAL_I2C_Mem_Write(&hi2c1, DS3231_ADDR, DS3231_REG_STATUS,
				I2C_MEMADD_SIZE_8BIT, &st, 1, DS3231_TIMEOUT_MS);
	}
	return true;
}

/* ------------------------------------------------------------------------ */
/* Часы                                                                     */
/* ------------------------------------------------------------------------ */

static void clock_init(void) {
	app_time_t build;
	if (!app_time_from_build(__DATE__, __TIME__, &build)) {
		build = (app_time_t ) { .year = 26, .month = 1, .date = 1 };
	}
	g_app.time = build;
	g_app.rtc_present = (HAL_I2C_IsDeviceReady(&hi2c1, DS3231_ADDR, 2, 5)
			== HAL_OK);
	if (g_app.rtc_present) {
		uint8_t st = DS3231_OSF;
		app_time_t t;
		HAL_I2C_Mem_Read(&hi2c1, DS3231_ADDR, DS3231_REG_STATUS,
				I2C_MEMADD_SIZE_8BIT, &st, 1, DS3231_TIMEOUT_MS);
		if (!(st & DS3231_OSF) && ds3231_read(&t)) {
			g_app.time = t;
		} else {
			// Время в DS3231 потеряно (села батарейка) — записать время сборки
			ds3231_write(&build);
		}
	}
	s_clock_ms = HAL_GetTick();
}

// Раз в секунду: программный ход, а при наличии DS3231 — его показания
static void clock_task(uint32_t now) {
	if (now - s_clock_ms < 1000) {
		return;
	}
	// += 1000, а не = now: часы не отстают из-за задержек суперцикла
	while (now - s_clock_ms >= 1000) {
		s_clock_ms += 1000;
		app_time_add_second(&g_app.time);
	}
	if (g_app.rtc_present) {
		app_time_t t;
		if (ds3231_read(&t)) {
			g_app.time = t;
			s_rtc_fail = 0;
		} else if (++s_rtc_fail >= APP_RTC_FAIL_LIMIT) {
			g_app.rtc_present = false; // часы пропали — дальше программный ход
		}
	}
}

/* ------------------------------------------------------------------------ */
/* Батарея                                                                  */
/* ------------------------------------------------------------------------ */

static void adc_select(uint32_t channel) {
	ADC_ChannelConfTypeDef c = { 0 };
	c.Channel = channel;
	c.Rank = 1;
	c.SamplingTime = ADC_SAMPLETIME_480CYCLES; // делитель высокоомный — нужна длинная выборка
	HAL_ADC_ConfigChannel(&hadc1, &c);
}

// Заряд 3S Li-ion по напряжению: кусочно-линейная кривая разряда ячейки
// (без нагрузки; под нагрузкой напряжение ниже — оценка грубая)
uint8_t app_bat_pct(float volts) {
	static const struct {
		float v;     // на ячейку, В
		uint8_t pct;
	} curve[] = {
		{ 3.30f, 0 }, { 3.40f, 3 }, { 3.50f, 8 }, { 3.60f, 18 }, { 3.70f, 33 },
		{ 3.80f, 50 }, { 3.90f, 65 }, { 4.00f, 78 }, { 4.10f, 90 }, { 4.20f, 100 },
	};
	const uint8_t n = (uint8_t) (sizeof(curve) / sizeof(curve[0]));
	float cell = volts / (float) APP_BAT_CELLS;
	if (!(cell > curve[0].v)) { // заодно NaN
		return 0;
	}
	for (uint8_t k = 1; k < n; k++) {
		if (cell <= curve[k].v) {
			float f = (cell - curve[k - 1].v) / (curve[k].v - curve[k - 1].v);
			float pct = (float) curve[k - 1].pct
					+ f * (float) (curve[k].pct - curve[k - 1].pct);
			return (uint8_t) (pct + 0.5f);
		}
	}
	return 100;
}

// Наличие АКБ и тревога по новому среднему (каждые ~320 мс). Тревога — после
// APP_BAT_LOW_READINGS средних подряд ниже порога (одиночный провал под
// нагрузкой не в счёт), снимается сразу выше порога + гистерезис или без АКБ.
// Питание только от USB: делитель показывает ~4 В — АКБ нет, тревоги нет.
static void battery_alarm_update(void) {
	float v = g_app.battery_v;
	g_app.battery_present = (v >= APP_BAT_PRESENT_MIN_V);
	g_app.battery_pct = g_app.battery_present ? app_bat_pct(v) : APP_BAT_PCT_NONE;
	if (!g_app.battery_present || v > g_app.bat_alarm_v + APP_BAT_ALARM_HYST_V) {
		s_bat_low_n = 0;
		g_app.battery_low = false;
		return;
	}
	if (v < g_app.bat_alarm_v) {
		if (s_bat_low_n < APP_BAT_LOW_READINGS) {
			s_bat_low_n++;
		}
		if (s_bat_low_n >= APP_BAT_LOW_READINGS) {
			g_app.battery_low = true;
		}
	} else {
		s_bat_low_n = 0; // в полосе гистерезиса: тревога (если была) остаётся
	}
}

// Пересчитать среднее: VDDA = 3,3 * VREFINT_CAL / VREFINT, U = adc / 4095 * VDDA * делитель
static void battery_update(void) {
	float vdda = APP_VDDA_NOMINAL;
	if (s_ref_sum) {
		vdda = APP_VDDA_NOMINAL * (float) APP_VREFINT_CAL * APP_BAT_AVG_N
				/ (float) s_ref_sum;
		if (vdda < 2.4f || vdda > 3.6f) {
			vdda = APP_VDDA_NOMINAL; // не похоже на правду — считать по номиналу
		}
	}
	g_app.battery_v = (float) s_bat_sum / APP_BAT_AVG_N / 4095.0f * vdda
			* APP_BAT_DIVIDER;
	// Вход у предела шкалы (13,3 В при VDDA 3,3 В; полная 3S — 12,6 В): показанное
	// напряжение — оценка снизу
	g_app.battery_adc_sat = (s_bat_max >= APP_BAT_ADC_SAT);
	s_bat_max = 0;
	s_bat_sum = 0;
	s_ref_sum = 0;
	s_bat_n = 0;
	battery_alarm_update();
}

// Одно преобразование с ожиданием (только при старте, ~25 мкс)
static uint32_t adc_read_now(uint32_t channel) {
	adc_select(channel);
	HAL_ADC_Start(&hadc1);
	if (HAL_ADC_PollForConversion(&hadc1, 2) != HAL_OK) {
		return 0;
	}
	return HAL_ADC_GetValue(&hadc1);
}

static void battery_init(void) {
	s_bat_low_n = 0;
	s_bat_sum = 0;
	s_ref_sum = 0;
	s_bat_max = 0;
	for (uint8_t i = 0; i < APP_BAT_AVG_N; i++) {
		uint32_t v = adc_read_now(ADC_CHANNEL_1);
		s_bat_sum += v;
		if (v > s_bat_max) {
			s_bat_max = v;
		}
		s_ref_sum += adc_read_now(ADC_CHANNEL_VREFINT);
	}
	battery_update();
	s_bat_phase_ref = false;
	adc_select(ADC_CHANNEL_1);
	HAL_ADC_Start(&hadc1);
	s_bat_ms = HAL_GetTick();
}

// Неблокирующее измерение: забрать готовое преобразование, запустить следующее
static void battery_task(uint32_t now) {
	if (now - s_bat_ms < APP_BAT_PERIOD_MS) {
		return;
	}
	s_bat_ms = now;
	if (__HAL_ADC_GET_FLAG(&hadc1, ADC_FLAG_EOC)) {
		uint32_t v = HAL_ADC_GetValue(&hadc1);
		if (s_bat_phase_ref) {
			s_ref_sum += v;
			if (++s_bat_n >= APP_BAT_AVG_N) {
				battery_update();
			}
		} else {
			s_bat_sum += v;
			if (v > s_bat_max) {
				s_bat_max = v;
			}
		}
		s_bat_phase_ref = !s_bat_phase_ref;
		adc_select(s_bat_phase_ref ? ADC_CHANNEL_VREFINT : ADC_CHANNEL_1);
	}
	HAL_ADC_Start(&hadc1);
}

/* ------------------------------------------------------------------------ */
/* Тумблер записи                                                           */
/* ------------------------------------------------------------------------ */

static bool sw_read(void) {
	return HAL_GPIO_ReadPin(SW_RECORD_GPIO_Port, SW_RECORD_Pin) == GPIO_PIN_RESET;
}

static void switch_task(uint32_t now) {
	bool raw = sw_read();
	if (raw != s_sw_raw) {
		s_sw_raw = raw;
		s_sw_ms = now;
	} else if (raw != g_app.rec_switch_on && now - s_sw_ms >= APP_SW_DEBOUNCE_MS) {
		g_app.rec_switch_on = raw;
	}
}

/* ------------------------------------------------------------------------ */
/* Датчики                                                                  */
/* ------------------------------------------------------------------------ */

int app_sensor_index(uint8_t addr) {
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		if (APP_SENSOR_ADDR(i) == addr) {
			return i;
		}
	}
	return -1;
}

// Учесть результат транзакции датчика i. true — свежий отсчёт.
static bool sensor_apply(uint8_t i, const bwm427_xfer_t *x) {
	app_sensor_t *s = &g_app.sensor[i];
	sensor_priv_t *p = &s_priv[i];
	if (x->result == BWM427_OK) {
		float rx, ry;
		bwm427_decode_xy(x->regs, &rx, &ry);
		if (s->status != SENSOR_OK) {
			// Датчик (снова) появился: старая история фильтра не годится
			bwm427_filter_reset(&p->flt);
			s->status = SENSOR_OK;
		}
		p->fail_streak = 0;
		s->raw_x = rx;
		s->raw_y = ry;
		bwm427_filter_step(&p->flt, rx, ry, g_app.ema_alpha, &s->filt_x,
				&s->filt_y);
		s->x = s->filt_x - s->off_x;
		s->y = s->filt_y - s->off_y;
		s->last_ok_ms = x->t_ms;
		s->ok_count++;
		stat_add(&s->lat_first, x->lat_first_us);
		stat_add(&s->lat_done, x->lat_done_us);
		return true;
	}
	s->last_err = (uint8_t) x->result;
	if (x->result == BWM427_CRC) {
		s->crc_count++;
	} else if (x->result == BWM427_BAD_FRAME) {
		s->frame_count++;
	} else if (x->result == BWM427_TIMEOUT && s->status != SENSOR_ABSENT) {
		s->timeout_count++;
	}
	// Битый ответ: кто-то на этом адресе отвечает, но кадр испорчен —
	// обычно два датчика с одинаковым адресом или помеха на линии.
	// Считаем всегда, даже если датчик ещё ни разу не ответил правильно.
	if (x->result == BWM427_CRC || x->result == BWM427_BAD_FRAME) {
		s->garbled_count++;
		s->last_garbled_ms = x->t_ms;
	}
	// Пробы ни разу не отвечавшего датчика ошибками не считаем
	if (s->status != SENSOR_ABSENT) {
		s->err_count++;
	}
	if (p->fail_streak < 255) {
		p->fail_streak++;
	}
	if (s->status == SENSOR_OK && p->fail_streak >= APP_FAIL_LIMIT) {
		s->status = SENSOR_LOST;
	}
	return false;
}

// Сбросить качку датчика i (пропал или ещё не отвечал)
static void roll_reset(uint8_t i) {
	app_sensor_t *s = &g_app.sensor[i];
	s_roll[i].n = 0;
	s_roll[i].i = 0;
	s_roll[i].used = false;
	s->roll_x = s->roll_y = 0.0f;
	s->calm_x = s->calm_y = false;
	s->roll_fill_s = 0;
}

// Покой по оси с гистерезисом: входит при размахе < порога (1,5°), выходит
// при размахе > порог + гистерезис (1,7°)
static bool roll_calm(bool was, bool full, float span) {
	if (!full) {
		return false;
	}
	return was ? (span <= g_app.roll_calm_deg + g_app.roll_hyst_deg)
			: (span < g_app.roll_calm_deg);
}

// Угол в сотых градуса для буфера (|угол| <= 90° у датчика — в int16 входит)
static int16_t roll_cdeg(float deg) {
	float c = deg * 100.0f;
	if (c > 32767.0f) {
		c = 32767.0f;
	} else if (c < -32767.0f) {
		c = -32767.0f;
	}
	return (int16_t) (c >= 0.0f ? c + 0.5f : c - 0.5f);
}

// Качка датчика i по обеим осям. Отсчёты в окно — по расписанию раз в
// 1000 / roll_rate_hz мс (при 5 Гц — 200 мс, в среднем ровно столько при любой
// частоте опроса; при опросе реже — каждый). Размах = максимум - минимум за
// последние roll_window_s секунд, по filt_x/filt_y (до вычитания нуля: SET ZERO
// не даёт ложного скачка). Расчёт — проход по <= window * rate отсчётам двух
// осей на каждый взятый отсчёт (от нового к старым, до границы окна).
static void roll_update(uint8_t i, uint32_t now) {
	app_sensor_t *s = &g_app.sensor[i];
	roll_buf_t *b = &s_roll[i];
	const uint32_t sample_ms = 1000u / g_app.roll_rate_hz;
	const uint32_t window_ms = (uint32_t) g_app.roll_window_s * 1000u;
	if (b->used && (int32_t) (now - b->due_ms) < -(int32_t) APP_STAB_SAMPLE_TOL_MS) {
		return;
	}
	uint32_t next = b->due_ms + sample_ms;
	if (!b->used || (int32_t) (now - next) >= -(int32_t) APP_STAB_SAMPLE_TOL_MS) {
		next = now + sample_ms;
	}
	b->due_ms = next;
	b->used = true;
	b->v[0][b->i] = roll_cdeg(s->filt_x);
	b->v[1][b->i] = roll_cdeg(s->filt_y);
	b->t[b->i] = now;
	b->i = (uint16_t) ((b->i + 1u) % APP_STAB_N);
	if (b->n < APP_STAB_N) {
		b->n++;
	}

	// Размах по отсчётам окна: от нового к старым, до первого отсчёта старше
	// окна (время в буфере идёт по порядку записи). Так проход — по window *
	// rate отсчётам, а не по всему буферу на максимальное окно.
	int16_t mnx, mxx, mny, mxy;
	uint16_t j = (uint16_t) ((b->i + APP_STAB_N - 1u) % APP_STAB_N); // только что записанный
	mnx = mxx = b->v[0][j];
	mny = mxy = b->v[1][j];
	uint32_t oldest_age = 0;
	for (uint16_t k = 1; k < b->n; k++) {
		j = (uint16_t) (j == 0 ? APP_STAB_N - 1u : j - 1u);
		uint32_t age = now - b->t[j];
		if (age > window_ms) {
			break;
		}
		oldest_age = age;
		int16_t vx = b->v[0][j], vy = b->v[1][j];
		if (vx < mnx) {
			mnx = vx;
		} else if (vx > mxx) {
			mxx = vx;
		}
		if (vy < mny) {
			mny = vy;
		} else if (vy > mxy) {
			mxy = vy;
		}
	}
	s->roll_x = (float) (mxx - mnx) * 0.01f;
	s->roll_y = (float) (mxy - mny) * 0.01f;
	// Окно заполнено, когда данные покрывают его целиком (с точностью до шага)
	bool full = oldest_age + sample_ms >= window_ms;
	uint32_t fill_s = (oldest_age + sample_ms) / 1000u;
	s->roll_fill_s = (uint8_t) (fill_s > g_app.roll_window_s ? g_app.roll_window_s : fill_s);
	s->calm_x = roll_calm(s->calm_x, full, s->roll_x);
	s->calm_y = roll_calm(s->calm_y, full, s->roll_y);
}

// Качка по всем осям: обновить датчики со свежим отсчётом, затем сводка —
// «ГОТОВ», только если все оси всех отвечающих датчиков в покое
static void stability_update(const bool fresh[APP_SENSOR_COUNT], uint32_t now) {
	uint8_t ok = 0, worst = APP_STAB_NONE, worst_axis = 0;
	uint8_t fill = g_app.roll_window_s;
	float worst_span = 0.0f;
	bool calm = true;
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		app_sensor_t *s = &g_app.sensor[i];
		if (s->status != SENSOR_OK) {
			if (s_roll[i].used) {
				roll_reset(i); // пропал — после возвращения копить окно заново
			}
			continue;
		}
		if (fresh[i]) {
			roll_update(i, now);
		}
		ok++;
		calm = calm && s->calm_x && s->calm_y;
		if (s->roll_fill_s < fill) {
			fill = s->roll_fill_s;
		}
		if (worst == APP_STAB_NONE || s->roll_x > worst_span) {
			worst = i;
			worst_axis = 0;
			worst_span = s->roll_x;
		}
		if (s->roll_y > worst_span) {
			worst = i;
			worst_axis = 1;
			worst_span = s->roll_y;
		}
	}
	g_app.stab_sensor = worst;
	g_app.stab_axis = worst_axis;
	g_app.stab_span = worst_span;
	g_app.stab_fill_s = ok ? fill : 0;
	g_app.is_stable = ok > 0 && calm;
}

// Фактическая частота: сглаженный интервал между завершёнными циклами
static void rate_update(uint32_t now) {
	if (s_rate_valid) {
		float dt = (float) (now - s_rate_last_ms);
		if (dt < 1.0f) {
			dt = 1.0f;
		}
		s_rate_dt_ms += APP_RATE_ALPHA * (dt - s_rate_dt_ms);
		g_app.actual_rate_hz = 1000.0f / s_rate_dt_ms;
	} else {
		s_rate_valid = true;
		s_rate_dt_ms = 1000.0f / (float) g_app.log_freq_hz;
	}
	s_rate_last_ms = now;
}

// Пора ли начинать цикл. Расписание жёсткое (epoch + n * период), так что
// частота не плывёт от задержек суперцикла; если отстали больше чем на
// период — цикл сразу и расписание от текущего момента (без «догоняния»).
static bool schedule_due(uint32_t now) {
	uint16_t hz = g_app.log_freq_hz;
	uint32_t period = 1000u / hz;
	uint32_t due = s_epoch_ms + (uint32_t) s_epoch_n * 1000u / hz;
	int32_t late = (int32_t) (now - due);
	if (late < 0) {
		return false;
	}
	if ((uint32_t) late >= period) {
		s_epoch_ms = now;
		s_epoch_n = 0;
	}
	if (++s_epoch_n >= hz) { // каждую секунду переносить отсчёт: без переполнения
		s_epoch_n = 0;
		s_epoch_ms += 1000u;
	}
	return true;
}

/* ------------------------------------------------------------------------ */
/* Смена адреса датчика                                                     */
/* ------------------------------------------------------------------------ */

static void svc_start(void) {
	bwm427_xfer_t *x = &s_xfer[0];
	switch (s_svc_step) {
	case SVC_STEP_WRITE:
		bwm427_xfer_write(x, s_svc_old, BWM427_REG_ADDR, s_svc_new);
		x->alt_addr = s_svc_new; // эхо может прийти уже с нового адреса
		break;
	case SVC_STEP_SAVE_NEW:
		bwm427_xfer_write(x, s_svc_new, BWM427_REG_SAVE, 0);
		break;
	case SVC_STEP_SAVE_OLD:
	default:
		bwm427_xfer_write(x, s_svc_old, BWM427_REG_SAVE, 0);
		break;
	}
	s_xfer_n = 1;
	s_batch_svc = true;
	bwm427_start(s_xfer, 1);
}

static void svc_finish(bool ok, uint32_t now) {
	s_svc_step = SVC_STEP_NONE;
	g_app.svc_state = ok ? SVC_OK : SVC_FAIL;
	if (!ok) {
		return;
	}
	// Старого адреса больше нет: датчик считаем пропавшим (если он всё же
	// отзовётся по старому адресу — проба найдёт его через секунду).
	// Адреса не из опрашиваемых (например, 1 по умолчанию) нас не касаются.
	int i = app_sensor_index(s_svc_old);
	if (i >= 0) {
		g_app.sensor[i].status = SENSOR_ABSENT;
		s_priv[i].fail_streak = 0;
		s_priv[i].last_probe_ms = now;
	}
	// Новый адрес — пробовать сразу же
	i = app_sensor_index(s_svc_new);
	if (i >= 0) {
		s_priv[i].last_probe_ms = now - APP_PROBE_MS;
		s_probe_rr = (uint8_t) i;
	}
}

static void svc_result(const bwm427_xfer_t *x, uint32_t now) {
	bool ok = (x->result == BWM427_OK);
	switch (s_svc_step) {
	case SVC_STEP_WRITE:
		if (x->result == BWM427_EXCEPTION) {
			svc_finish(false, now); // датчик отказался (недопустимый адрес)
		} else {
			// Даже без ответа: эхо могло потеряться, а адрес — смениться
			s_svc_step = SVC_STEP_SAVE_NEW;
		}
		break;
	case SVC_STEP_SAVE_NEW:
		if (ok) {
			svc_finish(true, now);
		} else {
			s_svc_step = SVC_STEP_SAVE_OLD;
		}
		break;
	case SVC_STEP_SAVE_OLD:
	default:
		svc_finish(ok, now);
		break;
	}
}

/* ------------------------------------------------------------------------ */
/* Опрос                                                                    */
/* ------------------------------------------------------------------------ */

// Тайминги шины за цикл: тишина перед запросами, простой перед циклом сверх
// паузы, время цикла на шине (для циклов, где все опрошенные ответили; простой
// перед первым запросом — не время шины, вместо него пауза bus_gap_ms)
static void bus_stats_cycle(void) {
	uint32_t gap_set = (uint32_t) g_app.bus_gap_ms * 1000u;
	uint32_t total = 0;
	bool clean = s_batch_measure;
	for (uint8_t k = 0; k < s_xfer_n; k++) {
		const bwm427_xfer_t *x = &s_xfer[k];
		uint32_t gap = x->gap_us;
		if (k == 0) {
			stat_add(&g_app.bus.idle, gap > gap_set ? gap - gap_set : 0u);
			if (gap > gap_set) {
				gap = gap_set;
			}
		} else {
			stat_add(&g_app.bus.gap, gap);
		}
		total += gap + x->dur_us;
		int i = app_sensor_index(x->addr);
		if (x->result != BWM427_OK || i < 0) {
			clean = false; // таймаут или проба отсутствующего — не предел шины
		}
	}
	if (clean) {
		stat_add(&g_app.bus.cycle, total);
	}
	g_app.bus.timer_fallbacks = bwm427_fallbacks();
}

// Разобрать результаты завершённого цикла. fresh[i] — датчик i дал отсчёт.
static void poll_collect(bool fresh[APP_SENSOR_COUNT], uint32_t now) {
	bus_stats_cycle();
	for (uint8_t k = 0; k < s_xfer_n; k++) {
		int i = app_sensor_index(s_xfer[k].addr);
		if (i >= 0) {
			fresh[i] = sensor_apply((uint8_t) i, &s_xfer[k]);
		}
	}
	stability_update(fresh, now);
	// Цикл, начатый до смены частоты, в оценку новой частоты не идёт
	if (s_batch_measure && s_batch_hz == g_app.log_freq_hz) {
		rate_update(now);
	}
}

// Таймаут ответа отвечающего датчика i (см. APP_TO_*): короче полного, когда
// его задержка известна. Только сокращает потерю времени на пропущенный ответ
// (при задержке ~15 мс получается больше BWM427_TIMEOUT_US — остаётся полный).
static uint32_t sensor_timeout_us(uint8_t i) {
	const app_sensor_t *s = &g_app.sensor[i];
	uint32_t full = BWM427_TIMEOUT_US;
	if (s->lat_first.n < APP_TO_LEARN_N || s_priv[i].fail_streak != 0) {
		return full;
	}
	uint32_t t = 2u * s->lat_first.max_us
			+ BWM427_CHARS_US(BWM427_READ_LEN(BWM427_REG_COUNT)) + APP_TO_MARGIN_US;
	if (t < APP_TO_MIN_US) {
		t = APP_TO_MIN_US;
	}
	return (t < full) ? t : full;
}

// Запустить сервисную команду или очередной цикл, если пора
static void poll_start(uint32_t now) {
	if (!bwm427_idle()) {
		return;
	}
	if (s_svc_step != SVC_STEP_NONE) {
		svc_start();
		return;
	}
	if (!schedule_due(now)) {
		return;
	}
	uint8_t n = 0;
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		if (g_app.sensor[i].status == SENSOR_OK) {
			bwm427_xfer_read(&s_xfer[n], APP_SENSOR_ADDR(i));
			g_app.sensor[i].timeout_us = sensor_timeout_us(i);
			s_xfer[n++].timeout_us = g_app.sensor[i].timeout_us;
		}
	}
	s_batch_measure = (n > 0);
	s_batch_hz = g_app.log_freq_hz;
	// Не больше одной пробы неотвечающего датчика за цикл, каждого — не чаще
	// раза в секунду, по кругу. Проба последней: её таймаут не сдвигает
	// отсчёты рабочих датчиков.
	uint32_t interval = (now < APP_BOOT_FAST_MS) ? APP_PROBE_BOOT_MS : APP_PROBE_MS;
	for (uint8_t k = 0; k < APP_SENSOR_COUNT; k++) {
		uint8_t i = (uint8_t) ((s_probe_rr + k) % APP_SENSOR_COUNT);
		if (g_app.sensor[i].status != SENSOR_OK
				&& now - s_priv[i].last_probe_ms >= interval) {
			bwm427_xfer_read(&s_xfer[n++], APP_SENSOR_ADDR(i)); // таймаут полный
			g_app.sensor[i].timeout_us = BWM427_TIMEOUT_US;
			s_priv[i].last_probe_ms = now;
			s_probe_rr = (uint8_t) ((i + 1) % APP_SENSOR_COUNT);
			break;
		}
	}
	if (n == 0) {
		return; // опрашивать некого — слот расписания пропускаем
	}
	s_xfer_n = n;
	s_batch_svc = false;
	bwm427_start(s_xfer, n);
}

static void poll_task(uint32_t now) {
	bool fresh[APP_SENSOR_COUNT] = { false };
	bool cycle_done = false;
	if (bwm427_take()) {
		if (s_batch_svc) {
			svc_result(&s_xfer[0], now);
		} else {
			poll_collect(fresh, now);
			cycle_done = true;
		}
	}
	// Следующий цикл запускаем до записи на SD: шина работает, пока пишем
	poll_start(now);
	if (cycle_done) {
		sd_logger_write_cycle(fresh);
	}
	if (s_rate_valid && now - s_rate_last_ms > APP_RATE_TIMEOUT_MS) {
		s_rate_valid = false;
		g_app.actual_rate_hz = 0.0f;
	}
}

/* ------------------------------------------------------------------------ */
/* Настройки                                                                */
/* ------------------------------------------------------------------------ */

static uint16_t clamp_freq(uint16_t hz) {
	if (hz < APP_FREQ_MIN_HZ) {
		hz = APP_FREQ_MIN_HZ;
	}
	if (hz > APP_FREQ_MAX_HZ) {
		hz = APP_FREQ_MAX_HZ;
	}
	return hz;
}

static float clamp_alpha(float alpha) {
	if (!(alpha >= APP_ALPHA_MIN)) { // заодно NaN
		alpha = APP_ALPHA_MIN;
	}
	if (alpha > APP_ALPHA_MAX) {
		alpha = APP_ALPHA_MAX;
	}
	return alpha;
}

static uint8_t clamp_gap(uint8_t ms) {
	if (ms < BWM427_GAP_MS) {
		ms = BWM427_GAP_MS;
	}
	if (ms > BWM427_GAP_MAX_MS) {
		ms = BWM427_GAP_MAX_MS;
	}
	return ms;
}

static uint8_t clamp_theme(uint8_t theme) {
	return (theme > APP_THEME_LIGHT) ? APP_THEME_LIGHT : theme;
}

// NaN — поля нет в записи (старая прошивка): порог по умолчанию
static float clamp_bat_alarm(float v) {
	if (v != v) {
		return APP_DEF_BAT_ALARM_V;
	}
	if (v < APP_BAT_ALARM_MIN_V) {
		v = APP_BAT_ALARM_MIN_V;
	}
	if (v > APP_BAT_ALARM_MAX_V) {
		v = APP_BAT_ALARM_MAX_V;
	}
	return v;
}

static uint8_t clamp_roll_window(uint8_t s) {
	return s < APP_ROLL_WIN_MIN_S ? APP_ROLL_WIN_MIN_S
			: s > APP_ROLL_WIN_MAX_S ? APP_ROLL_WIN_MAX_S : s;
}

static uint8_t clamp_roll_rate(uint8_t hz) {
	return hz < APP_ROLL_RATE_MIN_HZ ? APP_ROLL_RATE_MIN_HZ
			: hz > APP_ROLL_RATE_MAX_HZ ? APP_ROLL_RATE_MAX_HZ : hz;
}

// Градусы с шагом 0,01 (так хранятся во флеше), в пределах [lo, hi]; NaN — def
static float clamp_deg(float d, float lo, float hi, float def) {
	if (d != d) {
		d = def;
	}
	if (d < lo) {
		d = lo;
	}
	if (d > hi) {
		d = hi;
	}
	return (float) (int32_t) (d * 100.0f + 0.5f) * 0.01f;
}

static float clamp_roll_calm(float d) {
	return clamp_deg(d, APP_ROLL_CALM_MIN_DEG, APP_ROLL_CALM_MAX_DEG, APP_ROLL_CALM_DEF_DEG);
}

static float clamp_roll_hyst(float d) {
	return clamp_deg(d, APP_ROLL_HYST_MIN_DEG, APP_ROLL_HYST_MAX_DEG, APP_ROLL_HYST_DEF_DEG);
}

// Значения по умолчанию, поверх — последняя запись из флеша (если есть).
// Записанное приводится в пределы: его могла оставить другая версия прошивки.
static void settings_init(void) {
	g_app.log_freq_hz = APP_DEF_FREQ_HZ;
	g_app.ema_alpha = APP_DEF_ALPHA;
	g_app.bus_gap_ms = APP_DEF_GAP_MS;
	g_app.theme = APP_DEF_THEME;
	g_app.bat_alarm_v = APP_DEF_BAT_ALARM_V;
	g_app.roll_window_s = APP_ROLL_WIN_DEF_S;
	g_app.roll_rate_hz = APP_ROLL_RATE_DEF_HZ;
	g_app.roll_calm_deg = APP_ROLL_CALM_DEF_DEG;
	g_app.roll_hyst_deg = APP_ROLL_HYST_DEF_DEG;
	settings_t s;
	if (settings_load(&s)) {
		g_app.log_freq_hz = clamp_freq(s.log_freq_hz);
		g_app.ema_alpha = clamp_alpha(s.ema_alpha);
		g_app.bus_gap_ms = clamp_gap(s.bus_gap_ms);
		g_app.theme = clamp_theme(s.theme);
		g_app.bat_alarm_v = clamp_bat_alarm(s.bat_alarm_v);
		// Поля качки: 0xFF / 0xFFFF — запись старой прошивки, остаются умолчания
		if (s.roll_window_s != 0xFFu) {
			g_app.roll_window_s = clamp_roll_window(s.roll_window_s);
		}
		if (s.roll_rate_hz != 0xFFu) {
			g_app.roll_rate_hz = clamp_roll_rate(s.roll_rate_hz);
		}
		if (s.roll_calm_cdeg != 0xFFFFu) {
			g_app.roll_calm_deg = clamp_roll_calm((float) s.roll_calm_cdeg * 0.01f);
		}
		if (s.roll_hyst_cdeg != 0xFFFFu) {
			g_app.roll_hyst_deg = clamp_roll_hyst((float) s.roll_hyst_cdeg * 0.01f);
		}
	}
}

// Сохранить изменённые настройки через SETTINGS_SAVE_DELAY_MS после последнего
// изменения. Запись во флеш останавливает процессор (слово ~16 мкс, стирание
// сектора раз в 4096 сохранений — 1-2 с), поэтому во время записи замера на SD
// не сохраняем — ждём, пока запись остановят.
static void settings_save_task(uint32_t now) {
	if (g_app.sd_state == SD_RECORDING || !settings_due(now)) {
		return;
	}
	settings_t s;
	s.log_freq_hz = g_app.log_freq_hz;
	s.ema_alpha = g_app.ema_alpha;
	s.bus_gap_ms = g_app.bus_gap_ms;
	s.theme = g_app.theme;
	s.bat_alarm_v = g_app.bat_alarm_v;
	s.roll_window_s = g_app.roll_window_s;
	s.roll_rate_hz = g_app.roll_rate_hz;
	s.roll_calm_cdeg = (uint16_t) (g_app.roll_calm_deg * 100.0f + 0.5f);
	s.roll_hyst_cdeg = (uint16_t) (g_app.roll_hyst_deg * 100.0f + 0.5f);
	settings_store(&s);
}

/* ------------------------------------------------------------------------ */
/* Интерфейс модуля                                                         */
/* ------------------------------------------------------------------------ */

void app_init(void) {
	uint32_t now = HAL_GetTick();
	memset(&g_app, 0, sizeof(g_app));
	memset(s_priv, 0, sizeof(s_priv));
	memset(s_roll, 0, sizeof(s_roll));
	s_rate_valid = false;
	s_svc_step = SVC_STEP_NONE;
	s_probe_rr = 0;
	settings_init(); // до ui_init(): интерфейс сразу показывает сохранённое
	g_app.svc_state = SVC_IDLE;
	g_app.stab_sensor = APP_STAB_NONE;
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		g_app.sensor[i].addr = APP_SENSOR_ADDR(i);
		g_app.sensor[i].status = SENSOR_ABSENT;
		g_app.sensor[i].timeout_us = BWM427_TIMEOUT_US;
		s_priv[i].last_probe_ms = now - APP_PROBE_MS; // пробовать сразу
	}

	clock_init();
	battery_init();

	s_sw_raw = sw_read();
	s_sw_ms = now;
	g_app.rec_switch_on = s_sw_raw;

	bwm427_init();
	bwm427_set_gap_ms(g_app.bus_gap_ms);
	sd_logger_init(); // монтирование карты: до ~1 с, если карта вставлена

	s_epoch_ms = HAL_GetTick();
	s_epoch_n = 0;
}

void app_task(void) {
	uint32_t now = HAL_GetTick();
	switch_task(now);
	clock_task(now);
	battery_task(now);
	poll_task(now);
	sd_logger_task();
	settings_save_task(now); // после sd_logger_task: старт записи откладывает сохранение
	// Светодиод PC13 (активный 0) горит во время записи
	HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin,
			g_app.sd_state == SD_RECORDING ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void app_set_log_freq(uint16_t hz) {
	hz = clamp_freq(hz);
	if (hz == g_app.log_freq_hz) {
		return;
	}
	g_app.log_freq_hz = hz;
	s_epoch_ms = HAL_GetTick(); // новое расписание с текущего момента
	s_epoch_n = 0;
	s_rate_valid = false;       // и оценку частоты — с новой номинальной
	settings_touch(HAL_GetTick());
}

void app_set_bus_gap(uint8_t ms) {
	ms = clamp_gap(ms);
	if (ms == g_app.bus_gap_ms) {
		return;
	}
	g_app.bus_gap_ms = ms;
	bwm427_set_gap_ms(ms);
	settings_touch(HAL_GetTick());
}

void app_set_ema_alpha(float alpha) {
	alpha = clamp_alpha(alpha);
	if (alpha == g_app.ema_alpha) {
		return;
	}
	g_app.ema_alpha = alpha;
	settings_touch(HAL_GetTick());
}

void app_set_theme(uint8_t theme) {
	theme = clamp_theme(theme);
	if (theme == g_app.theme) {
		return;
	}
	g_app.theme = theme;
	settings_touch(HAL_GetTick());
}

// Сбросить окна качки всех датчиков (сменились окно или частота отсчётов)
static void roll_reset_all(void) {
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		roll_reset(i);
	}
	g_app.is_stable = false;
	g_app.stab_fill_s = 0;
}

void app_set_roll_window(uint8_t seconds) {
	seconds = clamp_roll_window(seconds);
	if (seconds == g_app.roll_window_s) {
		return;
	}
	g_app.roll_window_s = seconds;
	roll_reset_all();
	settings_touch(HAL_GetTick());
}

void app_set_roll_rate(uint8_t hz) {
	hz = clamp_roll_rate(hz);
	if (hz == g_app.roll_rate_hz) {
		return;
	}
	g_app.roll_rate_hz = hz;
	roll_reset_all();
	settings_touch(HAL_GetTick());
}

void app_set_roll_calm(float deg) {
	deg = clamp_roll_calm(deg);
	if (deg == g_app.roll_calm_deg) {
		return;
	}
	g_app.roll_calm_deg = deg;
	settings_touch(HAL_GetTick());
}

void app_set_roll_hyst(float deg) {
	deg = clamp_roll_hyst(deg);
	if (deg == g_app.roll_hyst_deg) {
		return;
	}
	g_app.roll_hyst_deg = deg;
	settings_touch(HAL_GetTick());
}

void app_set_bat_alarm(float volts) {
	volts = clamp_bat_alarm(volts);
	if (volts == g_app.bat_alarm_v) {
		return;
	}
	g_app.bat_alarm_v = volts;
	settings_touch(HAL_GetTick());
}

void app_zero_all(void) {
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		app_sensor_t *s = &g_app.sensor[i];
		if (s->status != SENSOR_OK) {
			continue; // нет свежих данных — ноль не трогаем
		}
		s->off_x = s->filt_x;
		s->off_y = s->filt_y;
		s->x = 0.0f;
		s->y = 0.0f;
	}
}

void app_zero_reset(void) {
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		app_sensor_t *s = &g_app.sensor[i];
		s->off_x = 0.0f;
		s->off_y = 0.0f;
		s->x = s->filt_x;
		s->y = s->filt_y;
	}
}

void app_set_time(const app_time_t *t) {
	app_time_t v = *t;
	// Привести поля в допустимые пределы (день — с учётом месяца и года)
	if (v.year > 99) {
		v.year = 99;
	}
	if (v.month < 1) {
		v.month = 1;
	}
	if (v.month > 12) {
		v.month = 12;
	}
	if (v.date < 1) {
		v.date = 1;
	}
	if (v.date > app_days_in_month(v.month, v.year)) {
		v.date = app_days_in_month(v.month, v.year);
	}
	if (v.hours > 23) {
		v.hours = 23;
	}
	if (v.minutes > 59) {
		v.minutes = 59;
	}
	if (v.seconds > 59) {
		v.seconds = 59;
	}
	g_app.time = v;
	s_clock_ms = HAL_GetTick(); // секунда отсчитывается от момента установки
	if (g_app.rtc_present && !ds3231_write(&v)) {
		g_app.rtc_present = false;
	}
}

void app_sensor_set_address(uint8_t old_addr, uint8_t new_addr) {
	if (g_app.svc_state == SVC_BUSY) {
		return; // предыдущая команда ещё выполняется
	}
	// Modbus: адреса 1..247 (любые, не только опрашиваемые); новый не должен
	// быть занят отвечающим датчиком
	int busy = app_sensor_index(new_addr);
	if (old_addr < 1 || old_addr > 247 || new_addr < 1 || new_addr > 247
			|| old_addr == new_addr
			|| (busy >= 0 && g_app.sensor[busy].status == SENSOR_OK)) {
		g_app.svc_state = SVC_FAIL;
		return;
	}
	s_svc_old = old_addr;
	s_svc_new = new_addr;
	s_svc_step = SVC_STEP_WRITE;
	g_app.svc_state = SVC_BUSY; // выполнится между циклами опроса
}
