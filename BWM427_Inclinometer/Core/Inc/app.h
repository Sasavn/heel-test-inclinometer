/*
 * app.h — общее состояние прибора и действия, доступные интерфейсу.
 *
 * Логика (датчики, SD, часы, батарея) пишет в g_app, интерфейс (ui.c) только
 * читает g_app и меняет настройки через функции app_*(). Всё работает в одном
 * суперцикле, поэтому блокировки не нужны; из прерываний g_app не трогать.
 */
#ifndef APP_H_
#define APP_H_

#include <stdint.h>
#include <stdbool.h>

// Датчики на одной шине RS485: Д2 и Д3 (Modbus-адреса 2 и 3; адрес 1 не
// используется). Вернуть третий датчик: COUNT 3 и ADDR (i) + 1.
#define APP_SENSOR_COUNT   2
#define APP_SENSOR_ADDR(i) ((uint8_t) ((i) + 2))
// Обратно: индекс в g_app.sensor по адресу — app_sensor_index()

#define APP_FREQ_MIN_HZ    1
#define APP_FREQ_MAX_HZ    50
#define APP_ALPHA_MIN      0.01f
#define APP_ALPHA_MAX      0.99f
#define APP_STAB_NONE      0xFF

#define APP_THEME_DARK     0
#define APP_THEME_LIGHT    1

// Тревога по напряжению питания (АКБ)
#define APP_BAT_ALARM_DEFAULT_V 10.0f  // порог по умолчанию, В
#define APP_BAT_ALARM_MIN_V     5.0f   // допустимый диапазон порога
#define APP_BAT_ALARM_MAX_V     30.0f
#define APP_BAT_ALARM_HYST_V    0.3f   // тревога снимается, когда V > порог + гистерезис
#define APP_BAT_PRESENT_MIN_V   5.0f   // ниже — АКБ не подключена (питание от USB):
                                       // тревогу не поднимаем
#define APP_BAT_CELLS           3      // 3S Li-ion 18650
#define APP_BAT_PCT_NONE        0xFF

typedef struct {
	uint8_t year;    // 0..99 (20xx)
	uint8_t month;   // 1..12
	uint8_t date;    // 1..31
	uint8_t hours;   // 0..23
	uint8_t minutes; // 0..59
	uint8_t seconds; // 0..59
} app_time_t;

// Статистика длительностей, мкс: с включения или с app_bus_stats_reset()
typedef struct {
	uint32_t n;              // замеров (0 — нет, остальные поля 0)
	uint32_t last_us;
	uint32_t min_us;
	uint32_t max_us;
	uint64_t sum_us;         // среднее — app_stat_avg_us()
} app_stat_t;

typedef enum {
	SENSOR_ABSENT = 0, // ни разу не ответил с момента включения
	SENSOR_OK,         // отвечает
	SENSOR_LOST,       // отвечал, но перестал (значения устарели)
} sensor_status_t;

typedef struct {
	uint8_t addr;            // Modbus-адрес
	sensor_status_t status;
	float raw_x, raw_y;      // последний ответ датчика, без фильтров, градусы
	float filt_x, filt_y;    // после медианы-3 и EMA
	float off_x, off_y;      // ноль, заданный кнопкой SET ZERO
	float x, y;              // filt - off: показываем и пишем на карту
	uint32_t last_ok_ms;     // HAL_GetTick() последнего удачного ответа
	uint32_t ok_count;       // удачные ответы
	uint32_t err_count;      // таймауты и битые CRC
	uint32_t garbled_count;  // битые ответы (CRC, чужой/неполный кадр), в т.ч. до первого
	                         // удачного: признак двух датчиков на одном адресе или помехи
	uint32_t last_garbled_ms;// HAL_GetTick() последнего битого ответа
	uint8_t last_err;        // bwm427_result_t последней неудачи, 0 = не было

	// Шина RS485: с включения или с app_bus_stats_reset() (usb_cli: diag reset)
	uint32_t timeout_count;  // нет ответа за таймаут (пробы ни разу не отвечавшего не в счёт)
	uint32_t crc_count;      // ответ с битой CRC
	uint32_t frame_count;    // чужой или неполный кадр (BWM427_BAD_FRAME)
	app_stat_t lat_first;    // удачные ответы: от конца запроса (TC) до первого байта ответа
	app_stat_t lat_done;     // ... до конца ответа (кадр принят целиком)
	uint32_t timeout_us;     // таймаут ответа, с которым датчик опрашивается сейчас, мкс
} app_sensor_t;

// Шина RS485 в целом: с включения или с app_bus_stats_reset()
typedef struct {
	app_stat_t gap;          // тишина перед запросом внутри цикла (2-й и далее датчик):
	                         // точность отсчёта паузы bus_gap_ms
	app_stat_t idle;         // простой шины перед циклом опроса сверх паузы: если
	                         // частота задана выше предельной — это задержка суперцикла
	app_stat_t cycle;        // циклы без ошибок: время на шине (паузы + транзакции);
	                         // предельная частота опроса = 1e6 / среднее
	uint32_t timer_fallbacks;// паузу/таймаут отработал SysTick, а не TIM5 (должно быть 0)
} app_bus_t;

// Диагностика суперцикла (обновляется раз в секунду)
typedef struct {
	uint32_t loops_per_s;    // проходов суперцикла за последнюю секунду
	uint16_t loop_max_ms;    // самый долгий проход за секунду
	uint16_t app_max_ms;     // ... из него app_task (датчики, SD, часы)
	uint16_t ui_max_ms;      // ... из него ui_task (LVGL: ввод и отрисовка)
	uint32_t uptime_s;
	uint8_t cpu_load_pct;    // загрузка процессора за последнюю секунду, 0..100 %
	                         // (доля времени, занятого работой, а не холостым
	                         // проходом суперцикла; прерывания учитываются)
	uint32_t idle_pass_cyc;  // оценка холостого прохода суперцикла, такты (см. app.c)
	uint32_t pass_min_cyc;   // самый короткий проход за последнюю секунду, такты
} app_diag_t;

typedef enum {
	SD_NO_CARD = 0, // карты нет или не смонтировалась (код в sd_err)
	SD_READY,       // карта готова, запись не идёт
	SD_RECORDING,   // идёт запись
	SD_ERROR,       // запись прервана ошибкой; ждём, пока тумблер вернут в STOP
} sd_state_t;

typedef enum {
	SVC_IDLE = 0,
	SVC_BUSY,        // команда в очереди или выполняется
	SVC_OK,          // датчик подтвердил новый адрес и сохранил настройки
	SVC_FAIL,        // нет ответа / ошибка
} svc_state_t;

typedef struct {
	app_sensor_t sensor[APP_SENSOR_COUNT];

	// Настройки (меняются из интерфейса)
	uint16_t log_freq_hz;    // частота опроса и записи, APP_FREQ_MIN_HZ..APP_FREQ_MAX_HZ
	float ema_alpha;         // коэффициент EMA, APP_ALPHA_MIN..APP_ALPHA_MAX
	float actual_rate_hz;    // фактическая частота циклов опроса (если шина не успевает)
	uint8_t bus_gap_ms;      // пауза тишины на шине перед каждым запросом, мс
	uint8_t theme;           // тема интерфейса: APP_THEME_DARK / APP_THEME_LIGHT

	// Запись на SD
	sd_state_t sd_state;
	uint8_t sd_err;          // последний FRESULT ошибки, 0 = нет
	uint16_t file_number;    // номер следующего / текущего замера MNNN: сквозной по карте
	                         // (наибольший на карте + 1); 0 — карты нет / номера кончились
	uint8_t rec_sensor_mask; // бит i = идёт запись файла датчика i
	uint32_t rec_start_ms;
	uint32_t rec_rows;       // строк записано за текущий замер (по всем файлам)
	bool rec_switch_on;      // положение тумблера записи

	// Прочее
	float battery_v;
	bool battery_present;    // battery_v >= APP_BAT_PRESENT_MIN_V (иначе питание от USB)
	bool battery_low;        // тревога: АКБ подключена и V < bat_alarm_v (с гистерезисом)
	float bat_alarm_v;       // порог тревоги, В (сохраняется вместе с настройками)
	uint8_t battery_pct;     // примерный заряд АКБ 3S Li-ion (18650 x3), 0..100 %;
	                         // 0 % = 3,3 В на ячейку (9,9 В), 100 % = 4,2 В (12,6 В);
	                         // APP_BAT_PCT_NONE — АКБ не подключена
	bool battery_adc_sat;    // АЦП батареи у верхнего предела (вход делителя выше
	                         // ~13,3 В при VDDA 3,3 В): battery_v — оценка снизу
	bool is_stable;         // размах качки по опорному датчику ниже порога
	float stab_span;         // размах Y опорного датчика за окно, градусы
	uint8_t stab_sensor;     // опорный датчик (первый отвечающий), APP_STAB_NONE — нет
	app_time_t time;
	bool rtc_present;        // найден DS3231 на I2C1

	// Сервисная команда (смена Modbus-адреса датчика)
	svc_state_t svc_state;

	app_diag_t diag;
	app_bus_t bus;           // тайминги шины RS485 (диагностика)
} app_state_t;

extern app_state_t g_app;

// Действия интерфейса
void app_set_log_freq(uint16_t hz);
void app_set_ema_alpha(float alpha);
void app_zero_all(void);   // ноль = текущие значения всех ответивших датчиков
void app_zero_reset(void); // сбросить ноль у всех датчиков
void app_set_time(const app_time_t *t);
void app_set_bus_gap(uint8_t ms); // пауза шины RS485 перед запросом (см. bwm427.h)
void app_set_theme(uint8_t theme); // APP_THEME_*; сохраняется вместе с настройками
void app_set_bat_alarm(float volts); // порог тревоги АКБ, APP_BAT_ALARM_MIN_V..MAX_V; сохраняется
// Частота, α, пауза шины, тема и порог АКБ сохраняются во флеш и переживают выключение.

// Сменить Modbus-адрес датчика old_addr -> new_addr (рег. 0x000D) и сохранить
// (рег. 0x000F). Асинхронно: выполняется планировщиком опроса, результат в
// g_app.svc_state. На шине должен быть только этот датчик (адрес по умолчанию 1).
void app_sensor_set_address(uint8_t old_addr, uint8_t new_addr);

// Индекс датчика в g_app.sensor по Modbus-адресу, -1 — адрес не наш
int app_sensor_index(uint8_t addr);

// Вызываются из main.c
void app_init(void); // после MX_*_Init(): настройки из флеша, часы, АЦП, шина RS485, SD-карта
void app_task(void); // каждый проход суперцикла, не блокирует (кроме записи на SD)
// Учесть длительность прохода суперцикла (в тактах ядра) для g_app.diag.
// Вызывать ровно раз за проход: интервал между вызовами (DWT->CYCCNT) —
// длительность прохода целиком, по ней считается cpu_load_pct.
void app_diag_loop(uint32_t app_cycles, uint32_t ui_cycles);

// Диагностика шины: обнулить счётчики и статистику g_app.sensor[i].timeout_count,
// crc_count, frame_count, lat_first, lat_done и g_app.bus (кроме timer_fallbacks)
void app_bus_stats_reset(void);
uint32_t app_stat_avg_us(const app_stat_t *s); // среднее, 0 при n = 0

// Загрузка процессора за окно (чистая функция, см. app.c): window_cyc тактов,
// passes проходов, самый короткий из них win_min_cyc. *idle_cyc — оценка
// холостого прохода, уточняется (0 — ещё нет).
uint8_t app_cpu_load_calc(uint32_t window_cyc, uint32_t passes, uint32_t win_min_cyc,
		uint32_t *idle_cyc);

// Календарь (чистые функции; годы 2000..2099)
uint8_t app_days_in_month(uint8_t month, uint8_t year); // 28..31
bool app_time_valid(const app_time_t *t);
void app_time_add_second(app_time_t *t);                 // с переходом через сутки/месяц/год
// Разобрать __DATE__ ("Oct  6 2026") и __TIME__ ("15:21:00")
bool app_time_from_build(const char *date, const char *time, app_time_t *t);

// Заряд АКБ 3S Li-ion по напряжению, 0..100 % (чистая функция; 3,3 В на
// ячейку = 0 %, 4,2 В = 100 %, между ними — кривая разряда)
uint8_t app_bat_pct(float volts);

#endif /* APP_H_ */
