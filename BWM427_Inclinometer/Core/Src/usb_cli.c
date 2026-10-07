/*
 * usb_cli.c — командная строка по USB CDC: диагностика, настройки (set) и
 * переход в загрузчик.
 *
 * Приём. Прерывание OTG_FS (CDC_Receive_FS -> usb_cli_rx_isr) кладёт байты в
 * кольцо s_rx_*: пишет только прерывание (head), читает только суперцикл
 * (tail), поэтому блокировки не нужны. Строки разбираются в usb_cli_task(),
 * не больше одной команды за вызов.
 *
 * Передача. Ответы копятся в кольце s_tx_* и уходят в CDC прямо из него
 * (CDC_Transmit_FS с указателем на непрерывный кусок кольца). Кусок «в полёте»
 * не перезаписывается, пока CDC не сообщит о завершении (TxState = 0). Если
 * хоста нет (устройство не CONFIGURED), порт не открыт или хост не забирает
 * данные дольше CLI_TX_STALL_MS — вывод выбрасывается: суперцикл не ждёт USB.
 *
 * Переход в загрузчик. Команда boot ставит метку в RAM-секции .noinit и делает
 * NVIC_SystemReset(). usb_cli_boot_check() в самом начале main() видит метку,
 * стирает её и прыгает в системную память 0x1FFF0000 — так же, как при старте
 * с BOOT0 = 1 (USB DFU 0483:DF11).
 *
 * Аварийный путь. Если суперцикл не вызывал usb_cli_task() дольше CLI_STALL_MS
 * (завис, долго инициализируется), строки boot/dfu/reset выполняются прямо в
 * прерывании, без ответа — чтобы перепрошить можно было и зависшую прошивку.
 */
#include "usb_cli.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "app.h"
#include "bwm427.h"
#include "sd_logger.h"
#include "settings.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "version.h"

extern TIM_HandleTypeDef htim3;

#define CLI_RX_SIZE         256u    // кольцо приёма, степень двойки
#define CLI_TX_SIZE         4096u   // кольцо передачи, степень двойки (diag ~1,5 КБ — целиком)
#define CLI_LINE_MAX        64u     // длина командной строки
#define CLI_OUT_MAX         192u    // длина одной строки ответа
#define CLI_STREAM_MAX      320u    // длина строки потока stream
#define CLI_TX_CHUNK_MAX    512u    // байт за одну передачу CDC
#define CLI_TX_STALL_MS     200u    // хост дольше не забирает данные — очередь сбросить
#define CLI_STALL_MS        3000u   // суперцикл стоит дольше — аварийный путь в прерывании
#define CLI_STREAM_MIN_MS   50u
#define CLI_STREAM_MAX_MS   60000u
#define CLI_FLUSH_MS        500u    // boot/reset: ждать отправки ответа не дольше
#define CLI_REBOOT_DELAY_MS 200u    // ... и ещё столько, чтобы хост успел закрыть порт
#define CLI_DETACH_MS       20u     // пауза после отключения от шины перед сбросом
#define CLI_FX_SLOTS        8u      // буферы fx(): на одну строку вывода

#define CLI_BOOT_MAGIC      0xB007DF11u
#define CLI_SYSMEM_ADDR     0x1FFF0000u // системная память (загрузчик) STM32F411

_Static_assert((CLI_RX_SIZE & (CLI_RX_SIZE - 1u)) == 0u, "CLI_RX_SIZE: степень двойки");
_Static_assert((CLI_TX_SIZE & (CLI_TX_SIZE - 1u)) == 0u, "CLI_TX_SIZE: степень двойки");

// Метка перехода в загрузчик. Секция .noinit (NOLOAD, см. STM32F411CEUX_*.ld):
// стартовый код её не обнуляет, значение переживает NVIC_SystemReset().
// Два слова (метка и её инверсия), чтобы мусор в RAM после включения питания
// случайно не совпал.
static volatile uint32_t s_boot_magic[2] __attribute__((section(".noinit")));

// --- Приём (прерывание -> суперцикл) ---
static uint8_t s_rx_buf[CLI_RX_SIZE];
static volatile uint32_t s_rx_head;      // пишет только прерывание
static volatile uint32_t s_rx_tail;      // пишет только суперцикл
static volatile uint32_t s_rx_dropped;   // байт не влезло в кольцо
static volatile bool s_dtr;              // хост держит DTR (порт открыт)
static volatile bool s_evt_open;         // DTR 0 -> 1
static volatile bool s_evt_close;        // DTR 1 -> 0
static volatile uint32_t s_last_task_ms; // HAL_GetTick() последнего usb_cli_task()

// Аварийный разбор строки в прерывании
static char s_isr_line[12];
static uint8_t s_isr_len;

// --- Текущая командная строка ---
static char s_line[CLI_LINE_MAX + 1u];
static uint8_t s_line_len;
static bool s_line_overflow;

// --- Передача (только суперцикл) ---
static uint8_t s_tx_buf[CLI_TX_SIZE];
static uint32_t s_tx_head;       // счётчики без заворота, индекс = счётчик & (SIZE - 1)
static uint32_t s_tx_tail;       // начало неподтверждённых данных (с куском «в полёте»)
static uint32_t s_tx_inflight;   // байт отдано в CDC, завершения ещё не было
static uint32_t s_tx_start_ms;   // когда отдан кусок «в полёте»
static uint32_t s_tx_dropped;    // байт выброшено (кольцо полно)
static bool s_tx_stalled;        // хост не забирает данные: вывод выбрасывается

// --- Поток stream ---
static uint16_t s_stream_ms;     // 0 = выключен
static uint32_t s_stream_last_ms;

/* ----------------------------------------------------------------------------
 * Мелочи
 * ------------------------------------------------------------------------- */

static char to_lower(char c) {
	return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

// Строка начинается со слова w, за которым конец строки или пробел
static bool word_is(const char *line, const char *w) {
	size_t n = strlen(w);
	return strncmp(line, w, n) == 0 && (line[n] == '\0' || line[n] == ' ');
}

// Десятичное число без знака, вся строка (до 9 цифр)
static bool parse_u32(const char *s, uint32_t *out) {
	uint32_t v = 0u;
	uint8_t digits = 0u;
	for (; *s != '\0'; s++) {
		if (*s < '0' || *s > '9' || digits >= 9u) {
			return false;
		}
		v = v * 10u + (uint32_t) (*s - '0');
		digits++;
	}
	*out = v;
	return digits > 0u;
}

// Десятичная дробь без знака: "0.15", ".2", "1" (до 4 знаков после точки)
static bool parse_decimal(const char *s, float *out) {
	uint32_t ip = 0u, fp = 0u, scale = 1u;
	uint8_t digits = 0u;
	for (; *s >= '0' && *s <= '9'; s++) {
		if (digits >= 6u) {
			return false;
		}
		ip = ip * 10u + (uint32_t) (*s - '0');
		digits++;
	}
	if (*s == '.') {
		for (s++; *s >= '0' && *s <= '9'; s++) {
			if (scale >= 10000u) {
				return false;
			}
			fp = fp * 10u + (uint32_t) (*s - '0');
			scale *= 10u;
			digits++;
		}
	}
	if (*s != '\0' || digits == 0u) {
		return false;
	}
	*out = (float) ip + (float) fp / (float) scale;
	return true;
}

// Число с фиксированной точкой (dec знаков после точки) без "%f": тот в newlib
// берёт память через malloc. Результат живёт до следующих CLI_FX_SLOTS вызовов.
static const char *fx(float v, uint8_t dec) {
	static char slots[CLI_FX_SLOTS][16];
	static uint8_t next;
	char *out = slots[next];
	next = (uint8_t) ((next + 1u) % CLI_FX_SLOTS);

	if (v != v) {
		strcpy(out, "nan");
		return out;
	}
	uint32_t scale = 1u;
	for (uint8_t i = 0u; i < dec; i++) {
		scale *= 10u;
	}
	float a = (v < 0.0f) ? -v : v;
	if (a >= 2.0e9f / (float) scale) {
		strcpy(out, (v < 0.0f) ? "-ovf" : "ovf");
		return out;
	}
	uint32_t q = (uint32_t) (a * (float) scale + 0.5f);
	const char *sign = (v < 0.0f && q != 0u) ? "-" : "";
	if (dec == 0u) {
		snprintf(out, sizeof(slots[0]), "%s%lu", sign, (unsigned long) q);
	} else {
		snprintf(out, sizeof(slots[0]), "%s%lu.%0*lu", sign, (unsigned long) (q / scale),
				(int) dec, (unsigned long) (q % scale));
	}
	return out;
}

// Дописать в buf[*pos..size) по формату; при нехватке места строка обрезается
static void buf_add(char *buf, size_t size, size_t *pos, const char *fmt, ...)
		__attribute__((format(printf, 4, 5)));
static void buf_add(char *buf, size_t size, size_t *pos, const char *fmt, ...) {
	if (*pos + 1u >= size) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf + *pos, size - *pos, fmt, ap);
	va_end(ap);
	if (n > 0) {
		*pos += (size_t) n;
		if (*pos > size - 1u) {
			*pos = size - 1u;
		}
	}
}

static const char *sensor_status_str(sensor_status_t st) {
	switch (st) {
	case SENSOR_ABSENT: return "ABSENT";
	case SENSOR_OK:     return "OK";
	case SENSOR_LOST:   return "LOST";
	default:            return "?";
	}
}

static const char *sd_state_str(sd_state_t st) {
	switch (st) {
	case SD_NO_CARD:   return "NO_CARD";
	case SD_READY:     return "READY";
	case SD_RECORDING: return "RECORDING";
	case SD_ERROR:     return "ERROR";
	default:           return "?";
	}
}

static const char *theme_str(uint8_t theme) {
	return theme == APP_THEME_LIGHT ? "light" : "dark";
}

static const char *svc_state_str(svc_state_t st) {
	switch (st) {
	case SVC_IDLE: return "IDLE";
	case SVC_BUSY: return "BUSY";
	case SVC_OK:   return "OK";
	case SVC_FAIL: return "FAIL";
	default:       return "?";
	}
}

// g_app.sensor[i].last_err — bwm427_result_t, 0 = ошибок не было
static const char *sensor_err_str(uint8_t e) {
	switch ((bwm427_result_t) e) {
	case BWM427_OK:        return "-";
	case BWM427_TIMEOUT:   return "TIMEOUT";
	case BWM427_CRC:       return "CRC";
	case BWM427_BAD_FRAME: return "BAD_FRAME";
	case BWM427_EXCEPTION: return "EXCEPTION";
	case BWM427_UART:      return "UART";
	case BWM427_PENDING:   return "PENDING";
	default:               return "?";
	}
}

/* ----------------------------------------------------------------------------
 * Передача
 * ------------------------------------------------------------------------- */

// Хост сконфигурировал устройство и забирает данные
static bool cli_port_ready(void) {
	return hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && !s_tx_stalled;
}

// Выбросить ещё не отданное в CDC (кусок «в полёте» не трогаем)
static void cli_tx_drop_queued(void) {
	s_tx_head = s_tx_tail + s_tx_inflight;
}

static void cli_write(const char *s, uint32_t n) {
	if (!cli_port_ready()) {
		return; // слушать некому — не копим
	}
	if (n > CLI_TX_SIZE - (s_tx_head - s_tx_tail)) {
		s_tx_dropped += n; // строку целиком или никак
		return;
	}
	for (uint32_t i = 0u; i < n; i++) {
		s_tx_buf[(s_tx_head + i) & (CLI_TX_SIZE - 1u)] = (uint8_t) s[i];
	}
	s_tx_head += n;
}

// Строка ответа по формату + "\r\n"
static void cli_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void cli_line(const char *fmt, ...) {
	if (!cli_port_ready()) {
		return;
	}
	char buf[CLI_OUT_MAX];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf) - 2u, fmt, ap);
	va_end(ap);
	if (n < 0) {
		return;
	}
	if ((size_t) n > sizeof(buf) - 3u) {
		n = (int) (sizeof(buf) - 3u); // обрезано vsnprintf
	}
	buf[n++] = '\r';
	buf[n++] = '\n';
	cli_write(buf, (uint32_t) n);
}

// Отдать накопленное в CDC. Не ждёт: без хоста или при занятом CDC — выход.
static void cli_tx_pump(uint32_t now) {
	uint8_t state = hUsbDeviceFS.dev_state;
	USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *) hUsbDeviceFS.pClassData;

	if (state == USBD_STATE_SUSPENDED) {
		// Хост спит или кабель вынут: кусок «в полёте» дошлётся после Resume,
		// остальное выбросить
		cli_tx_drop_queued();
		return;
	}
	if (state != USBD_STATE_CONFIGURED || hcdc == NULL) {
		// Не сконфигурировано (нет хоста, сброс шины): передач нет
		s_tx_tail = s_tx_head;
		s_tx_inflight = 0u;
		s_tx_stalled = false;
		return;
	}

	if (s_tx_inflight != 0u) {
		if (hcdc->TxState != 0u) {
			// Порт не открыт или хост не читает: не копить, выбрасывать
			if (!s_tx_stalled && (now - s_tx_start_ms) > CLI_TX_STALL_MS) {
				s_tx_stalled = true;
				cli_tx_drop_queued();
			}
			return;
		}
		s_tx_tail += s_tx_inflight; // хост забрал
		s_tx_inflight = 0u;
		s_tx_stalled = false;
	}

	uint32_t used = s_tx_head - s_tx_tail;
	if (used == 0u) {
		return;
	}
	uint32_t start = s_tx_tail & (CLI_TX_SIZE - 1u);
	uint32_t len = CLI_TX_SIZE - start; // до конца кольца
	if (len > used) {
		len = used;
	}
	if (len > CLI_TX_CHUNK_MAX) {
		len = CLI_TX_CHUNK_MAX;
	}
	if (CDC_Transmit_FS(&s_tx_buf[start], (uint16_t) len) == USBD_OK) {
		s_tx_inflight = len;
		s_tx_start_ms = now;
	}
}

/* ----------------------------------------------------------------------------
 * Перезагрузка
 * ------------------------------------------------------------------------- */

static void cli_set_boot_magic(void) {
	s_boot_magic[0] = CLI_BOOT_MAGIC;
	s_boot_magic[1] = ~CLI_BOOT_MAGIC;
	__DSB();
}

// Дослать ответ, отключиться от шины и перезагрузиться (в загрузчик или в
// прошивку). Блокирует суперцикл примерно на 0,3..0,8 с — прибор всё равно
// перезапускается.
static void cli_reboot(bool to_bootloader) __attribute__((noreturn));
static void cli_reboot(bool to_bootloader) {
	uint32_t t0 = HAL_GetTick();
	while ((s_tx_head != s_tx_tail) && (HAL_GetTick() - t0) < CLI_FLUSH_MS) {
		cli_tx_pump(HAL_GetTick());
	}
	HAL_Delay(CLI_REBOOT_DELAY_MS);
	// Программное отключение (снять подтяжку D+): хост сразу видит уход
	// устройства, затем загрузчик подключится заново как 0483:DF11
	(void) USBD_Stop(&hUsbDeviceFS);
	HAL_Delay(CLI_DETACH_MS);
	if (to_bootloader) {
		cli_set_boot_magic();
	}
	NVIC_SystemReset();
}

// Прошивку запустил не сброс, а переход из системного загрузчика (dfu-util
// ... :leave): по адресу 0 всё ещё отображена системная память (VTOR = 0 —
// прерывания ушли бы в векторы загрузчика, HAL_Delay() завис бы), а
// тактирование может быть настроено загрузчиком (SystemClock_Config() не
// пройдёт). После настоящего сброса ядро всегда на HSI, а при BOOT0 = 0 по
// адресу 0 — основная Flash.
static bool started_by_bootloader(void) {
	__HAL_RCC_SYSCFG_CLK_ENABLE(); // без тактирования регистр читался бы как 0
	uint32_t mem_mode = SYSCFG->MEMRMP & SYSCFG_MEMRMP_MEM_MODE;
	return (RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI
			|| mem_mode == SYSCFG_MEMRMP_MEM_MODE_0; // 01 = системная память
}

void usb_cli_boot_check(void) {
	if (s_boot_magic[0] != CLI_BOOT_MAGIC || s_boot_magic[1] != ~CLI_BOOT_MAGIC) {
		if (started_by_bootloader()) {
			// Чистый старт: сброс вернёт RCC, SYSCFG и VTOR в исходное состояние
			// (с BOOT0 = 1 плата снова окажется в загрузчике — как и без этого)
			NVIC_SystemReset();
		}
		return;
	}
	// Один раз: после выхода из загрузчика (dfu-util ... :leave) — обычный старт
	s_boot_magic[0] = 0u;
	s_boot_magic[1] = 0u;

	// Сюда попадаем сразу после сброса: HAL не запущен, ядро на HSI 16 МГц,
	// периферия в исходном состоянии — почти как при старте с BOOT0 = 1.
	__disable_irq();
	SysTick->CTRL = 0u;
	SysTick->LOAD = 0u;
	SysTick->VAL = 0u;
	for (uint32_t i = 0u; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); i++) {
		NVIC->ICER[i] = 0xFFFFFFFFu;
		NVIC->ICPR[i] = 0xFFFFFFFFu;
	}
	__HAL_RCC_SYSCFG_CLK_ENABLE();
	__HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH(); // по адресу 0x00000000 — системная память
	SCB->VTOR = 0u;                         // векторы загрузчика (через псевдоним 0)
	__DSB();
	__ISB();

	uint32_t sp = *(const volatile uint32_t *) CLI_SYSMEM_ADDR;
	uint32_t pc = *(const volatile uint32_t *) (CLI_SYSMEM_ADDR + 4u);
	// Загрузчик сам PRIMASK не снимает: с запрещёнными прерываниями USB DFU не
	// заработает. Все источники выше выключены, так что сработать нечему.
	__enable_irq();
	// sp и pc уже в регистрах: после смены MSP стек функции не используется
	__asm volatile (
			"msr msp, %0\n"
			"bx  %1\n"
			: : "r" (sp), "r" (pc) : "memory");
	for (;;) {
	}
}

/* ----------------------------------------------------------------------------
 * Команды
 * ------------------------------------------------------------------------- */

static void cmd_help(void) {
	cli_line("BWM427 USB CLI, commands (case-insensitive, end with CR/LF):");
	cli_line("  ver            firmware version, build date/time, HAL, chip UID");
	cli_line("  diag           one-shot status dump");
	cli_line("  diag reset     zero bus statistics (reply latency, timeouts, CRC, cycle)");
	cli_line("  stream N       status line 'S,...' every N ms (0 = off, %u..%u)",
			(unsigned) CLI_STREAM_MIN_MS, (unsigned) CLI_STREAM_MAX_MS);
	cli_line("  set freq N     poll/log rate, %u..%u Hz", (unsigned) APP_FREQ_MIN_HZ,
			(unsigned) APP_FREQ_MAX_HZ);
	cli_line("  set alpha X    EMA filter alpha, 0.01..0.99");
	cli_line("  set gap N      RS485 bus silence before each request, %u..%u ms",
			(unsigned) BWM427_GAP_MS, (unsigned) BWM427_GAP_MAX_MS);
	cli_line("  set theme dark|light   UI theme");
	cli_line("  set batalarm X battery low alarm below X V, %u..%u (default %s)",
			(unsigned) APP_BAT_ALARM_MIN_V, (unsigned) APP_BAT_ALARM_MAX_V,
			fx(APP_BAT_ALARM_DEFAULT_V, 1));
	cli_line("  set rollwin N  roll window, %u..%u s (default %u)",
			(unsigned) APP_ROLL_WIN_MIN_S, (unsigned) APP_ROLL_WIN_MAX_S,
			(unsigned) APP_ROLL_WIN_DEF_S);
	cli_line("  set rollhz N   roll samples per second, %u..%u (default %u)",
			(unsigned) APP_ROLL_RATE_MIN_HZ, (unsigned) APP_ROLL_RATE_MAX_HZ,
			(unsigned) APP_ROLL_RATE_DEF_HZ);
	cli_line("  set rollcalm X calm threshold, deg (default %s)", fx(APP_ROLL_CALM_DEF_DEG, 2));
	cli_line("  set rollhyst X calm hysteresis, deg (default %s)", fx(APP_ROLL_HYST_DEF_DEG, 2));
	cli_line("                 settings go to flash %u s after the last change",
			(unsigned) (SETTINGS_SAVE_DELAY_MS / 1000u));
	cli_line("                 (not while recording: after it stops)");
	cli_line("  boot | dfu     reboot into STM32 system bootloader (USB DFU 0483:DF11)");
	cli_line("  boot force     ... even while recording to SD");
	cli_line("  reset [force]  reboot the firmware");
	usb_cli_ext_help();
	cli_line("  help");
	cli_line("OK");
}

static void cmd_ver(void) {
	const volatile uint32_t *uid = (const volatile uint32_t *) UID_BASE;
	uint32_t hal = HAL_GetHalVersion();
	cli_line("BWM427 inclinometer firmware v" FW_VERSION ", build " __DATE__ " " __TIME__);
	cli_line("HAL %lu.%lu.%lu, SYSCLK %lu MHz, UID %08lX%08lX%08lX",
			(unsigned long) (hal >> 24), (unsigned long) ((hal >> 16) & 0xFFu),
			(unsigned long) ((hal >> 8) & 0xFFu),
			(unsigned long) (SystemCoreClock / 1000000u),
			(unsigned long) uid[2], (unsigned long) uid[1], (unsigned long) uid[0]);
	cli_line("OK");
}

// Имена файлов текущего (идёт запись) или следующего замера
static void cli_diag_files(void) {
	char buf[CLI_OUT_MAX];
	size_t pos = 0u;
	bool rec = (g_app.sd_state == SD_RECORDING);
	buf_add(buf, sizeof(buf), &pos, "sd %s:", rec ? "files" : "next files");
	bool any = false;
	for (uint8_t i = 0u; i < APP_SENSOR_COUNT; i++) {
		char name[SD_NAME_LEN];
		if (!sd_logger_file_name(i, name)) {
			continue;
		}
		const char *mark = "";
		if (rec) {
			mark = (g_app.rec_sensor_mask & (1u << i)) ? " (open)" : " (no sensor yet)";
		}
		buf_add(buf, sizeof(buf), &pos, "%s %s%s", any ? "," : "", name, mark);
		any = true;
	}
	if (!any) {
		buf_add(buf, sizeof(buf), &pos, " - (no card or numbers exhausted)");
	}
	cli_line("%s", buf);
}

// Хранилище настроек во флеше (settings.c)
static void cli_diag_settings(void) {
	const settings_info_t *si = settings_get_info();
	char buf[CLI_OUT_MAX];
	size_t pos = 0u;
	buf_add(buf, sizeof(buf), &pos,
			"settings %s, flash records %u/%u, seq %lu, saves %lu, erases %lu",
			si->loaded ? "loaded from flash" : "defaults (no flash record)",
			(unsigned) si->used, (unsigned) SETTINGS_CAPACITY, (unsigned long) si->seq,
			(unsigned long) si->saves, (unsigned long) si->erases);
	if (si->pending) {
		buf_add(buf, sizeof(buf), &pos, ", save pending%s",
				g_app.sd_state == SD_RECORDING ? " (after recording)" : "");
	}
	if (si->last_err != SETTINGS_ERR_NONE) {
		buf_add(buf, sizeof(buf), &pos, ", last error %s",
				si->last_err == SETTINGS_ERR_ERASE ? "ERASE" : "WRITE");
	}
	cli_line("%s", buf);
}

// Батарея: напряжение, наличие, заряд, тревога
static void cli_diag_battery(void) {
	char pct[8];
	if (g_app.battery_pct == APP_BAT_PCT_NONE) {
		strcpy(pct, "-");
	} else {
		snprintf(pct, sizeof(pct), "%u%%", (unsigned) g_app.battery_pct);
	}
	cli_line("battery %s V, present=%u low=%u pct=%s alarm_below=%s V (hyst %s V)%s",
			fx(g_app.battery_v, 2), g_app.battery_present ? 1u : 0u,
			g_app.battery_low ? 1u : 0u, pct, fx(g_app.bat_alarm_v, 2),
			fx(APP_BAT_ALARM_HYST_V, 2),
			g_app.battery_adc_sat ? ", ADC SATURATED (V is a lower bound)" : "");
}

// Тайминги шины RS485 (с включения или с diag reset), мкс
static void cli_diag_bus(void) {
	for (uint8_t i = 0u; i < APP_SENSOR_COUNT; i++) {
		const app_sensor_t *s = &g_app.sensor[i];
		const app_stat_t *f = &s->lat_first, *e = &s->lat_done;
		unsigned a = APP_SENSOR_ADDR(i);
		cli_line("reply D%u n_ok=%lu timeout=%lu crc=%lu bad_frame=%lu timeout_us=%lu", a,
				(unsigned long) f->n, (unsigned long) s->timeout_count,
				(unsigned long) s->crc_count, (unsigned long) s->frame_count,
				(unsigned long) s->timeout_us);
		cli_line("reply D%u first_us last=%lu avg=%lu min=%lu max=%lu"
				" done_us last=%lu avg=%lu min=%lu max=%lu", a,
				(unsigned long) f->last_us, (unsigned long) app_stat_avg_us(f),
				(unsigned long) f->min_us, (unsigned long) f->max_us,
				(unsigned long) e->last_us, (unsigned long) app_stat_avg_us(e),
				(unsigned long) e->min_us, (unsigned long) e->max_us);
	}
	const app_bus_t *b = &g_app.bus;
	cli_line("bus gap_us n=%lu avg=%lu min=%lu max=%lu (silence between requests of a cycle)",
			(unsigned long) b->gap.n, (unsigned long) app_stat_avg_us(&b->gap),
			(unsigned long) b->gap.min_us, (unsigned long) b->gap.max_us);
	cli_line("bus idle_us n=%lu avg=%lu max=%lu (bus idle before a cycle beyond the gap)",
			(unsigned long) b->idle.n, (unsigned long) app_stat_avg_us(&b->idle),
			(unsigned long) b->idle.max_us);
	uint32_t cyc = app_stat_avg_us(&b->cycle);
	cli_line("bus cycle_us n=%lu last=%lu avg=%lu min=%lu max=%lu max_rate=%s Hz",
			(unsigned long) b->cycle.n, (unsigned long) b->cycle.last_us,
			(unsigned long) cyc, (unsigned long) b->cycle.min_us,
			(unsigned long) b->cycle.max_us, fx(cyc ? 1.0e6f / (float) cyc : 0.0f, 2));
	cli_line("bus timer_fallbacks=%lu", (unsigned long) bwm427_fallbacks());
}

static void cmd_diag(const char *arg) {
	if (word_is(arg, "reset")) {
		app_bus_stats_reset();
		cli_line("OK bus stats reset");
		return;
	}
	if (*arg != '\0') {
		cli_line("ERR usage: diag [reset]");
		return;
	}
	uint32_t now = HAL_GetTick();
	const app_diag_t *d = &g_app.diag;

	cli_line("BWM427 diag");
	cli_line("uptime %lu s, tick %lu ms", (unsigned long) d->uptime_s, (unsigned long) now);
	cli_line("loop %u/s, max %u ms (app %u ms, ui %u ms)", (unsigned) d->loops_per_s,
			(unsigned) d->loop_max_ms, (unsigned) d->app_max_ms, (unsigned) d->ui_max_ms);
	float cyc_per_us = (float) SystemCoreClock / 1.0e6f;
	cli_line("cpu load=%u%% idle_pass=%s us min_pass=%s us (load = 1 - passes * idle_pass / 1 s)",
			(unsigned) d->cpu_load_pct, fx((float) d->idle_pass_cyc / cyc_per_us, 2),
			fx((float) d->pass_min_cyc / cyc_per_us, 2));
	for (uint8_t i = 0u; i < APP_SENSOR_COUNT; i++) {
		const app_sensor_t *s = &g_app.sensor[i];
		char since[16];
		if (s->ok_count == 0u) {
			strcpy(since, "never");
		} else {
			snprintf(since, sizeof(since), "%lu ms", (unsigned long) (now - s->last_ok_ms));
		}
		cli_line("sensor D%u addr=%u %s x=%s y=%s ok=%lu err=%lu garbled=%lu last_err=%s"
				" since_ok=%s", (unsigned) APP_SENSOR_ADDR(i), (unsigned) s->addr,
				sensor_status_str(s->status), fx(s->x, 3), fx(s->y, 3),
				(unsigned long) s->ok_count, (unsigned long) s->err_count,
				(unsigned long) s->garbled_count, sensor_err_str(s->last_err), since);
		// Качка по осям: размах за окно, покой по X,Y (1/0), накоплено окна
		cli_line("  roll x=%s y=%s deg calm=%u,%u fill=%u/%u s", fx(s->roll_x, 3),
				fx(s->roll_y, 3), s->calm_x ? 1u : 0u, s->calm_y ? 1u : 0u,
				(unsigned) s->roll_fill_s, (unsigned) g_app.roll_window_s);
	}
	cli_line("rate actual=%s Hz, log_freq=%u Hz, ema_alpha=%s", fx(g_app.actual_rate_hz, 1),
			(unsigned) g_app.log_freq_hz, fx(g_app.ema_alpha, 2));
	cli_line("sd %s err=%u file=%u rec_mask=0x%02X rows=%lu rec_time=%lu s",
			sd_state_str(g_app.sd_state), (unsigned) g_app.sd_err,
			(unsigned) g_app.file_number, (unsigned) g_app.rec_sensor_mask,
			(unsigned long) g_app.rec_rows,
			(unsigned long) (g_app.sd_state == SD_RECORDING ?
					(now - g_app.rec_start_ms) / 1000u : 0u));
	if (g_app.sd_free_mb != APP_SD_FREE_UNKNOWN) {
		cli_line("sd space total=%lu MB free=%lu MB", (unsigned long) g_app.sd_total_mb,
				(unsigned long) g_app.sd_free_mb);
	} else {
		cli_line("sd space total=%lu MB free=unknown", (unsigned long) g_app.sd_total_mb);
	}
	cli_diag_files();
	cli_diag_battery();
	const app_time_t *t = &g_app.time;
	cli_line("time 20%02u-%02u-%02u %02u:%02u:%02u, rtc %s", (unsigned) t->year,
			(unsigned) t->month, (unsigned) t->date, (unsigned) t->hours,
			(unsigned) t->minutes, (unsigned) t->seconds, g_app.rtc_present ? "yes" : "no");
	cli_line("rec_switch %s", g_app.rec_switch_on ? "ON" : "OFF");
	if (g_app.stab_sensor < APP_SENSOR_COUNT) {
		cli_line("stability %s, span %s deg, ref sensor D%u %c, fill=%u/%u s",
				g_app.is_stable ? "STABLE" : "UNSTABLE", fx(g_app.stab_span, 3),
				(unsigned) APP_SENSOR_ADDR(g_app.stab_sensor), g_app.stab_axis ? 'Y' : 'X',
				(unsigned) g_app.stab_fill_s, (unsigned) g_app.roll_window_s);
	} else {
		cli_line("stability %s, span %s deg, ref sensor none, fill=%u/%u s",
				g_app.is_stable ? "STABLE" : "UNSTABLE", fx(g_app.stab_span, 3),
				(unsigned) g_app.stab_fill_s, (unsigned) g_app.roll_window_s);
	}
	cli_line("bus gap %u ms", (unsigned) g_app.bus_gap_ms);
	cli_diag_bus();
	cli_line("theme %s", theme_str(g_app.theme));
	cli_diag_settings();
	// Энкодер: сырой счётчик TIM3 (4 импульса на щелчок) и кнопка PA0 (0 = нажата)
	cli_line("encoder tim3=%u btn_pin=%u", (unsigned) __HAL_TIM_GET_COUNTER(&htim3),
			(unsigned) HAL_GPIO_ReadPin(ENCODER_SW_GPIO_Port, ENCODER_SW_Pin));
	cli_line("svc %s", svc_state_str(g_app.svc_state));
	cli_line("usb dtr=%u rx_drop=%lu tx_drop=%lu stream=%u ms", s_dtr ? 1u : 0u,
			(unsigned long) s_rx_dropped, (unsigned long) s_tx_dropped,
			(unsigned) s_stream_ms);
	cli_line("OK");
}

// Строка потока: S,t_ms,loop_ms,{st,x,y,err} по датчикам,sd,rate_hz,file,gap_ms,
// дальше (добавлено в конец): cpu,bat_v,bat_low, по датчикам {ok,to,bad,lat,lmin,
// lavg,lmax} (с diag reset: удачные ответы, таймауты, CRC+чужие кадры, задержка
// ответа до первого байта, мкс: последняя, мин., ср., макс.), cyc_us,idle_us
// (последний цикл без ошибок на шине и простой шины перед последним циклом, мкс)
static void stream_emit(uint32_t now) {
	char buf[CLI_STREAM_MAX];
	size_t pos = 0u;
	buf_add(buf, sizeof(buf) - 2u, &pos, "S,%lu,%u", (unsigned long) now,
			(unsigned) g_app.diag.loop_max_ms);
	for (uint8_t i = 0u; i < APP_SENSOR_COUNT; i++) {
		const app_sensor_t *s = &g_app.sensor[i];
		char st = (s->status == SENSOR_OK) ? 'O' : (s->status == SENSOR_LOST) ? 'L' : 'A';
		buf_add(buf, sizeof(buf) - 2u, &pos, ",%c,%s,%s,%lu", st, fx(s->x, 3), fx(s->y, 3),
				(unsigned long) s->err_count);
	}
	static const char *const sd_short[] = { "NOCARD", "READY", "REC", "ERR" };
	buf_add(buf, sizeof(buf) - 2u, &pos, ",%s,%s,%u,%u",
			(unsigned) g_app.sd_state < 4u ? sd_short[g_app.sd_state] : "?",
			fx(g_app.actual_rate_hz, 1), (unsigned) g_app.file_number,
			(unsigned) g_app.bus_gap_ms);
	buf_add(buf, sizeof(buf) - 2u, &pos, ",%u,%s,%u", (unsigned) g_app.diag.cpu_load_pct,
			fx(g_app.battery_v, 2), g_app.battery_low ? 1u : 0u);
	for (uint8_t i = 0u; i < APP_SENSOR_COUNT; i++) {
		const app_sensor_t *s = &g_app.sensor[i];
		const app_stat_t *f = &s->lat_first;
		buf_add(buf, sizeof(buf) - 2u, &pos, ",%lu,%lu,%lu,%lu,%lu,%lu,%lu",
				(unsigned long) f->n, (unsigned long) s->timeout_count,
				(unsigned long) (s->crc_count + s->frame_count), (unsigned long) f->last_us,
				(unsigned long) f->min_us, (unsigned long) app_stat_avg_us(f),
				(unsigned long) f->max_us);
	}
	buf_add(buf, sizeof(buf) - 2u, &pos, ",%lu,%lu", (unsigned long) g_app.bus.cycle.last_us,
			(unsigned long) g_app.bus.idle.last_us);
	buf[pos++] = '\r';
	buf[pos++] = '\n';
	cli_write(buf, (uint32_t) pos);
}

static void cmd_stream(const char *arg) {
	if (*arg == '\0') {
		if (s_stream_ms != 0u) {
			cli_line("OK stream every %u ms", (unsigned) s_stream_ms);
		} else {
			cli_line("OK stream off");
		}
		return;
	}
	uint32_t ms;
	if (!parse_u32(arg, &ms)) {
		cli_line("ERR usage: stream N (ms, 0 = off, %u..%u)", (unsigned) CLI_STREAM_MIN_MS,
				(unsigned) CLI_STREAM_MAX_MS);
		return;
	}
	if (ms != 0u && ms < CLI_STREAM_MIN_MS) {
		ms = CLI_STREAM_MIN_MS;
	}
	if (ms > CLI_STREAM_MAX_MS) {
		ms = CLI_STREAM_MAX_MS;
	}
	s_stream_ms = (uint16_t) ms;
	if (ms == 0u) {
		cli_line("OK stream off");
		return;
	}
	// Шапка: # S,t_ms,loop_ms,s2,x2,y2,err2,s3,...,sd,rate_hz,file,gap_ms,cpu,bat_v,
	// bat_low,ok2,to2,bad2,lat2,lmin2,lavg2,lmax2,ok3,...,cyc_us,idle_us (цифра — адрес)
	char hdr[CLI_STREAM_MAX];
	size_t pos = 0u;
	buf_add(hdr, sizeof(hdr) - 2u, &pos, "# S,t_ms,loop_ms");
	for (uint8_t i = 0u; i < APP_SENSOR_COUNT; i++) {
		unsigned a = APP_SENSOR_ADDR(i);
		buf_add(hdr, sizeof(hdr) - 2u, &pos, ",s%u,x%u,y%u,err%u", a, a, a, a);
	}
	buf_add(hdr, sizeof(hdr) - 2u, &pos, ",sd,rate_hz,file,gap_ms,cpu,bat_v,bat_low");
	for (uint8_t i = 0u; i < APP_SENSOR_COUNT; i++) {
		unsigned a = APP_SENSOR_ADDR(i);
		buf_add(hdr, sizeof(hdr) - 2u, &pos, ",ok%u,to%u,bad%u,lat%u,lmin%u,lavg%u,lmax%u",
				a, a, a, a, a, a, a);
	}
	buf_add(hdr, sizeof(hdr) - 2u, &pos, ",cyc_us,idle_us");
	hdr[pos++] = '\r';
	hdr[pos++] = '\n';
	cli_write(hdr, (uint32_t) pos);
	cli_line("OK stream every %u ms", (unsigned) s_stream_ms);
	s_stream_last_ms = HAL_GetTick() - ms; // первая строка — в этом же проходе
}

static void cmd_set_usage(void) {
	cli_line("ERR usage: set freq N | set alpha X | set gap N | set theme dark|light"
			" | set batalarm X | set rollwin N | set rollhz N | set rollcalm X"
			" | set rollhyst X");
}

// Когда значение попадёт во флеш
static const char *cmd_set_note(void) {
	if (!settings_get_info()->pending) {
		return "no change";
	}
	return g_app.sd_state == SD_RECORDING ? "flash save after recording stops"
			: "flash save pending";
}

// set <name> <value>: то же, что меню прибора (функции app_*)
static void cmd_set(const char *arg) {
	const char *val = strchr(arg, ' ');
	if (val == NULL) {
		cmd_set_usage();
		return;
	}
	while (*val == ' ') {
		val++;
	}
	uint32_t n;
	float f;
	if (word_is(arg, "freq")) {
		if (!parse_u32(val, &n)) {
			cmd_set_usage();
			return;
		}
		app_set_log_freq((uint16_t) (n > 0xFFFFu ? 0xFFFFu : n));
		cli_line("OK freq=%u Hz (%s)", (unsigned) g_app.log_freq_hz, cmd_set_note());
	} else if (word_is(arg, "alpha")) {
		if (!parse_decimal(val, &f)) {
			cmd_set_usage();
			return;
		}
		app_set_ema_alpha(f);
		cli_line("OK alpha=%s (%s)", fx(g_app.ema_alpha, 2), cmd_set_note());
	} else if (word_is(arg, "gap")) {
		if (!parse_u32(val, &n)) {
			cmd_set_usage();
			return;
		}
		app_set_bus_gap((uint8_t) (n > 0xFFu ? 0xFFu : n));
		cli_line("OK gap=%u ms (%s)", (unsigned) g_app.bus_gap_ms, cmd_set_note());
	} else if (word_is(arg, "theme")) {
		if (strcmp(val, "dark") == 0) {
			app_set_theme(APP_THEME_DARK);
		} else if (strcmp(val, "light") == 0) {
			app_set_theme(APP_THEME_LIGHT);
		} else {
			cmd_set_usage();
			return;
		}
		cli_line("OK theme=%s (%s)", theme_str(g_app.theme), cmd_set_note());
	} else if (word_is(arg, "batalarm")) {
		if (!parse_decimal(val, &f)) {
			cmd_set_usage();
			return;
		}
		app_set_bat_alarm(f);
		cli_line("OK batalarm=%s V (%s)", fx(g_app.bat_alarm_v, 2), cmd_set_note());
	} else if (word_is(arg, "rollwin")) {
		if (!parse_u32(val, &n)) {
			cmd_set_usage();
			return;
		}
		app_set_roll_window((uint8_t) (n > 0xFFu ? 0xFFu : n));
		cli_line("OK rollwin=%u s (%s)", (unsigned) g_app.roll_window_s, cmd_set_note());
	} else if (word_is(arg, "rollhz")) {
		if (!parse_u32(val, &n)) {
			cmd_set_usage();
			return;
		}
		app_set_roll_rate((uint8_t) (n > 0xFFu ? 0xFFu : n));
		cli_line("OK rollhz=%u Hz (%s)", (unsigned) g_app.roll_rate_hz, cmd_set_note());
	} else if (word_is(arg, "rollcalm")) {
		if (!parse_decimal(val, &f)) {
			cmd_set_usage();
			return;
		}
		app_set_roll_calm(f);
		cli_line("OK rollcalm=%s deg (%s)", fx(g_app.roll_calm_deg, 2), cmd_set_note());
	} else if (word_is(arg, "rollhyst")) {
		if (!parse_decimal(val, &f)) {
			cmd_set_usage();
			return;
		}
		app_set_roll_hyst(f);
		cli_line("OK rollhyst=%s deg (%s)", fx(g_app.roll_hyst_deg, 2), cmd_set_note());
	} else {
		cmd_set_usage();
	}
}

static void cmd_reboot(const char *name, const char *arg, bool to_bootloader) {
	bool force = word_is(arg, "force");
	if (*arg != '\0' && !force) {
		cli_line("ERR usage: %s [force]", name);
		return;
	}
	if (g_app.sd_state == SD_RECORDING && !force) {
		cli_line("ERR recording to SD in progress: stop it or use '%s force'", name);
		return;
	}
	cli_line("%s", to_bootloader ? "DFU..." : "RESET...");
	cli_reboot(to_bootloader);
}

// Вывод для команд из usb_cli_ext.c (status, files, get…)
void usb_cli_out(const char *s, uint32_t n) {
	cli_write(s, n);
}

uint32_t usb_cli_out_free(void) {
	return cli_port_ready() ? CLI_TX_SIZE - (s_tx_head - s_tx_tail) : 0u;
}

// line: нижний регистр, без ведущих пробелов
static void cli_exec(char *line) {
	size_t n = strlen(line);
	while (n > 0u && line[n - 1u] == ' ') {
		line[--n] = '\0';
	}
	const char *arg = "";
	char *sp = strchr(line, ' ');
	if (sp != NULL) {
		*sp = '\0';
		arg = sp + 1;
		while (*arg == ' ') {
			arg++;
		}
	}

	if (usb_cli_ext_exec(line, arg)) {
		// status, set time, addr, zero, files, get — usb_cli_ext.c
	} else if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
		cmd_help();
	} else if (strcmp(line, "ver") == 0) {
		cmd_ver();
	} else if (strcmp(line, "diag") == 0) {
		cmd_diag(arg);
	} else if (strcmp(line, "stream") == 0) {
		cmd_stream(arg);
	} else if (strcmp(line, "set") == 0) {
		cmd_set(arg);
	} else if (strcmp(line, "boot") == 0 || strcmp(line, "dfu") == 0) {
		cmd_reboot(line, arg, true);
	} else if (strcmp(line, "reset") == 0) {
		cmd_reboot(line, arg, false);
	} else {
		cli_line("ERR unknown command '%s' (try: help)", line);
	}
}

/* ----------------------------------------------------------------------------
 * Приём
 * ------------------------------------------------------------------------- */

// Очередной принятый байт. true — в s_line готова строка.
static bool cli_feed(char c) {
	if (c == '\r' || c == '\n') {
		if (s_line_overflow) {
			s_line_overflow = false;
			s_line_len = 0u;
			cli_line("ERR line too long (max %u)", (unsigned) CLI_LINE_MAX);
			return false;
		}
		if (s_line_len == 0u) {
			return false; // пустая строка или вторая половина CRLF
		}
		s_line[s_line_len] = '\0';
		s_line_len = 0u;
		return true;
	}
	if (c == '\b' || c == 0x7F) { // забой
		if (s_line_len > 0u) {
			s_line_len--;
		}
		return false;
	}
	if (c == '\t') {
		c = ' ';
	}
	if (c < 0x20 || c > 0x7E) {
		return false; // прочие управляющие и не-ASCII
	}
	if (c == ' ' && s_line_len == 0u) {
		return false; // ведущие пробелы
	}
	if (s_line_len >= CLI_LINE_MAX) {
		s_line_overflow = true;
		return false;
	}
	s_line[s_line_len++] = to_lower(c);
	return false;
}

// Разобрать принятое, выполнить не больше одной команды
static void cli_rx_poll(void) {
	uint32_t head = s_rx_head;
	__DMB(); // данные кольца читать после head
	uint32_t tail = s_rx_tail;
	bool ready = false;
	while (tail != head && !ready) {
		ready = cli_feed((char) s_rx_buf[tail & (CLI_RX_SIZE - 1u)]);
		tail++;
	}
	s_rx_tail = tail;
	if (ready) {
		cli_exec(s_line);
	}
}

// Аварийный путь: суперцикл стоит — boot/dfu/reset прямо здесь (без ответа)
static void cli_isr_emergency(const uint8_t *buf, uint32_t len) {
	for (uint32_t i = 0u; i < len; i++) {
		char c = to_lower((char) buf[i]);
		if (c == '\r' || c == '\n') {
			s_isr_line[s_isr_len] = '\0';
			s_isr_len = 0u;
			if ((HAL_GetTick() - s_last_task_ms) <= CLI_STALL_MS) {
				continue; // суперцикл жив — команду выполнит usb_cli_task()
			}
			bool boot = word_is(s_isr_line, "boot") || word_is(s_isr_line, "dfu");
			if (boot || word_is(s_isr_line, "reset")) {
				if (boot) {
					cli_set_boot_magic();
				}
				NVIC_SystemReset();
			}
		} else if (s_isr_len < sizeof(s_isr_line) - 1u) {
			s_isr_line[s_isr_len++] = c;
		}
	}
}

void usb_cli_rx_isr(const uint8_t *buf, uint32_t len) {
	uint32_t head = s_rx_head;
	uint32_t tail = s_rx_tail;
	for (uint32_t i = 0u; i < len; i++) {
		if (head - tail >= CLI_RX_SIZE) {
			s_rx_dropped += len - i;
			break;
		}
		s_rx_buf[head & (CLI_RX_SIZE - 1u)] = buf[i];
		head++;
	}
	s_rx_head = head;
	cli_isr_emergency(buf, len);
}

void usb_cli_line_state_isr(bool dtr) {
	if (dtr && !s_dtr) {
		s_evt_open = true;
	} else if (!dtr && s_dtr) {
		s_evt_close = true;
	}
	s_dtr = dtr;
}

/* ----------------------------------------------------------------------------
 * Суперцикл
 * ------------------------------------------------------------------------- */

void usb_cli_task(void) {
	uint32_t now = HAL_GetTick();
	s_last_task_ms = now;

	if (s_evt_open) {
		// Порт только что открыли: старый неотправленный вывод не нужен
		s_evt_open = false;
		cli_tx_drop_queued();
		usb_cli_ext_abort(); // и недоданная передача files/get прежнему хосту
	}
	if (s_evt_close) {
		// Порт закрыли: поток выключить, чтобы не крутился без слушателя
		s_evt_close = false;
		s_stream_ms = 0u;
		usb_cli_ext_abort();
	}

	// Сначала подобрать завершённую передачу: снимает признак «хост не читает»
	// до того, как команда начнёт писать ответ
	cli_tx_pump(now);

	cli_rx_poll();

	if (s_stream_ms != 0u && (now - s_stream_last_ms) >= s_stream_ms) {
		s_stream_last_ms = now;
		if (cli_port_ready()) {
			stream_emit(now);
		}
	}

	usb_cli_ext_task(now); // files / get: очередная порция

	cli_tx_pump(now);
}
