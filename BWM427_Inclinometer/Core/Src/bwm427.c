/*
 * bwm427.c — Modbus RTU мастер для инклинометров BWM427 + разбор + фильтры.
 *
 * Транзакция: пауза gap от конца прошлой транзакции -> DE/RE = 1, передача
 * 8 байт по прерыванию -> по флагу TC (HAL_UART_TxCpltCallback) DE/RE = 0,
 * сброс ORE/DR -> приём до паузы на линии (HAL_UARTEx_ReceiveToIdle_IT), так
 * что и короткий ответ-исключение (5 байт) не ждёт таймаута -> проверка кадра.
 *
 * Сроки (конец паузы, таймаут передачи и ответа) отсчитывает TIM5: 32-битный
 * счётчик 1 МГц и прерывание по сравнению в нужную микросекунду. Раньше пауза
 * отсчитывалась тиками SysTick «строго больше gap мс», т. е. реально gap..gap+1
 * мс (в среднем +0,5 мс на каждую транзакцию), и цикл, запущенный суперциклом
 * после паузы, ждал ещё до 1 мс следующего тика. SysTick (bwm427_tick_1ms)
 * остался запасным путём: срабатывает, только если прерывание TIM5 не пришло
 * за BWM427_FALLBACK_MS (счётчик bwm427_fallbacks(); при исправном таймере — 0).
 *
 * Состояние шины меняют только прерывания (USART1 — приоритет 0, TIM5 — 1,
 * SysTick — 15); проверка срока и действие по нему (bus_service) идут при
 * запрещённых прерываниях. Суперцикл трогает состояние лишь в IDLE/DONE.
 */
#include "bwm427.h"
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart1;
#define BUS_UART (&huart1)

typedef enum {
	BUS_IDLE = 0, // пакета нет (или результат забран)
	BUS_GAP,      // пауза перед следующей транзакцией
	BUS_TX,       // передача запроса
	BUS_RX,       // ожидание ответа
	BUS_DONE,     // пакет завершён, ждём bwm427_take()
} bus_state_t;

static volatile bus_state_t s_state = BUS_IDLE;
static bwm427_xfer_t *s_xfer;            // текущий пакет (массив вызывающего)
static uint8_t s_count;                  // транзакций в пакете
static volatile uint8_t s_idx;           // текущая транзакция
static volatile uint32_t s_deadline_us;  // GAP: начало передачи; TX, RX: таймаут
static uint32_t s_mark_us;               // конец прошлой транзакции (начало тишины на шине)
static uint32_t s_tx_us;                 // начало передачи запроса
static uint32_t s_tc_us;                 // конец передачи запроса (флаг TC)
// Запасной путь по SysTick: GAP — от конца прошлой транзакции, TX — от старта,
// RX — от конца передачи; срок s_limit_ms (+ BWM427_FALLBACK_MS)
static volatile uint32_t s_mark_ms;
static volatile uint32_t s_limit_ms;
static volatile uint32_t s_fallbacks;
static uint8_t s_tx[BWM427_REQ_LEN];
static uint8_t s_rx[BWM427_RX_MAX];
static volatile uint16_t s_rx_len;       // принято байт текущего кадра
static uint16_t s_rx_want;               // запрошено у HAL в текущем приёме
static volatile uint16_t s_gap_ms = BWM427_GAP_DEFAULT_MS; // пауза перед запросом
static volatile uint32_t s_gap_us = BWM427_GAP_DEFAULT_MS * 1000u;

/* ------------------------------------------------------------------------ */
/* Протокол (чистые функции)                                                */
/* ------------------------------------------------------------------------ */

// Рассчитать контрольную сумму CRC16 Modbus (полином 0xA001, начальное 0xFFFF).
// В кадре передаётся младшим байтом вперёд.
uint16_t bwm427_crc16(const uint8_t *buf, uint16_t len) {
	uint16_t crc = 0xFFFF;
	for (uint16_t pos = 0; pos < len; pos++) {
		crc ^= (uint16_t) buf[pos];
		for (uint8_t i = 0; i < 8; i++) {
			if (crc & 0x0001) {
				crc = (uint16_t) ((crc >> 1) ^ 0xA001);
			} else {
				crc >>= 1;
			}
		}
	}
	return crc;
}

// Транзакция чтения углов
void bwm427_xfer_read(bwm427_xfer_t *x, uint8_t addr) {
	memset(x, 0, sizeof(*x));
	x->addr = addr;
	x->func = BWM427_FUNC_READ;
	x->reg = BWM427_REG_ANGLE;
	x->val = BWM427_REG_COUNT;
	x->result = BWM427_PENDING;
}

// Транзакция записи одного регистра
void bwm427_xfer_write(bwm427_xfer_t *x, uint8_t addr, uint16_t reg,
		uint16_t val) {
	memset(x, 0, sizeof(*x));
	x->addr = addr;
	x->func = BWM427_FUNC_WRITE;
	x->reg = reg;
	x->val = val;
	x->result = BWM427_PENDING;
}

// Сформировать запрос: адрес, функция, регистр, число/значение, CRC (младший байт первым)
void bwm427_build_request(const bwm427_xfer_t *x, uint8_t out[BWM427_REQ_LEN]) {
	out[0] = x->addr;
	out[1] = x->func;
	out[2] = (uint8_t) (x->reg >> 8);
	out[3] = (uint8_t) x->reg;
	out[4] = (uint8_t) (x->val >> 8);
	out[5] = (uint8_t) x->val;
	uint16_t crc = bwm427_crc16(out, 6);
	out[6] = (uint8_t) crc;
	out[7] = (uint8_t) (crc >> 8);
}

// Длина нормального (не исключения) ответа
uint16_t bwm427_reply_len(const bwm427_xfer_t *x) {
	if (x->func == BWM427_FUNC_READ) {
		return (uint16_t) BWM427_READ_LEN(x->val);
	}
	return BWM427_WRITE_LEN;
}

// Определить длину кадра по его началу (ответ может прийти кусками, если
// датчик делает паузы между байтами больше 1 символа)
int bwm427_frame_need(const bwm427_xfer_t *x, const uint8_t *buf,
		uint16_t len) {
	if (len < 2) {
		return 0;
	}
	if (buf[1] == (x->func | 0x80)) {
		return BWM427_EXC_LEN;
	}
	if (buf[1] != x->func) {
		return -1;
	}
	if (x->func == BWM427_FUNC_READ) {
		if (len < 3) {
			return 0;
		}
		if (buf[2] != 2u * x->val) {
			return -1; // не тот счётчик байт — не наш ответ
		}
		return 5 + buf[2];
	}
	return BWM427_WRITE_LEN;
}

// Проверить ответ: длина, CRC, адрес, функция, счётчик байт / эхо
bwm427_result_t bwm427_parse_reply(bwm427_xfer_t *x, const uint8_t *buf,
		uint16_t len) {
	if (len < BWM427_EXC_LEN) {
		return BWM427_BAD_FRAME;
	}
	uint16_t crc = (uint16_t) (buf[len - 2] | (buf[len - 1] << 8));
	if (bwm427_crc16(buf, (uint16_t) (len - 2)) != crc) {
		return BWM427_CRC;
	}
	if (buf[0] != x->addr && !(x->alt_addr != 0 && buf[0] == x->alt_addr)) {
		return BWM427_BAD_FRAME;
	}
	if (buf[1] == (x->func | 0x80)) {
		if (len != BWM427_EXC_LEN) {
			return BWM427_BAD_FRAME;
		}
		x->exc_code = buf[2];
		return BWM427_EXCEPTION;
	}
	if (buf[1] != x->func) {
		return BWM427_BAD_FRAME;
	}
	if (x->func == BWM427_FUNC_READ) {
		if (x->val > BWM427_REG_COUNT || buf[2] != 2u * x->val
				|| len != BWM427_READ_LEN(x->val)) {
			return BWM427_BAD_FRAME;
		}
		for (uint16_t i = 0; i < x->val; i++) {
			x->regs[i] = (uint16_t) ((buf[3 + 2 * i] << 8) | buf[4 + 2 * i]);
		}
		return BWM427_OK;
	}
	// Запись: ответ — эхо регистра и значения
	if (len != BWM427_WRITE_LEN || buf[2] != (uint8_t) (x->reg >> 8)
			|| buf[3] != (uint8_t) x->reg || buf[4] != (uint8_t) (x->val >> 8)
			|| buf[5] != (uint8_t) x->val) {
		return BWM427_BAD_FRAME;
	}
	return BWM427_OK;
}

// Перевести регистр угла в градусы: смещение 10000, шаг 0,01°.
// Формулу менять только здесь.
float bwm427_decode_angle(uint16_t reg) {
	return (float) ((int16_t) reg - 10000) / 100.0f;
}

// Углы из прочитанных регистров: regs[0] = X (0x0001), regs[1] = Y (0x0002)
void bwm427_decode_xy(const uint16_t *regs, float *x, float *y) {
	*x = bwm427_decode_angle(regs[0]);
	*y = bwm427_decode_angle(regs[1]);
}

// Медиана трёх значений
float bwm427_median3(float a, float b, float c) {
	float t;
	if (a > b) {
		t = a;
		a = b;
		b = t;
	}
	if (b > c) {
		b = c;
	}
	return (a > b) ? a : b;
}

// Экспоненциальное сглаживание (EMA)
float bwm427_ema(float old_value, float new_value, float alpha) {
	return (alpha * new_value) + ((1.0f - alpha) * old_value);
}

// Сбросить фильтр: следующий отсчёт заполнит медиану и EMA целиком
void bwm427_filter_reset(bwm427_filter_t *f) {
	f->init = false;
}

// Каскад: медиана-3 убирает одиночные выбросы, EMA сглаживает остальное
void bwm427_filter_step(bwm427_filter_t *f, float x, float y, float alpha,
		float *out_x, float *out_y) {
	if (!f->init) {
		f->mx[0] = f->mx[1] = f->mx[2] = x;
		f->my[0] = f->my[1] = f->my[2] = y;
		f->ex = x;
		f->ey = y;
		f->init = true;
	} else {
		f->mx[2] = f->mx[1];
		f->mx[1] = f->mx[0];
		f->mx[0] = x;
		f->my[2] = f->my[1];
		f->my[1] = f->my[0];
		f->my[0] = y;
		f->ex = bwm427_ema(f->ex, bwm427_median3(f->mx[0], f->mx[1], f->mx[2]),
				alpha);
		f->ey = bwm427_ema(f->ey, bwm427_median3(f->my[0], f->my[1], f->my[2]),
				alpha);
	}
	*out_x = f->ex;
	*out_y = f->ey;
}


/* ------------------------------------------------------------------------ */
/* Таймер шины: TIM5, 32 бита, 1 МГц (на МК; в host-тестах — имитация)      */
/* ------------------------------------------------------------------------ */

#ifndef BWM427_TIMER_FAKE

#define BUS_TIM       TIM5
// Приоритет прерывания TIM5: ниже USART1 (0) — приём не ждёт таймер, выше
// USB (5) и SysTick (15) — пауза не растягивается на время их обработчиков
#define BUS_TIM_PRIO  1u

void bwm427_port_init(void) {
	__HAL_RCC_TIM5_CLK_ENABLE();
	// Частота таймеров APB1: PCLK1, а при делителе APB1 больше 1 — вдвое выше
	uint32_t clk = HAL_RCC_GetPCLK1Freq();
	if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1) {
		clk *= 2u;
	}
	BUS_TIM->CR1 = 0u;
	BUS_TIM->DIER = 0u;
	BUS_TIM->CCMR1 = 0u;               // канал 1: сравнение без вывода, только флаг CC1IF
	BUS_TIM->PSC = clk / 1000000u - 1u;
	BUS_TIM->ARR = 0xFFFFFFFFu;
	BUS_TIM->CNT = 0u;
	BUS_TIM->EGR = TIM_EGR_UG;         // загрузить PSC сразу
	BUS_TIM->SR = 0u;
	BUS_TIM->CR1 = TIM_CR1_CEN;
	HAL_NVIC_SetPriority(TIM5_IRQn, BUS_TIM_PRIO, 0);
	HAL_NVIC_EnableIRQ(TIM5_IRQn);
}

uint32_t bwm427_port_us(void) {
	return BUS_TIM->CNT;
}

void bwm427_port_alarm(uint32_t at_us) {
	BUS_TIM->DIER &= ~TIM_DIER_CC1IE;
	BUS_TIM->CCR1 = at_us;
	BUS_TIM->SR = ~TIM_SR_CC1IF;       // rc_w0: сбросить только CC1IF
	BUS_TIM->DIER |= TIM_DIER_CC1IE;
	// Момент уже прошёл (или наступил, пока писали CCR1): совпадения не будет
	// до переполнения счётчика — запросить прерывание сразу
	if ((int32_t) (BUS_TIM->CNT - at_us) >= 0) {
		BUS_TIM->EGR = TIM_EGR_CC1G;
	}
}

void bwm427_port_alarm_off(void) {
	BUS_TIM->DIER &= ~TIM_DIER_CC1IE;
}

// Вектор TIM5 (в startup_*.s он слабый). TIM5 в CubeMX не включать: занят шиной.
void TIM5_IRQHandler(void) {
	if (BUS_TIM->SR & TIM_SR_CC1IF) {
		BUS_TIM->SR = ~TIM_SR_CC1IF;
		bwm427_alarm_isr();
	}
}

#endif /* BWM427_TIMER_FAKE */

/* ------------------------------------------------------------------------ */
/* Шина (прерывания)                                                        */
/* ------------------------------------------------------------------------ */

// Шина молчит дольше — не полагаться на разность в мкс (переполнение через 71 мин)
#define BUS_IDLE_LONG_MS  60000u

// MAX485 на передачу (DE = 1, RE = 1: приёмник отключён, эха нет)
static void rs485_tx(void) {
	HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(RS485_RE_GPIO_Port, RS485_RE_Pin, GPIO_PIN_SET);
}

// MAX485 на приём
static void rs485_rx(void) {
	HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(RS485_RE_GPIO_Port, RS485_RE_Pin, GPIO_PIN_RESET);
}

// Принимаем ли кадр, начинающийся с этого байта адреса
static bool addr_match(const bwm427_xfer_t *x, uint8_t a) {
	return a == x->addr || (x->alt_addr != 0 && a == x->alt_addr);
}

// Срок текущего состояния: прерывание таймера в момент at_us
static void bus_arm(uint32_t at_us) {
	s_deadline_us = at_us;
	bwm427_port_alarm(at_us);
}

// Пауза перед текущей транзакцией: gap от конца прошлой (s_mark_us, s_mark_ms).
// Если она уже прошла, прерывание таймера придёт сразу.
static void bus_wait_gap(void) {
	s_limit_ms = s_gap_ms;
	s_deadline_us = s_mark_us + s_gap_us;
	s_state = BUS_GAP;
	bwm427_port_alarm(s_deadline_us);
}

// Завершить текущую транзакцию и перейти к следующей (из прерываний)
static void bus_finish(bwm427_result_t res) {
	bwm427_xfer_t *x = &s_xfer[s_idx];
	uint32_t now = bwm427_port_us();
	x->result = res;
	x->t_ms = HAL_GetTick();
	x->dur_us = now - s_tx_us;
	s_mark_ms = x->t_ms;
	s_mark_us = now;
	if (++s_idx >= s_count) {
		bwm427_port_alarm_off();
		s_state = BUS_DONE;
	} else {
		bus_wait_gap();
	}
}

// Начать передачу запроса текущей транзакции (пауза истекла)
static void bus_start_tx(void) {
	bwm427_xfer_t *x = &s_xfer[s_idx];
	uint32_t now = bwm427_port_us();
	bwm427_build_request(x, s_tx);
	x->gap_us = now - s_mark_us;
	x->lat_first_us = 0;
	x->lat_done_us = 0;
	s_tx_us = now;
	s_rx_len = 0;
	s_mark_ms = HAL_GetTick();
	s_limit_ms = BWM427_TIMEOUT_MS;
	s_state = BUS_TX;
	bus_arm(now + BWM427_TIMEOUT_US); // TC должен прийти через 0,7 мс
	rs485_tx();
	if (HAL_UART_Transmit_IT(BUS_UART, s_tx, BWM427_REQ_LEN) != HAL_OK) {
		rs485_rx();
		bus_finish(BWM427_UART);
	}
}

// Начать (продолжить) приём want байт в s_rx с позиции s_rx_len
static void bus_receive(uint16_t want) {
	s_rx_want = want;
	if (want == 0 || s_rx_len + want > BWM427_RX_MAX
			|| HAL_UARTEx_ReceiveToIdle_IT(BUS_UART, &s_rx[s_rx_len], want)
					!= HAL_OK) {
		bus_finish(BWM427_UART);
	}
}

// Срок состояния st наступил: начать передачу или завершить транзакцию по
// таймауту. Только при запрещённых прерываниях.
static void bus_expire(bus_state_t st) {
	if (st == BUS_GAP) {
		bus_start_tx();
	} else if (st == BUS_TX) {
		HAL_UART_AbortTransmit_IT(BUS_UART);
		rs485_rx();
		bus_finish(BWM427_UART);
	} else if (st == BUS_RX) {
		HAL_UART_AbortReceive_IT(BUS_UART);
		bus_finish(s_rx_len ? BWM427_BAD_FRAME : BWM427_TIMEOUT);
	}
}

// Проверить срок по счётчику мкс. Прерывание USART1 (приоритет выше) могло как
// раз сменить состояние — поэтому проверка и действие при запрещённых
// прерываниях. Прерывание таймера раньше срока (старый флаг) — взвести заново.
static void bus_service(bool from_alarm) {
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	bus_state_t st = s_state;
	if (st == BUS_GAP || st == BUS_TX || st == BUS_RX) {
		if ((int32_t) (bwm427_port_us() - s_deadline_us) >= 0) {
			bus_expire(st);
		} else if (from_alarm) {
			bwm427_port_alarm(s_deadline_us);
		}
	}
	__set_PRIMASK(primask);
}

void bwm427_alarm_isr(void) {
	bus_service(true);
}

void bwm427_init(void) {
	rs485_rx();
	if (BUS_UART->Init.BaudRate != BWM427_BAUD) {
		BUS_UART->Init.BaudRate = BWM427_BAUD;
		if (HAL_UART_Init(BUS_UART) != HAL_OK) {
			Error_Handler();
		}
	}
	bwm427_port_init();
	bwm427_port_alarm_off();
	s_mark_ms = HAL_GetTick();
	s_mark_us = bwm427_port_us();
	s_state = BUS_IDLE;
}

void bwm427_set_gap_ms(uint16_t ms) {
	if (ms < BWM427_GAP_MS) {
		ms = BWM427_GAP_MS;
	}
	if (ms > BWM427_GAP_MAX_MS) {
		ms = BWM427_GAP_MAX_MS;
	}
	s_gap_ms = ms;
	s_gap_us = (uint32_t) ms * 1000u;
}

bool bwm427_start(bwm427_xfer_t *xfer, uint8_t n) {
	if (s_state != BUS_IDLE || n == 0 || n > BWM427_MAX_XFER) {
		return false;
	}
	for (uint8_t i = 0; i < n; i++) {
		xfer[i].result = BWM427_PENDING;
	}
	s_xfer = xfer;
	s_count = n;
	s_idx = 0;
	__DMB(); // запрос записан до того, как его увидят прерывания
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	if (HAL_GetTick() - s_mark_ms > BUS_IDLE_LONG_MS) {
		s_mark_us = bwm427_port_us() - s_gap_us; // пауза давно прошла
	}
	// Пауза уже прошла (суперцикл забрал прошлый пакет позже) — передача
	// начнётся в прерывании таймера сразу после разрешения прерываний
	bus_wait_gap();
	__set_PRIMASK(primask);
	return true;
}

bool bwm427_take(void) {
	if (s_state != BUS_DONE) {
		return false;
	}
	__DMB(); // результаты читаются после проверки состояния
	s_state = BUS_IDLE;
	return true;
}

bool bwm427_idle(void) {
	return s_state == BUS_IDLE;
}

uint32_t bwm427_fallbacks(void) {
	return s_fallbacks;
}

// Вызывается раз в 1 мс из SysTick_Handler
void bwm427_tick_1ms(void) {
	bus_state_t st = s_state;
	if (st == BUS_IDLE || st == BUS_DONE) {
		return;
	}
	// Срок по мкс уже прошёл (прерывание таймера опаздывает) — отработать здесь
	bus_service(false);
	// Запасной путь по миллисекундам: таймер не срабатывает или стоит
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	st = s_state;
	if ((st == BUS_GAP || st == BUS_TX || st == BUS_RX)
			&& HAL_GetTick() - s_mark_ms > s_limit_ms + BWM427_FALLBACK_MS) {
		s_fallbacks++;
		bus_expire(st);
	}
	__set_PRIMASK(primask);
}

// Запрос ушёл полностью (флаг TC): переключиться на приём
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart != BUS_UART || s_state != BUS_TX) {
		return;
	}
	rs485_rx();
	// Чтение SR, затем DR: сбросить ORE/FE/NE/IDLE и байт-мусор,
	// пойманный, пока приёмник MAX485 был отключён
	__HAL_UART_CLEAR_OREFLAG(huart);
	const bwm427_xfer_t *x = &s_xfer[s_idx];
	uint32_t to = x->timeout_us ? x->timeout_us : BWM427_TIMEOUT_US;
	s_tc_us = bwm427_port_us();
	s_rx_len = 0;
	s_mark_ms = HAL_GetTick();
	s_limit_ms = (to + 999u) / 1000u;
	s_state = BUS_RX;
	bus_arm(s_tc_us + to);
	bus_receive(bwm427_reply_len(x));
}

// Принята порция ответа: весь ожидаемый кадр или кусок до паузы на линии
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size) {
	if (huart != BUS_UART || s_state != BUS_RX) {
		return;
	}
	bwm427_xfer_t *x = &s_xfer[s_idx];
	// Конец порции: при приёме всех запрошенных байт — сейчас, при паузе на
	// линии (порция короче) — на символ раньше (флаг IDLE ставится через символ
	// тишины после последнего стоп-бита)
	uint32_t end_us = bwm427_port_us();
	if (size < s_rx_want) {
		end_us -= BWM427_CHARS_US(1);
	}
	uint16_t held = s_rx_len;
	uint16_t len = (uint16_t) (held + size);
	if (len > BWM427_RX_MAX) {
		len = BWM427_RX_MAX;
	}

	// Кадр начинается с адреса: отбросить помеху перед ним
	// (одиночный байт при переключении MAX485 на приём)
	uint16_t skip = 0;
	while (skip < len && !addr_match(x, s_rx[skip])) {
		skip++;
	}
	if (skip) {
		len = (uint16_t) (len - skip);
		memmove(s_rx, &s_rx[skip], len);
	}
	s_rx_len = len;
	// Кадр начался в этой порции: его len байт шли подряд до конца порции
	if (held == 0 && len > 0) {
		int32_t d = (int32_t) (end_us - BWM427_CHARS_US(len) - s_tc_us);
		x->lat_first_us = (d > 0) ? (uint32_t) d : 0u;
	}

	int need = bwm427_frame_need(x, s_rx, len);
	if (need < 0 || need > BWM427_RX_MAX) {
		bus_finish(BWM427_BAD_FRAME);
		return;
	}
	if (need == 0 || len < need) {
		// Кадр ещё не весь — дочитать (таймаут транзакции продолжает идти)
		uint16_t total = need ? (uint16_t) need : bwm427_reply_len(x);
		bus_receive((uint16_t) (total - len));
		return;
	}
	x->lat_done_us = end_us - s_tc_us;
	bus_finish(bwm427_parse_reply(x, s_rx, (uint16_t) need));
}

// Ошибка UART. FE/NE: HAL продолжает приём — кадр всё равно проверит CRC.
// ORE: HAL сам остановил приём — транзакция не удалась.
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
	if (huart != BUS_UART) {
		return;
	}
	if (s_state == BUS_RX && huart->RxState == HAL_UART_STATE_READY) {
		bus_finish(BWM427_UART);
	}
}
