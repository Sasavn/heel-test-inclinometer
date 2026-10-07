/*
 * bwm427.h — инклинометры BWM427 по RS485 (Modbus RTU, USART1, MAX485).
 *
 * Мастер шины неблокирующий: пакет из нескольких транзакций (по одной на
 * датчик) выполняется в прерываниях — USART1 (передача/приём) и TIM5 (паузы
 * между кадрами и таймауты с точностью до микросекунды; SysTick через
 * bwm427_tick_1ms() — запасной путь). Суперцикл только запускает пакет
 * (bwm427_start) и забирает результат (bwm427_take).
 *
 * По каждой транзакции драйвер меряет (TIM5, 1 мкс): тишину на шине перед
 * запросом, задержку ответа датчика от конца запроса до первого байта и до
 * конца кадра, длительность транзакции — статистику ведёт app.c.
 *
 * Здесь же — разбор протокола и фильтры (чистые функции без HAL, покрыты
 * host-тестами в tests/host).
 */
#ifndef BWM427_H_
#define BWM427_H_

#include <stdint.h>
#include <stdbool.h>

// --- Шина ---
// Датчик по умолчанию работает на 9600 (рег. 0x000B), но прибор и датчики
// настроены на 115200. Если BWM427_BAUD отличается от скорости в
// MX_USART1_UART_Init, bwm427_init() переинициализирует USART1.
#define BWM427_BAUD          115200u

// Время одного символа 8N1 (10 бит), мкс
#define BWM427_CHAR_US       (10000000u / BWM427_BAUD)
// Время n символов подряд, мкс (с округлением: 9 байт на 115200 — 781 мкс)
#define BWM427_CHARS_US(n)   (((uint32_t) (n) * 10000000u + BWM427_BAUD / 2u) / BWM427_BAUD)

// Таймаут ответа (от конца передачи запроса): время 17 символов + запас датчика.
// 115200 -> 30 мс, 9600 -> 46 мс. Датчику, который уже отвечает, app.c может
// задать в транзакции таймаут короче (bwm427_xfer_t.timeout_us).
#define BWM427_RESP_MARGIN_MS 28u
#define BWM427_TIMEOUT_MS    ((17u * BWM427_CHAR_US) / 1000u + 1u + BWM427_RESP_MARGIN_MS)
#define BWM427_TIMEOUT_US    (BWM427_TIMEOUT_MS * 1000u)

// Пауза между кадрами: не меньше 3,5 символов и не меньше 2 мс.
// Отсчитывается таймером TIM5 от конца прошлой транзакции с точностью ~1 мкс.
#define BWM427_GAP_CHARS_MS  ((35u * BWM427_CHAR_US + 9999u) / 10000u)
#define BWM427_GAP_MS        (BWM427_GAP_CHARS_MS > 2u ? BWM427_GAP_CHARS_MS : 2u)

// Запасной путь по SysTick (таймер TIM5 не сработал): срок + столько мс.
// При исправном таймере не срабатывает никогда (счётчик — bwm427_fallbacks()).
#define BWM427_FALLBACK_MS   2u

// Рабочая пауза по умолчанию (bwm427_set_gap_ms). Датчики BWM427 делят кадры
// не по 3,5 символа, а по своей, заметно большей паузе тишины: запрос к одному
// датчику, отправленный через 2–3 мс после ответа другого, сливается с этим
// ответом в один «кадр» с неверной CRC и игнорируется — на шине работал только
// первый найденный датчик.
#define BWM427_GAP_DEFAULT_MS 15u
#define BWM427_GAP_MAX_MS     100u

// --- Протокол ---
#define BWM427_FUNC_READ     0x03    // чтение регистров
#define BWM427_FUNC_WRITE    0x06    // запись одного регистра
#define BWM427_REG_ANGLE     0x0001  // X = 0x0001, Y = 0x0002
#define BWM427_REG_COUNT     2       // сколько регистров читаем за опрос
#define BWM427_REG_ADDR      0x000D  // Modbus-адрес датчика
#define BWM427_REG_SAVE      0x000F  // запись 0 = сохранить настройки во флеш датчика

#define BWM427_REQ_LEN       8                               // запрос 03/06
#define BWM427_READ_LEN(n)   (5u + 2u * (n))                 // ответ на 03: адрес, функция, счётчик, данные, CRC
#define BWM427_WRITE_LEN     8                               // ответ на 06 — эхо запроса
#define BWM427_EXC_LEN       5                               // исключение: адрес, функция|0x80, код, CRC
#define BWM427_RX_MAX        16                              // приёмный буфер (больше любого нашего ответа)
#define BWM427_MAX_XFER      4                               // транзакций в одном пакете

typedef enum {
	BWM427_OK = 0,      // валидный ответ
	BWM427_TIMEOUT,     // нет ответа за BWM427_TIMEOUT_MS
	BWM427_CRC,         // битая контрольная сумма
	BWM427_BAD_FRAME,   // чужой адрес, функция, длина, неполный кадр
	BWM427_EXCEPTION,   // датчик ответил исключением Modbus (код в exc_code)
	BWM427_UART,        // ошибка UART (переполнение) или HAL занят
	BWM427_PENDING,     // ещё не выполнялась
} bwm427_result_t;

// Одна транзакция Modbus
typedef struct {
	// Запрос (заполняет вызывающий, удобнее через bwm427_xfer_read/write)
	uint8_t addr;        // адрес датчика
	uint8_t alt_addr;    // ответ принимается и с этого адреса (0 = нет): эхо при смене адреса
	uint8_t func;        // BWM427_FUNC_READ / BWM427_FUNC_WRITE
	uint16_t reg;        // первый регистр
	uint16_t val;        // READ: число регистров (<= BWM427_REG_COUNT); WRITE: значение
	uint32_t timeout_us; // таймаут ответа от конца запроса, мкс; 0 — BWM427_TIMEOUT_US
	// Результат (заполняет драйвер)
	bwm427_result_t result;
	uint8_t exc_code;    // код исключения Modbus
	uint16_t regs[BWM427_REG_COUNT];
	uint32_t t_ms;       // HAL_GetTick() окончания транзакции (приёма ответа)
	// Времена, мкс (TIM5)
	uint32_t gap_us;       // тишина на шине перед запросом: от конца прошлой транзакции
	uint32_t lat_first_us; // от конца запроса (флаг TC) до начала первого байта ответа
	                       // (байт адреса; помеха перед ним не в счёт); 0 — ответа не было.
	                       // Считается от конца порции байт (по прерыванию HAL) назад
	                       // на n символов: паузы между байтами короче символа завышают его
	uint32_t lat_done_us;  // ... до конца ответа (кадр принят целиком); 0 — кадра не было
	uint32_t dur_us;       // вся транзакция: от начала передачи запроса до конца
	                       // (ответ принят, таймаут или ошибка)
} bwm427_xfer_t;

// Медиана-3 + EMA по двум осям, своё состояние на каждый датчик
typedef struct {
	float mx[3], my[3]; // три последних сырых значения
	float ex, ey;       // состояние EMA
	bool init;
} bwm427_filter_t;

// --- Шина (HAL, прерывания) ---

// Настроить RS485 на приём, при необходимости переинициализировать USART1 на
// BWM427_BAUD, запустить таймер шины TIM5 (1 МГц, прерывание сравнения).
void bwm427_init(void);

// Пауза тишины на шине перед каждым запросом, мс (BWM427_GAP_MS..BWM427_GAP_MAX_MS)
void bwm427_set_gap_ms(uint16_t ms);

// Запустить пакет из n транзакций (n <= BWM427_MAX_XFER), выполняются по очереди;
// перед каждым запросом — пауза bwm427_set_gap_ms() от конца прошлой транзакции
// (если она уже прошла — передача сразу). Массив принадлежит драйверу до
// bwm427_take(). false — шина занята (предыдущий пакет не завершён или не забран).
bool bwm427_start(bwm427_xfer_t *xfer, uint8_t n);

// true (один раз) — пакет завершён, результаты в xfer[].result; шина свободна.
bool bwm427_take(void);

// Шина свободна, можно запускать пакет.
bool bwm427_idle(void);

// Вызывать раз в 1 мс из SysTick_Handler: запасной путь для пауз и таймаутов,
// если прерывание TIM5 опоздало или таймер не работает.
void bwm427_tick_1ms(void);

// Сколько раз паузу или таймаут отработал запасной путь по SysTick (таймер
// TIM5 не сработал за BWM427_FALLBACK_MS). При исправном таймере — 0.
uint32_t bwm427_fallbacks(void);

// --- Таймер шины ---
// На МК — TIM5 (bwm427.c), в host-тестах (-DBWM427_TIMER_FAKE) — имитация.
void bwm427_port_init(void);            // запустить счётчик мкс
uint32_t bwm427_port_us(void);          // счётчик мкс (32 бита, переполнение через 71 мин)
void bwm427_port_alarm(uint32_t at_us); // вызвать bwm427_alarm_isr() в момент at_us (уже прошёл — сразу)
void bwm427_port_alarm_off(void);
void bwm427_alarm_isr(void);            // из прерывания таймера

// --- Протокол (чистые функции) ---

uint16_t bwm427_crc16(const uint8_t *buf, uint16_t len);
void bwm427_xfer_read(bwm427_xfer_t *x, uint8_t addr);                       // углы X/Y
void bwm427_xfer_write(bwm427_xfer_t *x, uint8_t addr, uint16_t reg, uint16_t val);
void bwm427_build_request(const bwm427_xfer_t *x, uint8_t out[BWM427_REQ_LEN]);
uint16_t bwm427_reply_len(const bwm427_xfer_t *x);                           // длина нормального ответа

// Сколько байт должен занять кадр, начало которого лежит в buf[0..len):
// >0 — полная длина кадра, 0 — нужно больше байт, -1 — это не наш ответ.
int bwm427_frame_need(const bwm427_xfer_t *x, const uint8_t *buf, uint16_t len);

// Проверить ответ (адрес, функция, длина, CRC); при BWM427_OK заполняет x->regs.
bwm427_result_t bwm427_parse_reply(bwm427_xfer_t *x, const uint8_t *buf, uint16_t len);

// Перевод регистра в градусы — ЕДИНСТВЕННОЕ место, где задана формула.
float bwm427_decode_angle(uint16_t reg);
// Углы X/Y из прочитанных регистров (порядок регистров задан здесь же).
void bwm427_decode_xy(const uint16_t *regs, float *x, float *y);

float bwm427_median3(float a, float b, float c);
float bwm427_ema(float old_value, float new_value, float alpha);
void bwm427_filter_reset(bwm427_filter_t *f);
void bwm427_filter_step(bwm427_filter_t *f, float x, float y, float alpha,
		float *out_x, float *out_y);

#endif /* BWM427_H_ */
