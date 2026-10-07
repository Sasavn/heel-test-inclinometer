/*
 * usb_cli_ext.c — команды USB CDC для программы на ПК (pc/, krenomer.exe):
 * status (JSON), set time, addr, zero, files, get. Список команд и форматы —
 * usb_cli_ext.h и README (раздел «Командная строка по USB»).
 *
 * Вывод — через кольцо передачи usb_cli.c (usb_cli_out(): строка целиком или
 * ничего). Поэтому перед каждой порцией проверяется свободное место
 * (usb_cli_out_free()): строки files/get пишутся, только пока в кольце остаётся
 * запас CLI_EXT_TX_RESERVE — на ответы других команд (status до ~1,6 КБ) и поток.
 *
 * files и get — конечный автомат s_xf: команда открывает каталог или файл,
 * дальше usb_cli_ext_task() в каждом проходе суперцикла читает не больше
 * сектора (512 байт, от границы до границы — одно чтение карты) или
 * CLI_EXT_LIST_STEP записей каталога и сразу выдаёт их строками. Хост не
 * забирает данные дольше CLI_EXT_STALL_MS — передача прерывается.
 *
 * FatFs вызывается только отсюда и из sd_logger.c — оба из суперцикла. Во
 * время записи замера (SD_RECORDING) карту не трогаем: files/get отказывают, а
 * начавшаяся запись прерывает передачу (как и вынутая карта: sd_logger тогда
 * отмонтирует том, наши FIL/DIR становятся недействительными).
 */
#include "usb_cli_ext.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "app.h"
#include "bwm427.h"
#include "ff.h"
#include "sd_logger.h"
#include "settings.h"
#include "version.h"

#define CLI_EXT_LINE_MAX     192u    // длина строки ответа (как CLI_OUT_MAX в usb_cli.c)
#define CLI_EXT_SCRATCH      2048u   // буфер на один вызов: JSON status (до ~1,6 КБ) или сектор файла
#define CLI_EXT_SECTOR       512u    // get: читать не дальше границы сектора
#define CLI_EXT_D_BYTES      48u     // байт файла в строке D (64 знака base64)
#define CLI_EXT_D_LINE_MAX   (2u + 64u + 2u)   // "D," + base64 + "\r\n"
#define CLI_EXT_END_MAX      48u     // "E,<байт>,<crc>\r\n" + "OK\r\n"
#define CLI_EXT_F_LINE_MAX   (CLI_EXT_LINE_MAX + 2u)
#define CLI_EXT_TX_RESERVE   1792u   // запас в кольце передачи: ответ status (до ~1,6 КБ) и поток
#define CLI_EXT_LIST_STEP    4u      // записей каталога за проход (~ одно чтение сектора)
#define CLI_EXT_STALL_MS     5000u   // хост не забирает данные — прервать files/get
#define CLI_EXT_ADDR_MAX     247u    // Modbus: адреса 1..247

/* ----------------------------------------------------------------------------
 * Состояние files / get
 * ------------------------------------------------------------------------- */

typedef enum {
	XF_IDLE = 0,
	XF_LIST,     // files: обход корня карты
	XF_GET,      // get: передача файла
} xf_mode_t;

static xf_mode_t s_xf_mode;
static FIL s_xf_fil;
static DIR s_xf_dir;
static FILINFO s_xf_fno;           // с длинным именем — ~270 байт, не на стек
static uint32_t s_xf_size;         // get: размер файла
static uint32_t s_xf_pos;          // get: позиция чтения
static uint32_t s_xf_start;        // get: смещение начала передачи
static uint32_t s_xf_crc;          // get: CRC-32 переданного
static uint32_t s_xf_count;        // files: файлов в списке
static uint32_t s_xf_ms;           // HAL_GetTick() последнего продвижения

// Буфер на один вызов: JSON status или сектор файла get (между вызовами
// ничего в нём не хранится)
static char s_scratch[CLI_EXT_SCRATCH];

/* ----------------------------------------------------------------------------
 * Мелочи
 * ------------------------------------------------------------------------- */

// Строка начинается со слова w, за которым конец строки или пробел
static bool word_is(const char *line, const char *w) {
	size_t n = strlen(w);
	return strncmp(line, w, n) == 0 && (line[n] == '\0' || line[n] == ' ');
}

static const char *skip_spaces(const char *s) {
	while (*s == ' ') {
		s++;
	}
	return s;
}

// Десятичное число без знака до пробела или конца строки (до 9 цифр).
// Возвращает указатель за числом или NULL.
static const char *parse_u32(const char *s, uint32_t *out) {
	uint32_t v = 0u;
	uint8_t digits = 0u;
	for (; *s >= '0' && *s <= '9'; s++) {
		if (digits >= 9u) {
			return NULL;
		}
		v = v * 10u + (uint32_t) (*s - '0');
		digits++;
	}
	if (digits == 0u || (*s != '\0' && *s != ' ')) {
		return NULL;
	}
	*out = v;
	return s;
}

// Строка ответа по формату + "\r\n" (не влезла в кольцо — выброшена целиком)
static void ext_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void ext_line(const char *fmt, ...) {
	char buf[CLI_EXT_LINE_MAX];
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
	usb_cli_out(buf, (uint32_t) n);
}

static bool ends_with_csv(const char *name) {
	size_t n = strlen(name);
	if (n < 4u) {
		return false;
	}
	const char *e = name + n - 4u;
	return e[0] == '.' && (e[1] == 'c' || e[1] == 'C') && (e[2] == 's' || e[2] == 'S')
			&& (e[3] == 'v' || e[3] == 'V');
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
 * Чистые функции: base64, CRC-32, разбор времени
 * ------------------------------------------------------------------------- */

size_t usb_cli_ext_base64(char *out, const uint8_t *in, size_t n) {
	static const char abc[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char *p = out;
	size_t i = 0u;
	for (; i + 3u <= n; i += 3u) {
		uint32_t v = ((uint32_t) in[i] << 16) | ((uint32_t) in[i + 1u] << 8) | in[i + 2u];
		*p++ = abc[(v >> 18) & 63u];
		*p++ = abc[(v >> 12) & 63u];
		*p++ = abc[(v >> 6) & 63u];
		*p++ = abc[v & 63u];
	}
	if (i < n) {
		uint32_t v = (uint32_t) in[i] << 16;
		if (i + 1u < n) {
			v |= (uint32_t) in[i + 1u] << 8;
		}
		*p++ = abc[(v >> 18) & 63u];
		*p++ = abc[(v >> 12) & 63u];
		*p++ = (i + 1u < n) ? abc[(v >> 6) & 63u] : '=';
		*p++ = '=';
	}
	*p = '\0';
	return (size_t) (p - out);
}

// Таблица на полбайта: 64 байта во флеше, ~2 такта на бит — сектор за ~15 мкс
uint32_t usb_cli_ext_crc32(uint32_t crc, const uint8_t *p, size_t n) {
	static const uint32_t tab[16] = {
		0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
		0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
		0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
		0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
	};
	crc = ~crc;
	for (size_t i = 0u; i < n; i++) {
		crc ^= p[i];
		crc = (crc >> 4) ^ tab[crc & 15u];
		crc = (crc >> 4) ^ tab[crc & 15u];
	}
	return ~crc;
}

// Ровно n цифр
static bool digits_n(const char *s, uint8_t n, uint16_t *out) {
	uint16_t v = 0u;
	for (uint8_t i = 0u; i < n; i++) {
		if (s[i] < '0' || s[i] > '9') {
			return false;
		}
		v = (uint16_t) (v * 10u + (uint16_t) (s[i] - '0'));
	}
	*out = v;
	return true;
}

bool usb_cli_ext_parse_time(const char *s, app_time_t *t) {
	// 0123456789012345678
	// YYYY-MM-DD HH:MM:SS
	uint16_t y, mo, d, h, mi, se;
	if (strlen(s) != 19u || s[4] != '-' || s[7] != '-'
			|| (s[10] != ' ' && s[10] != 't' && s[10] != 'T') || s[13] != ':' || s[16] != ':') {
		return false;
	}
	if (!digits_n(s, 4u, &y) || !digits_n(s + 5, 2u, &mo) || !digits_n(s + 8, 2u, &d)
			|| !digits_n(s + 11, 2u, &h) || !digits_n(s + 14, 2u, &mi)
			|| !digits_n(s + 17, 2u, &se)) {
		return false;
	}
	if (y < 2000u || y > 2099u) {
		return false;
	}
	app_time_t v = {
		.year = (uint8_t) (y - 2000u), .month = (uint8_t) mo, .date = (uint8_t) d,
		.hours = (uint8_t) h, .minutes = (uint8_t) mi, .seconds = (uint8_t) se,
	};
	if (!app_time_valid(&v)) {
		return false;
	}
	*t = v;
	return true;
}

/* ----------------------------------------------------------------------------
 * JSON status
 * ------------------------------------------------------------------------- */

typedef struct {
	char *buf;
	size_t size;
	size_t pos;
	bool overflow;
} jbuf_t;

static void jb_add(jbuf_t *j, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void jb_add(jbuf_t *j, const char *fmt, ...) {
	if (j->overflow) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(j->buf + j->pos, j->size - j->pos, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t) n >= j->size - j->pos) {
		j->overflow = true;
		return;
	}
	j->pos += (size_t) n;
}

// Число с фиксированной точкой без "%f" (тот в newlib берёт память через
// malloc); NaN и переполнение — null
static void jb_fix(jbuf_t *j, float v, uint8_t dec) {
	uint32_t scale = 1u;
	for (uint8_t i = 0u; i < dec; i++) {
		scale *= 10u;
	}
	float a = (v < 0.0f) ? -v : v;
	if (v != v || a >= 2.0e9f / (float) scale) {
		jb_add(j, "null");
		return;
	}
	uint32_t q = (uint32_t) (a * (float) scale + 0.5f);
	const char *sign = (v < 0.0f && q != 0u) ? "-" : "";
	if (dec == 0u) {
		jb_add(j, "%s%lu", sign, (unsigned long) q);
	} else {
		jb_add(j, "%s%lu.%0*lu", sign, (unsigned long) (q / scale), (int) dec,
				(unsigned long) (q % scale));
	}
}

static void jb_bool(jbuf_t *j, const char *key, bool v) {
	jb_add(j, ",\"%s\":%s", key, v ? "true" : "false");
}

static void jb_u32(jbuf_t *j, const char *key, uint32_t v) {
	jb_add(j, ",\"%s\":%lu", key, (unsigned long) v);
}

static void jb_float(jbuf_t *j, const char *key, float v, uint8_t dec) {
	jb_add(j, ",\"%s\":", key);
	jb_fix(j, v, dec);
}

// Строка в кавычках (имена файлов — ASCII; кавычки, \ и управляющие экранируются)
static void jb_str(jbuf_t *j, const char *s) {
	jb_add(j, "\"");
	for (; *s != '\0'; s++) {
		unsigned char c = (unsigned char) *s;
		if (c == '"' || c == '\\') {
			jb_add(j, "\\%c", c);
		} else if (c < 0x20u) {
			jb_add(j, "\\u%04x", c);
		} else {
			jb_add(j, "%c", c);
		}
	}
	jb_add(j, "\"");
}

// Статистика длительностей: [n,last,min,avg,max], мкс
static void jb_stat(jbuf_t *j, const char *key, const app_stat_t *s) {
	jb_add(j, ",\"%s\":[%lu,%lu,%lu,%lu,%lu]", key, (unsigned long) s->n,
			(unsigned long) s->last_us, (unsigned long) s->min_us,
			(unsigned long) app_stat_avg_us(s), (unsigned long) s->max_us);
}

static void jb_sensor(jbuf_t *j, uint8_t i, uint32_t now) {
	const app_sensor_t *s = &g_app.sensor[i];
	jb_add(j, "%s{\"addr\":%u,\"st\":\"%s\"", i ? "," : "", (unsigned) APP_SENSOR_ADDR(i),
			sensor_status_str(s->status));
	jb_float(j, "x", s->x, 3);
	jb_float(j, "y", s->y, 3);
	jb_float(j, "ox", s->off_x, 3);
	jb_float(j, "oy", s->off_y, 3);
	jb_float(j, "roll_x", s->roll_x, 3);
	jb_float(j, "roll_y", s->roll_y, 3);
	jb_bool(j, "calm_x", s->calm_x);
	jb_bool(j, "calm_y", s->calm_y);
	jb_u32(j, "fill", s->roll_fill_s);
	jb_u32(j, "ok", s->ok_count);
	jb_u32(j, "err", s->err_count);
	jb_u32(j, "garbled", s->garbled_count);
	jb_add(j, ",\"last_err\":\"%s\"", sensor_err_str(s->last_err));
	if (s->ok_count == 0u) {
		jb_add(j, ",\"age\":null");
	} else {
		jb_u32(j, "age", now - s->last_ok_ms);
	}
	jb_u32(j, "to", s->timeout_count);
	jb_u32(j, "crc", s->crc_count);
	jb_u32(j, "bad", s->frame_count);
	jb_u32(j, "tmo_us", s->timeout_us);
	jb_stat(j, "lat", &s->lat_first);
	jb_stat(j, "done", &s->lat_done);
	jb_add(j, "}");
}

size_t usb_cli_ext_status_json(char *out, size_t size, uint32_t now) {
	jbuf_t jb = { .buf = out, .size = size, .pos = 0u, .overflow = false };
	jbuf_t *j = &jb;
	const app_time_t *t = &g_app.time;

	jb_add(j, "{\"v\":1,\"fw\":\"%s\"", FW_VERSION);
	jb_u32(j, "up", g_app.diag.uptime_s);
	jb_u32(j, "ms", now);
	jb_add(j, ",\"time\":\"20%02u-%02u-%02u %02u:%02u:%02u\"", (unsigned) t->year,
			(unsigned) t->month, (unsigned) t->date, (unsigned) t->hours,
			(unsigned) t->minutes, (unsigned) t->seconds);
	jb_bool(j, "rtc", g_app.rtc_present);

	// Настройки
	jb_u32(j, "freq", g_app.log_freq_hz);
	jb_float(j, "alpha", g_app.ema_alpha, 2);
	jb_u32(j, "gap", g_app.bus_gap_ms);
	jb_add(j, ",\"theme\":\"%s\"", g_app.theme == APP_THEME_LIGHT ? "light" : "dark");
	jb_float(j, "batalarm", g_app.bat_alarm_v, 2);
	jb_u32(j, "rollwin", g_app.roll_window_s);
	jb_u32(j, "rollhz", g_app.roll_rate_hz);
	jb_float(j, "rollcalm", g_app.roll_calm_deg, 2);
	jb_float(j, "rollhyst", g_app.roll_hyst_deg, 2);
	jb_bool(j, "save_pending", settings_get_info()->pending);

	// Суперцикл и опрос
	jb_float(j, "rate", g_app.actual_rate_hz, 1);
	jb_u32(j, "cpu", g_app.diag.cpu_load_pct);
	jb_u32(j, "loops", g_app.diag.loops_per_s);
	jb_u32(j, "loop_max", g_app.diag.loop_max_ms);

	// Датчики
	jb_add(j, ",\"sens\":[");
	for (uint8_t i = 0u; i < APP_SENSOR_COUNT; i++) {
		jb_sensor(j, i, now);
	}
	jb_add(j, "]");

	// Сводка качки
	jb_bool(j, "stable", g_app.is_stable);
	jb_float(j, "span", g_app.stab_span, 3);
	if (g_app.stab_sensor < APP_SENSOR_COUNT) {
		jb_add(j, ",\"stab_addr\":%u,\"stab_axis\":\"%c\"",
				(unsigned) APP_SENSOR_ADDR(g_app.stab_sensor), g_app.stab_axis ? 'Y' : 'X');
	} else {
		jb_add(j, ",\"stab_addr\":null,\"stab_axis\":null");
	}
	jb_u32(j, "stab_fill", g_app.stab_fill_s);

	// Шина RS485
	const app_bus_t *b = &g_app.bus;
	uint32_t cyc = app_stat_avg_us(&b->cycle);
	jb_add(j, ",\"bus\":{\"fallbacks\":%lu", (unsigned long) b->timer_fallbacks);
	jb_stat(j, "gap", &b->gap);
	jb_stat(j, "idle", &b->idle);
	jb_stat(j, "cyc", &b->cycle);
	jb_float(j, "max_rate", cyc ? 1.0e6f / (float) cyc : 0.0f, 2);
	jb_add(j, "}");

	// Карта и запись
	jb_add(j, ",\"sd\":\"%s\"", sd_state_str(g_app.sd_state));
	jb_u32(j, "sd_err", g_app.sd_err);
	jb_u32(j, "file", g_app.file_number);
	jb_add(j, ",\"names\":[");
	bool any = false;
	for (uint8_t i = 0u; i < APP_SENSOR_COUNT; i++) {
		char name[SD_NAME_LEN];
		if (sd_logger_file_name(i, name)) {
			if (any) {
				jb_add(j, ",");
			}
			jb_str(j, name);
			any = true;
		}
	}
	jb_add(j, "]");
	jb_u32(j, "rec_mask", g_app.rec_sensor_mask);
	jb_u32(j, "rows", g_app.rec_rows);
	jb_u32(j, "rec_s", g_app.sd_state == SD_RECORDING ? (now - g_app.rec_start_ms) / 1000u : 0u);
	jb_u32(j, "sd_total", g_app.sd_total_mb);
	if (g_app.sd_free_mb == APP_SD_FREE_UNKNOWN) {
		jb_add(j, ",\"sd_free\":null");
	} else {
		jb_u32(j, "sd_free", g_app.sd_free_mb);
	}

	// Питание
	jb_float(j, "bat_v", g_app.battery_v, 2);
	jb_bool(j, "bat_present", g_app.battery_present);
	jb_bool(j, "bat_low", g_app.battery_low);
	if (g_app.battery_pct == APP_BAT_PCT_NONE) {
		jb_add(j, ",\"bat_pct\":null");
	} else {
		jb_u32(j, "bat_pct", g_app.battery_pct);
	}
	jb_bool(j, "bat_sat", g_app.battery_adc_sat);

	// Прочее
	jb_bool(j, "rec_sw", g_app.rec_switch_on);
	jb_add(j, ",\"svc\":\"%s\"", svc_state_str(g_app.svc_state));
	jb_add(j, ",\"xfer\":\"%s\"",
			s_xf_mode == XF_GET ? "get" : s_xf_mode == XF_LIST ? "list" : "idle");
	jb_add(j, "}\r\n");

	return jb.overflow ? 0u : jb.pos;
}

/* ----------------------------------------------------------------------------
 * files / get
 * ------------------------------------------------------------------------- */

// Закрыть открытое (файл открыт только на чтение — карта не пишется; после
// отмонтирования FatFs просто вернёт ошибку объекта)
static void xf_close(void) {
	if (s_xf_mode == XF_GET) {
		(void) f_close(&s_xf_fil);
	} else if (s_xf_mode == XF_LIST) {
		(void) f_closedir(&s_xf_dir);
	}
	s_xf_mode = XF_IDLE;
}

// Карта для files/get: готова и не пишется. Иначе — ответ ERR, false.
static bool xf_card_ready(void) {
	switch (g_app.sd_state) {
	case SD_READY:
		return true;
	case SD_RECORDING:
		ext_line("ERR busy recording");
		return false;
	case SD_ERROR:
		ext_line("ERR card error (FatFs %u): set the REC switch to STOP", (unsigned) g_app.sd_err);
		return false;
	case SD_NO_CARD:
	default:
		ext_line("ERR no card");
		return false;
	}
}

static void cmd_files(const char *arg) {
	if (*arg != '\0') {
		ext_line("ERR usage: files");
		return;
	}
	xf_close(); // новая команда отменяет прежнюю передачу
	if (!xf_card_ready()) {
		return;
	}
	FRESULT res = f_opendir(&s_xf_dir, "/");
	if (res != FR_OK) {
		ext_line("ERR opendir (FatFs %u)", (unsigned) res);
		return;
	}
	s_xf_mode = XF_LIST;
	s_xf_count = 0u;
	s_xf_ms = HAL_GetTick();
	usb_cli_ext_task(s_xf_ms); // первые записи — сразу
}

static void xf_list_step(uint32_t now) {
	for (uint8_t k = 0u; k < CLI_EXT_LIST_STEP; k++) {
		if (usb_cli_out_free() < CLI_EXT_F_LINE_MAX + CLI_EXT_TX_RESERVE) {
			return; // ждать, пока хост заберёт
		}
		FRESULT res = f_readdir(&s_xf_dir, &s_xf_fno);
		if (res != FR_OK) {
			xf_close();
			ext_line("ERR readdir (FatFs %u)", (unsigned) res);
			return;
		}
		s_xf_ms = now;
		if (s_xf_fno.fname[0] == '\0') {
			xf_close();
			ext_line("OK %lu", (unsigned long) s_xf_count);
			return;
		}
		if ((s_xf_fno.fattrib & (AM_DIR | AM_HID | AM_SYS)) || !ends_with_csv(s_xf_fno.fname)) {
			continue;
		}
		WORD d = s_xf_fno.fdate, t = s_xf_fno.ftime;
		ext_line("F,%s,%lu,%04u-%02u-%02u %02u:%02u", s_xf_fno.fname,
				(unsigned long) s_xf_fno.fsize, (unsigned) (1980u + (d >> 9)),
				(unsigned) ((d >> 5) & 15u), (unsigned) (d & 31u), (unsigned) (t >> 11),
				(unsigned) ((t >> 5) & 63u));
		s_xf_count++;
	}
}

// get <имя> [смещение] | get abort
static void cmd_get(const char *arg) {
	if (strcmp(arg, "abort") == 0) {
		bool active = (s_xf_mode != XF_IDLE);
		xf_close();
		ext_line("%s", active ? "OK aborted" : "OK nothing to abort");
		return;
	}
	xf_close();
	// Имя — до последнего пробела, если за ним число (смещение), иначе вся строка
	char name[CLI_EXT_LINE_MAX / 2u];
	uint32_t offset = 0u;
	const char *sp = strrchr(arg, ' ');
	size_t len = strlen(arg);
	if (sp != NULL && parse_u32(sp + 1, &offset) != NULL) {
		len = (size_t) (sp - arg);
		while (len > 0u && arg[len - 1u] == ' ') {
			len--;
		}
	} else {
		offset = 0u;
	}
	if (len == 0u || len >= sizeof(name)) {
		ext_line("ERR usage: get <name> [offset] | get abort");
		return;
	}
	memcpy(name, arg, len);
	name[len] = '\0';
	if (strpbrk(name, "/\\:*?\"<>|") != NULL) {
		ext_line("ERR only files in the card root");
		return;
	}
	if (!xf_card_ready()) {
		return;
	}
	FRESULT res = f_open(&s_xf_fil, name, FA_READ | FA_OPEN_EXISTING);
	if (res != FR_OK) {
		if (res == FR_NO_FILE) {
			ext_line("ERR no such file");
		} else {
			ext_line("ERR open (FatFs %u)", (unsigned) res);
		}
		return;
	}
	uint32_t size = (uint32_t) f_size(&s_xf_fil);
	if (offset > size) {
		(void) f_close(&s_xf_fil);
		ext_line("ERR offset %lu > size %lu", (unsigned long) offset, (unsigned long) size);
		return;
	}
	if (offset != 0u) {
		res = f_lseek(&s_xf_fil, offset);
		if (res != FR_OK) {
			(void) f_close(&s_xf_fil);
			ext_line("ERR seek (FatFs %u)", (unsigned) res);
			return;
		}
	}
	s_xf_mode = XF_GET;
	s_xf_size = size;
	s_xf_pos = offset;
	s_xf_start = offset;
	s_xf_crc = 0u;
	s_xf_ms = HAL_GetTick();
	ext_line("G,%s,%lu,%lu", name, (unsigned long) size, (unsigned long) offset);
	usb_cli_ext_task(s_xf_ms); // первый сектор — сразу (пустой файл — сразу E и OK)
}

static void xf_get_end(void) {
	ext_line("E,%lu,%08lx", (unsigned long) (s_xf_size - s_xf_start), (unsigned long) s_xf_crc);
	ext_line("OK");
	xf_close();
}

static void xf_get_step(uint32_t now) {
	uint32_t left = s_xf_size - s_xf_pos;
	if (left == 0u) {
		if (usb_cli_out_free() >= CLI_EXT_END_MAX) {
			xf_get_end();
		}
		return;
	}
	// До границы сектора: одно чтение карты за проход
	uint32_t n = CLI_EXT_SECTOR - (s_xf_pos % CLI_EXT_SECTOR);
	if (n > left) {
		n = left;
	}
	uint32_t lines = (n + CLI_EXT_D_BYTES - 1u) / CLI_EXT_D_BYTES;
	uint32_t need = lines * CLI_EXT_D_LINE_MAX + CLI_EXT_TX_RESERVE;
	if (n == left) {
		need += CLI_EXT_END_MAX; // E и OK — в этом же проходе
	}
	if (usb_cli_out_free() < need) {
		return; // ждать, пока хост заберёт
	}
	UINT br = 0u;
	FRESULT res = f_read(&s_xf_fil, s_scratch, n, &br);
	if (res != FR_OK || br != n) {
		xf_close();
		ext_line("ERR read (FatFs %u, %u of %lu bytes)", (unsigned) res, (unsigned) br,
				(unsigned long) n);
		return;
	}
	const uint8_t *p = (const uint8_t *) s_scratch;
	s_xf_crc = usb_cli_ext_crc32(s_xf_crc, p, n);
	for (uint32_t i = 0u; i < n; i += CLI_EXT_D_BYTES) {
		char line[CLI_EXT_D_LINE_MAX + 1u];
		uint32_t k = (n - i < CLI_EXT_D_BYTES) ? n - i : CLI_EXT_D_BYTES;
		line[0] = 'D';
		line[1] = ',';
		size_t m = 2u + usb_cli_ext_base64(&line[2], p + i, k);
		line[m++] = '\r';
		line[m++] = '\n';
		usb_cli_out(line, (uint32_t) m);
	}
	s_xf_pos += n;
	s_xf_ms = now;
	if (s_xf_pos == s_xf_size) {
		xf_get_end();
	}
}

void usb_cli_ext_task(uint32_t now) {
	if (s_xf_mode == XF_IDLE) {
		return;
	}
	if (g_app.sd_state != SD_READY) {
		// Началась запись, карту вынули или ошибка. f_close/f_closedir всё равно
		// нужны: при записи том смонтирован, и незакрытый файл держал бы место в
		// таблице блокировок FatFs (_FS_LOCK); отмонтированный том FatFs просто
		// не примет (FR_INVALID_OBJECT), к карте при этом не обращаясь
		bool rec = (g_app.sd_state == SD_RECORDING);
		xf_close();
		ext_line("%s", rec ? "ERR aborted: recording started" : "ERR aborted: card removed");
		return;
	}
	if ((now - s_xf_ms) > CLI_EXT_STALL_MS) {
		xf_close();
		ext_line("ERR aborted: host does not read");
		return;
	}
	if (s_xf_mode == XF_LIST) {
		xf_list_step(now);
	} else {
		xf_get_step(now);
	}
}

/* ----------------------------------------------------------------------------
 * Остальные команды
 * ------------------------------------------------------------------------- */

static void cmd_status(const char *arg) {
	if (*arg != '\0') {
		ext_line("ERR usage: status");
		return;
	}
	size_t n = usb_cli_ext_status_json(s_scratch, sizeof(s_scratch), HAL_GetTick());
	if (n == 0u) {
		ext_line("ERR status does not fit");
		return;
	}
	if (usb_cli_out_free() < n + 4u) {
		ext_line("ERR tx busy, retry");
		return;
	}
	usb_cli_out(s_scratch, (uint32_t) n);
	ext_line("OK");
}

static void cmd_set_time(const char *arg) {
	app_time_t t;
	if (!usb_cli_ext_parse_time(arg, &t)) {
		ext_line("ERR usage: set time YYYY-MM-DD HH:MM:SS (2000..2099, valid date)");
		return;
	}
	app_set_time(&t);
	const app_time_t *v = &g_app.time;
	ext_line("OK time=20%02u-%02u-%02u %02u:%02u:%02u (rtc %s)", (unsigned) v->year,
			(unsigned) v->month, (unsigned) v->date, (unsigned) v->hours,
			(unsigned) v->minutes, (unsigned) v->seconds, g_app.rtc_present ? "yes" : "no");
}

// addr OLD NEW
static void cmd_addr(const char *arg) {
	uint32_t from, to;
	const char *p = parse_u32(arg, &from);
	if (p != NULL) {
		p = parse_u32(skip_spaces(p), &to);
	}
	if (p == NULL || *p != '\0') {
		ext_line("ERR usage: addr OLD NEW (Modbus 1..%u)", (unsigned) CLI_EXT_ADDR_MAX);
		return;
	}
	if (from < 1u || from > CLI_EXT_ADDR_MAX || to < 1u || to > CLI_EXT_ADDR_MAX || from == to) {
		ext_line("ERR addresses must be 1..%u and differ", (unsigned) CLI_EXT_ADDR_MAX);
		return;
	}
	if (g_app.sd_state == SD_RECORDING) {
		ext_line("ERR busy recording");
		return;
	}
	if (g_app.svc_state == SVC_BUSY) {
		ext_line("ERR busy: address change in progress");
		return;
	}
	app_sensor_set_address((uint8_t) from, (uint8_t) to);
	if (g_app.svc_state == SVC_BUSY) {
		ext_line("OK addr %lu->%lu started (result: status \"svc\")", (unsigned long) from,
				(unsigned long) to);
	} else {
		ext_line("ERR addr %lu->%lu rejected: address %lu answers on the bus",
				(unsigned long) from, (unsigned long) to, (unsigned long) to);
	}
}

// zero | zero reset
static void cmd_zero(const char *arg) {
	if (strcmp(arg, "reset") == 0) {
		app_zero_reset();
		ext_line("OK zero reset");
		return;
	}
	if (*arg != '\0') {
		ext_line("ERR usage: zero | zero reset");
		return;
	}
	char list[32];
	size_t pos = 0u;
	for (uint8_t i = 0u; i < APP_SENSOR_COUNT; i++) {
		if (g_app.sensor[i].status == SENSOR_OK && pos + 6u < sizeof(list)) {
			pos += (size_t) snprintf(&list[pos], sizeof(list) - pos, " D%u",
					(unsigned) APP_SENSOR_ADDR(i));
		}
	}
	if (pos == 0u) {
		ext_line("ERR no answering sensors");
		return;
	}
	app_zero_all();
	ext_line("OK zero set:%s", list);
}

/* ----------------------------------------------------------------------------
 * Точки входа
 * ------------------------------------------------------------------------- */

bool usb_cli_ext_exec(const char *cmd, const char *arg) {
	if (strcmp(cmd, "status") == 0) {
		cmd_status(arg);
	} else if (strcmp(cmd, "set") == 0 && word_is(arg, "time")) {
		cmd_set_time(skip_spaces(arg + 4));
	} else if (strcmp(cmd, "addr") == 0) {
		cmd_addr(arg);
	} else if (strcmp(cmd, "zero") == 0) {
		cmd_zero(arg);
	} else if (strcmp(cmd, "files") == 0) {
		cmd_files(arg);
	} else if (strcmp(cmd, "get") == 0) {
		cmd_get(arg);
	} else {
		return false;
	}
	return true;
}

void usb_cli_ext_help(void) {
	ext_line("  status         one-line JSON snapshot (for the PC program)");
	ext_line("  set time YYYY-MM-DD HH:MM:SS   set the clock (and DS3231)");
	ext_line("  addr OLD NEW   change sensor Modbus address (only it on the bus!)");
	ext_line("  zero | zero reset   zero angles of answering sensors / clear zero");
	ext_line("  files          list *.CSV on the card: F,name,bytes,date time");
	ext_line("  get NAME [OFS] send a file: G,name,size,ofs  D,base64...  E,bytes,crc32");
	ext_line("  get abort      stop the transfer");
}
