/*
 * Host-тесты usb_cli_ext.c — команд USB для программы на ПК: base64, CRC-32,
 * разбор времени, JSON status, команды set time / addr / zero и неблокирующие
 * files / get поверх своей имитации FatFs (файлы с содержимым, чтение,
 * смещение, каталог с датами) и кольца передачи usb_cli.c (свободное место,
 * «всё или ничего», хост забирает данные между проходами).
 *
 * Отдельная программа (не test_main.c): здесь FatFs читает файлы, а у
 * test_main.c своя имитация FatFs для записи замеров. Логика прибора (app.c)
 * заменена заглушками — usb_cli_ext.c вызывает только её интерфейс.
 *
 * Сборка и запуск: tests/host/run.sh (вторая программа)
 */
#include "usb_cli_ext.h"
#include "app.h"
#include "bwm427.h"
#include "ff.h"
#include "sd_logger.h"
#include "settings.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_checks, s_fails;

#define CHECK(c) do { s_checks++; if (!(c)) { s_fails++; \
	printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void group(const char *name) {
	printf("[%s]\n", name);
}

/* ------------------------------------------------------------------------ */
/* Заглушки прибора                                                         */
/* ------------------------------------------------------------------------ */

app_state_t g_app;
uint32_t fake_tick;
uint32_t SystemCoreClock = 100000000u;
static int s_set_time_calls, s_zero_calls, s_zero_reset_calls, s_addr_calls;
static uint8_t s_addr_old, s_addr_new;
static settings_info_t s_settings_info;

uint32_t HAL_GetTick(void) {
	return fake_tick;
}

uint8_t app_days_in_month(uint8_t month, uint8_t year) {
	static const uint8_t dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (month < 1 || month > 12) {
		return 31;
	}
	if (month == 2 && (year % 4) == 0) {
		return 29;
	}
	return dim[month - 1];
}

// Как в app.c
bool app_time_valid(const app_time_t *t) {
	return t->year <= 99 && t->month >= 1 && t->month <= 12 && t->date >= 1
			&& t->date <= app_days_in_month(t->month, t->year) && t->hours <= 23
			&& t->minutes <= 59 && t->seconds <= 59;
}

void app_set_time(const app_time_t *t) {
	s_set_time_calls++;
	g_app.time = *t;
}

void app_zero_all(void) {
	s_zero_calls++;
}

void app_zero_reset(void) {
	s_zero_reset_calls++;
}

// Как в app.c: занято / недопустимо -> FAIL, иначе BUSY (выполнит планировщик)
void app_sensor_set_address(uint8_t old_addr, uint8_t new_addr) {
	s_addr_calls++;
	if (g_app.svc_state == SVC_BUSY) {
		return;
	}
	int busy = app_sensor_index(new_addr);
	if (old_addr < 1 || old_addr > 247 || new_addr < 1 || new_addr > 247 || old_addr == new_addr
			|| (busy >= 0 && g_app.sensor[busy].status == SENSOR_OK)) {
		g_app.svc_state = SVC_FAIL;
		return;
	}
	s_addr_old = old_addr;
	s_addr_new = new_addr;
	g_app.svc_state = SVC_BUSY;
}

int app_sensor_index(uint8_t addr) {
	for (int i = 0; i < APP_SENSOR_COUNT; i++) {
		if (APP_SENSOR_ADDR(i) == addr) {
			return i;
		}
	}
	return -1;
}

uint32_t app_stat_avg_us(const app_stat_t *s) {
	return s->n ? (uint32_t) (s->sum_us / s->n) : 0u;
}

const settings_info_t* settings_get_info(void) {
	return &s_settings_info;
}

bool sd_logger_file_name(uint8_t i, char out[SD_NAME_LEN]) {
	if (i >= APP_SENSOR_COUNT || g_app.file_number == 0) {
		return false;
	}
	snprintf(out, SD_NAME_LEN, "20%02u-%02u-%02u_M%03u_D%u.CSV", g_app.time.year, g_app.time.month,
			g_app.time.date, g_app.file_number, APP_SENSOR_ADDR(i));
	return true;
}

/* ------------------------------------------------------------------------ */
/* Кольцо передачи usb_cli.c                                                */
/* ------------------------------------------------------------------------ */

#define TX_SIZE 4096u
static uint32_t s_tx_used;            // в кольце, ещё не забрано хостом
static bool s_tx_ready = true;        // хост слушает
static uint32_t s_tx_dropped;
static char s_cap[1u << 20];          // всё, что ушло в кольцо (по порядку)
static uint32_t s_cap_len;

void usb_cli_out(const char *s, uint32_t n) {
	if (!s_tx_ready) {
		return;
	}
	if (n > TX_SIZE - s_tx_used) {
		s_tx_dropped += n;
		return;
	}
	if (s_cap_len + n < sizeof(s_cap)) {
		memcpy(&s_cap[s_cap_len], s, n);
		s_cap_len += n;
		s_cap[s_cap_len] = '\0';
	}
	s_tx_used += n;
}

uint32_t usb_cli_out_free(void) {
	return s_tx_ready ? TX_SIZE - s_tx_used : 0u;
}

static void tx_reset(void) {
	s_tx_used = 0;
	s_tx_ready = true;
	s_tx_dropped = 0;
	s_cap_len = 0;
	s_cap[0] = '\0';
}

// Хост забрал всё из кольца
static void tx_drain(void) {
	s_tx_used = 0;
}

// Строки вывода (без \r\n) с начала захвата
#define MAX_LINES 4096
static char *s_lines[MAX_LINES];
static int s_nlines;

static void split_lines(void) {
	for (int i = 0; i < s_nlines; i++) {
		free(s_lines[i]);
	}
	s_nlines = 0;
	uint32_t a = 0;
	for (uint32_t i = 0; i + 1 < s_cap_len + 1; i++) {
		if (i + 1 < s_cap_len && s_cap[i] == '\r' && s_cap[i + 1] == '\n') {
			uint32_t n = i - a;
			char *l = malloc(n + 1);
			memcpy(l, &s_cap[a], n);
			l[n] = '\0';
			if (s_nlines < MAX_LINES) {
				s_lines[s_nlines++] = l;
			} else {
				free(l);
			}
			a = i + 2;
			i++;
		}
	}
}

static const char *last_line(void) {
	split_lines();
	return s_nlines ? s_lines[s_nlines - 1] : "";
}

// Команда так, как её отдаёт cli_exec(): первое слово и остаток
static bool run(const char *line) {
	char buf[80];
	snprintf(buf, sizeof(buf), "%s", line);
	char *sp = strchr(buf, ' ');
	const char *arg = "";
	if (sp) {
		*sp = '\0';
		arg = sp + 1;
		while (*arg == ' ') {
			arg++;
		}
	}
	return usb_cli_ext_exec(buf, arg);
}

/* ------------------------------------------------------------------------ */
/* FatFs: файлы с содержимым в памяти                                       */
/* ------------------------------------------------------------------------ */

#define FF_MAX 16
typedef struct {
	bool used;
	char name[64];
	uint8_t *data;
	uint32_t size;
	WORD fdate, ftime;
	BYTE attr;
} ffake_t;
static ffake_t s_fs[FF_MAX];
static int s_open_files, s_open_dirs;      // как таблица блокировок _FS_LOCK
static uint32_t s_reads, s_read_max, s_read_cross; // чтения, наибольшее, через границу сектора
static uint32_t s_readdirs;

static void fs_clear(void) {
	for (int i = 0; i < FF_MAX; i++) {
		free(s_fs[i].data);
		memset(&s_fs[i], 0, sizeof(s_fs[i]));
	}
	s_open_files = s_open_dirs = 0;
	s_reads = s_read_max = s_read_cross = s_readdirs = 0;
}

static WORD fat_date(unsigned y, unsigned m, unsigned d) {
	return (WORD) (((y - 1980u) << 9) | (m << 5) | d);
}

static WORD fat_time(unsigned h, unsigned mi) {
	return (WORD) ((h << 11) | (mi << 5));
}

static ffake_t* fs_add(const char *name, const uint8_t *data, uint32_t size, BYTE attr) {
	for (int i = 0; i < FF_MAX; i++) {
		if (!s_fs[i].used) {
			ffake_t *f = &s_fs[i];
			f->used = true;
			snprintf(f->name, sizeof(f->name), "%s", name);
			f->data = malloc(size ? size : 1);
			if (size) {
				memcpy(f->data, data, size);
			}
			f->size = size;
			f->fdate = fat_date(2026, 10, 7);
			f->ftime = fat_time(14, 5 + (unsigned) i);
			f->attr = attr;
			return f;
		}
	}
	return NULL;
}

static int fs_find(const char *name) {
	for (int i = 0; i < FF_MAX; i++) {
		if (s_fs[i].used) {
			const char *a = s_fs[i].name, *b = name;
			while (*a && *b && toupper((unsigned char) *a) == toupper((unsigned char) *b)) {
				a++;
				b++;
			}
			if (*a == '\0' && *b == '\0') {
				return i; // как FatFs с длинными именами: без учёта регистра
			}
		}
	}
	return -1;
}

FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode) {
	CHECK(mode == (FA_READ | FA_OPEN_EXISTING)); // только чтение
	int i = fs_find(path);
	if (i < 0 || (s_fs[i].attr & AM_DIR)) {
		return FR_NO_FILE;
	}
	memset(fp, 0, sizeof(*fp));
	fp->obj.id = (WORD) (i + 1);
	fp->obj.objsize = s_fs[i].size;
	fp->fptr = 0;
	s_open_files++;
	return FR_OK;
}

static ffake_t* fil_of(FIL *fp) {
	if (fp->obj.id == 0 || fp->obj.id > FF_MAX) {
		return NULL;
	}
	return &s_fs[fp->obj.id - 1];
}

FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br) {
	ffake_t *f = fil_of(fp);
	*br = 0;
	if (!f) {
		return FR_INVALID_OBJECT;
	}
	s_reads++;
	if (btr > s_read_max) {
		s_read_max = btr;
	}
	if (btr && (fp->fptr / 512u) != ((fp->fptr + btr - 1u) / 512u)) {
		s_read_cross++;
	}
	uint32_t left = f->size - (uint32_t) fp->fptr;
	uint32_t n = btr < left ? btr : left;
	memcpy(buff, &f->data[fp->fptr], n);
	fp->fptr += n;
	*br = n;
	return FR_OK;
}

FRESULT f_lseek(FIL *fp, FSIZE_t ofs) {
	ffake_t *f = fil_of(fp);
	if (!f) {
		return FR_INVALID_OBJECT;
	}
	fp->fptr = ofs > f->size ? f->size : ofs;
	return FR_OK;
}

FRESULT f_close(FIL *fp) {
	if (!fil_of(fp)) {
		return FR_INVALID_OBJECT;
	}
	fp->obj.id = 0;
	s_open_files--;
	return FR_OK;
}

FRESULT f_opendir(DIR *dp, const TCHAR *path) {
	CHECK(strcmp(path, "/") == 0);
	memset(dp, 0, sizeof(*dp));
	dp->obj.id = 1;
	dp->dptr = 0;
	s_open_dirs++;
	return FR_OK;
}

FRESULT f_readdir(DIR *dp, FILINFO *fno) {
	s_readdirs++;
	while (dp->dptr < FF_MAX && !s_fs[dp->dptr].used) {
		dp->dptr++;
	}
	if (dp->dptr >= FF_MAX) {
		fno->fname[0] = '\0';
		return FR_OK;
	}
	const ffake_t *f = &s_fs[dp->dptr++];
	snprintf(fno->fname, sizeof(fno->fname), "%s", f->name);
	fno->fsize = f->size;
	fno->fdate = f->fdate;
	fno->ftime = f->ftime;
	fno->fattrib = f->attr;
	return FR_OK;
}

FRESULT f_closedir(DIR *dp) {
	if (dp->obj.id == 0) {
		return FR_INVALID_OBJECT;
	}
	dp->obj.id = 0;
	s_open_dirs--;
	return FR_OK;
}

/* ------------------------------------------------------------------------ */
/* Разбор ответа get                                                        */
/* ------------------------------------------------------------------------ */

static int b64v(char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

// Base64 -> байты; -1 — ошибка
static int b64dec(const char *s, uint8_t *out) {
	size_t n = strlen(s);
	if (n % 4) {
		return -1;
	}
	int k = 0;
	for (size_t i = 0; i < n; i += 4) {
		int a = b64v(s[i]), b = b64v(s[i + 1]);
		int c = s[i + 2] == '=' ? 0 : b64v(s[i + 2]);
		int d = s[i + 3] == '=' ? 0 : b64v(s[i + 3]);
		if (a < 0 || b < 0 || c < 0 || d < 0) {
			return -1;
		}
		uint32_t v = ((uint32_t) a << 18) | ((uint32_t) b << 12) | ((uint32_t) c << 6) | (uint32_t) d;
		out[k++] = (uint8_t) (v >> 16);
		if (s[i + 2] != '=') out[k++] = (uint8_t) (v >> 8);
		if (s[i + 3] != '=') out[k++] = (uint8_t) v;
	}
	return k;
}

typedef struct {
	bool g_ok, e_ok, ok;
	uint32_t g_size, g_ofs;
	uint32_t e_bytes, e_crc;
	uint8_t *data;
	uint32_t len;
	uint32_t d_lines, d_max;
	char err[128];
} get_result_t;

static void parse_get(get_result_t *r) {
	memset(r, 0, sizeof(*r));
	r->data = malloc(1u << 20);
	split_lines();
	for (int i = 0; i < s_nlines; i++) {
		const char *l = s_lines[i];
		if (strncmp(l, "G,", 2) == 0) {
			const char *c = strrchr(l, ',');
			r->g_ofs = (uint32_t) strtoul(c + 1, NULL, 10);
			const char *c2 = c - 1;
			while (c2 > l && *c2 != ',') c2--;
			r->g_size = (uint32_t) strtoul(c2 + 1, NULL, 10);
			r->g_ok = true;
		} else if (strncmp(l, "D,", 2) == 0) {
			int k = b64dec(l + 2, r->data + r->len);
			if (k < 0) {
				snprintf(r->err, sizeof(r->err), "bad base64");
			} else {
				r->len += (uint32_t) k;
				r->d_lines++;
				if ((uint32_t) k > r->d_max) r->d_max = (uint32_t) k;
			}
		} else if (strncmp(l, "E,", 2) == 0) {
			r->e_ok = sscanf(l, "E,%u,%x", &r->e_bytes, &r->e_crc) == 2;
		} else if (strcmp(l, "OK") == 0) {
			r->ok = true;
		} else if (strncmp(l, "ERR", 3) == 0) {
			snprintf(r->err, sizeof(r->err), "%s", l);
		}
	}
}

/* ------------------------------------------------------------------------ */
/* Тесты                                                                    */
/* ------------------------------------------------------------------------ */

static void test_base64(void) {
	group("base64 (RFC 4648)");
	static const char *in[] = { "", "f", "fo", "foo", "foob", "fooba", "foobar" };
	static const char *out[] = { "", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy" };
	char buf[64];
	for (int i = 0; i < 7; i++) {
		size_t n = usb_cli_ext_base64(buf, (const uint8_t *) in[i], strlen(in[i]));
		CHECK(n == strlen(out[i]) && strcmp(buf, out[i]) == 0);
	}
	uint8_t all[256], back[256];
	for (int i = 0; i < 256; i++) {
		all[i] = (uint8_t) i;
	}
	char big[400];
	for (int n = 0; n <= 48; n++) { // все длины строки D
		size_t m = usb_cli_ext_base64(big, all + 200, (size_t) n);
		CHECK(m == (size_t) ((n + 2) / 3 * 4));
		CHECK(b64dec(big, back) == n && memcmp(back, all + 200, (size_t) n) == 0);
	}
	CHECK(usb_cli_ext_base64(big, all, 256) == 344);
	CHECK(b64dec(big, back) == 256 && memcmp(back, all, 256) == 0);
	printf("  ok\n");
}

static void test_crc32(void) {
	group("CRC-32 (как zlib)");
	CHECK(usb_cli_ext_crc32(0, (const uint8_t *) "123456789", 9) == 0xCBF43926u);
	CHECK(usb_cli_ext_crc32(0, (const uint8_t *) "", 0) == 0u);
	CHECK(usb_cli_ext_crc32(0, (const uint8_t *) "a", 1) == 0xE8B7BE43u);
	// По кускам — то же, что целиком
	uint8_t buf[1000];
	for (int i = 0; i < 1000; i++) {
		buf[i] = (uint8_t) (i * 37 + 11);
	}
	uint32_t whole = usb_cli_ext_crc32(0, buf, 1000);
	uint32_t part = usb_cli_ext_crc32(0, buf, 333);
	part = usb_cli_ext_crc32(part, buf + 333, 512);
	part = usb_cli_ext_crc32(part, buf + 845, 155);
	CHECK(whole == part);
	printf("  ok\n");
}

static void test_parse_time(void) {
	group("set time: разбор даты и времени");
	app_time_t t;
	CHECK(usb_cli_ext_parse_time("2026-10-07 20:41:05", &t));
	CHECK(t.year == 26 && t.month == 10 && t.date == 7 && t.hours == 20 && t.minutes == 41
			&& t.seconds == 5);
	CHECK(usb_cli_ext_parse_time("2000-01-01t00:00:00", &t) && t.year == 0);
	CHECK(usb_cli_ext_parse_time("2099-12-31 23:59:59", &t) && t.year == 99);
	CHECK(usb_cli_ext_parse_time("2028-02-29 12:00:00", &t));  // високосный
	CHECK(!usb_cli_ext_parse_time("2027-02-29 12:00:00", &t));
	CHECK(!usb_cli_ext_parse_time("2026-13-01 12:00:00", &t));
	CHECK(!usb_cli_ext_parse_time("2026-04-31 12:00:00", &t));
	CHECK(!usb_cli_ext_parse_time("2026-10-07 24:00:00", &t));
	CHECK(!usb_cli_ext_parse_time("2026-10-07 23:60:00", &t));
	CHECK(!usb_cli_ext_parse_time("1999-12-31 23:59:59", &t));
	CHECK(!usb_cli_ext_parse_time("2100-01-01 00:00:00", &t));
	CHECK(!usb_cli_ext_parse_time("2026-10-07 20:41", &t));
	CHECK(!usb_cli_ext_parse_time("2026-10-07 20:41:05 ", &t));
	CHECK(!usb_cli_ext_parse_time("2026/10/07 20:41:05", &t));
	CHECK(!usb_cli_ext_parse_time("2026-1a-07 20:41:05", &t));
	CHECK(!usb_cli_ext_parse_time("", &t));
	printf("  ok\n");
}

// Состояние прибора для status: два датчика, качка, карта, АКБ
static void setup_app(void) {
	memset(&g_app, 0, sizeof(g_app));
	app_sensor_t *a = &g_app.sensor[0], *b = &g_app.sensor[1];
	a->addr = 2;
	a->status = SENSOR_OK;
	a->x = 1.234f;
	a->y = -0.5674f;
	a->filt_x = 1.354f;
	a->filt_y = -0.5674f;
	a->off_x = 0.12f;
	a->roll_x = 0.123f;
	a->roll_y = 0.456f;
	a->calm_x = true;
	a->roll_fill_s = 20;
	a->ok_count = 1234;
	a->err_count = 3;
	a->last_ok_ms = 999990;
	a->timeout_count = 2;
	a->crc_count = 1;
	a->timeout_us = 30000;
	a->lat_first = (app_stat_t) { .n = 4, .last_us = 1500, .min_us = 1400, .max_us = 1700, .sum_us = 6200 };
	a->lat_done = (app_stat_t) { .n = 4, .last_us = 2400, .min_us = 2300, .max_us = 2600, .sum_us = 9800 };
	a->last_err = 1; // TIMEOUT
	b->addr = 3;
	b->status = SENSOR_LOST;
	b->x = -12.5f;
	b->y = 0.0f;
	b->roll_x = 2.5f;
	b->roll_y = 0.05f;
	b->calm_y = true;
	b->roll_fill_s = 12;
	b->ok_count = 0;
	g_app.log_freq_hz = 10;
	g_app.ema_alpha = 0.15f;
	g_app.actual_rate_hz = 9.96f;
	g_app.bus_gap_ms = 15;
	g_app.theme = APP_THEME_LIGHT;
	g_app.sd_state = SD_READY;
	g_app.file_number = 7;
	g_app.sd_total_mb = 7600;
	g_app.sd_free_mb = APP_SD_FREE_UNKNOWN;
	g_app.battery_v = 12.1f;
	g_app.battery_present = true;
	g_app.battery_pct = APP_BAT_PCT_NONE;
	g_app.bat_alarm_v = 10.0f;
	g_app.is_stable = false;
	g_app.stab_span = 2.5f;
	g_app.stab_sensor = 1;
	g_app.stab_axis = 0;
	g_app.stab_fill_s = 12;
	g_app.roll_window_s = 20;
	g_app.roll_rate_hz = 5;
	g_app.roll_calm_deg = 1.5f;
	g_app.roll_hyst_deg = 0.2f;
	g_app.time = (app_time_t) { .year = 26, .month = 10, .date = 7, .hours = 20, .minutes = 41, .seconds = 5 };
	g_app.rtc_present = true;
	g_app.svc_state = SVC_IDLE;
	g_app.diag.uptime_s = 1000;
	g_app.diag.cpu_load_pct = 12;
	g_app.diag.loops_per_s = 2400;
	g_app.diag.loop_max_ms = 5;
	g_app.bus.cycle = (app_stat_t) { .n = 10, .last_us = 22000, .min_us = 21000, .max_us = 25000, .sum_us = 220000 };
	g_app.bus.gap = (app_stat_t) { .n = 10, .last_us = 15100, .min_us = 15000, .max_us = 15300, .sum_us = 151000 };
	g_app.bus.idle = (app_stat_t) { .n = 10, .last_us = 70000, .min_us = 60000, .max_us = 80000, .sum_us = 700000 };
	fake_tick = 1000000;
	s_settings_info.pending = true;
}

// Скобки и кавычки сбалансированы, нет висячих запятых — достаточно для одной строки
static bool json_shape_ok(const char *s) {
	int depth = 0;
	bool in_str = false;
	char prev = 0;
	for (; *s && *s != '\r'; s++) {
		char c = *s;
		if (in_str) {
			if (c == '\\') {
				s++;
				continue;
			}
			if (c == '"') {
				in_str = false;
			}
		} else if (c == '"') {
			in_str = true;
		} else if (c == '{' || c == '[') {
			depth++;
		} else if (c == '}' || c == ']') {
			if (prev == ',' || --depth < 0) {
				return false;
			}
		} else if (c == ',' && (prev == ',' || prev == '{' || prev == '[')) {
			return false;
		}
		prev = c;
	}
	return depth == 0 && !in_str;
}

static void test_status_json(void) {
	group("status: JSON одной строкой");
	setup_app();
	static char js[2048];
	size_t n = usb_cli_ext_status_json(js, sizeof(js), fake_tick);
	CHECK(n > 0 && n == strlen(js));
	CHECK(n < 1400);
	CHECK(js[0] == '{' && n > 3 && strcmp(js + n - 3, "}\r\n") == 0);
	CHECK(strchr(js, '\n') == js + n - 1); // одна строка
	CHECK(json_shape_ok(js));
	CHECK(strstr(js, "{\"v\":1,\"fw\":\"") == js);
	CHECK(strstr(js, ",\"time\":\"2026-10-07 20:41:05\",\"rtc\":true,") != NULL);
	CHECK(strstr(js, ",\"freq\":10,\"alpha\":0.15,\"gap\":15,\"theme\":\"light\",\"batalarm\":10.00,"
			"\"rollwin\":20,\"rollhz\":5,\"rollcalm\":1.50,\"rollhyst\":0.20,\"save_pending\":true,") != NULL);
	CHECK(strstr(js, ",\"rate\":10.0,\"cpu\":12,\"loops\":2400,\"loop_max\":5,") != NULL);
	CHECK(strstr(js, ",\"sens\":[{\"addr\":2,\"st\":\"OK\",\"x\":1.234,\"y\":-0.567,\"ox\":0.120,\"oy\":0.000,\"roll_x\":0.123,\"roll_y\":0.456,"
			"\"calm_x\":true,\"calm_y\":false,\"fill\":20,\"ok\":1234,\"err\":3,\"garbled\":0,"
			"\"last_err\":\"TIMEOUT\",\"age\":10,\"to\":2,\"crc\":1,\"bad\":0,\"tmo_us\":30000,"
			"\"lat\":[4,1500,1400,1550,1700],\"done\":[4,2400,2300,2450,2600]}") != NULL);
	CHECK(strstr(js, "{\"addr\":3,\"st\":\"LOST\",\"x\":-12.500,\"y\":0.000,") != NULL);
	CHECK(strstr(js, "\"age\":null") != NULL); // Д3 ни разу не ответил
	CHECK(strstr(js, ",\"stable\":false,\"span\":2.500,\"stab_addr\":3,\"stab_axis\":\"X\",\"stab_fill\":12,") != NULL);
	CHECK(strstr(js, ",\"bus\":{\"fallbacks\":0,\"gap\":[10,15100,15000,15100,15300],"
			"\"idle\":[10,70000,60000,70000,80000],\"cyc\":[10,22000,21000,22000,25000],"
			"\"max_rate\":45.45}") != NULL);
	CHECK(strstr(js, ",\"sd\":\"READY\",\"sd_err\":0,\"file\":7,"
			"\"names\":[\"2026-10-07_M007_D2.CSV\",\"2026-10-07_M007_D3.CSV\"],\"rec_mask\":0,"
			"\"rows\":0,\"rec_s\":0,\"sd_total\":7600,\"sd_free\":null,") != NULL);
	CHECK(strstr(js, ",\"bat_v\":12.10,\"bat_present\":true,\"bat_low\":false,\"bat_pct\":null,"
			"\"bat_sat\":false,\"rec_sw\":false,\"svc\":\"IDLE\",\"xfer\":\"idle\"}") != NULL);

	// Неизвестное, NaN и запись
	g_app.sd_free_mb = 7500;
	g_app.battery_pct = 85;
	g_app.sensor[0].x = 0.0f / 0.0f;
	g_app.stab_sensor = APP_STAB_NONE;
	g_app.sd_state = SD_RECORDING;
	g_app.rec_start_ms = fake_tick - 65000;
	g_app.rec_sensor_mask = 3;
	g_app.rec_rows = 1300;
	g_app.file_number = 0;
	n = usb_cli_ext_status_json(js, sizeof(js), fake_tick);
	CHECK(n > 0 && json_shape_ok(js));
	CHECK(strstr(js, "\"x\":null,") != NULL);
	CHECK(strstr(js, "\"stab_addr\":null,\"stab_axis\":null,") != NULL);
	CHECK(strstr(js, "\"sd\":\"RECORDING\",\"sd_err\":0,\"file\":0,\"names\":[],\"rec_mask\":3,"
			"\"rows\":1300,\"rec_s\":65,") != NULL);
	CHECK(strstr(js, "\"sd_free\":7500,") != NULL);
	CHECK(strstr(js, "\"bat_pct\":85,") != NULL);
	// Мало места — 0, без обрезанного JSON
	CHECK(usb_cli_ext_status_json(js, 200, fake_tick) == 0);

	// Самый длинный JSON: все счётчики и статистика — по 10 цифр, отрицательные углы
	for (int i = 0; i < APP_SENSOR_COUNT; i++) {
		app_sensor_t *s = &g_app.sensor[i];
		s->status = SENSOR_LOST;
		s->x = s->y = s->off_x = s->off_y = -99.999f;
		s->roll_x = s->roll_y = 180.0f;
		s->ok_count = s->err_count = s->garbled_count = 4294967295u;
		s->last_ok_ms = 1;
		s->timeout_count = s->crc_count = s->frame_count = s->timeout_us = 4294967295u;
		s->last_err = BWM427_EXCEPTION;
		s->roll_fill_s = 255;
		app_stat_t big = { .n = 4294967295u, .last_us = 4294967295u, .min_us = 4294967295u,
				.max_us = 4294967295u, .sum_us = 18446744073709551615ull };
		s->lat_first = s->lat_done = big;
	}
	app_stat_t big = { .n = 4294967295u, .last_us = 4294967295u, .min_us = 4294967295u,
			.max_us = 4294967295u, .sum_us = 18446744073709551615ull };
	g_app.bus.gap = g_app.bus.idle = g_app.bus.cycle = big;
	g_app.bus.timer_fallbacks = 4294967295u;
	g_app.diag.uptime_s = g_app.diag.loops_per_s = 4294967295u;
	g_app.rec_rows = g_app.sd_total_mb = g_app.sd_free_mb = 4000000000u;
	g_app.file_number = 9999;
	g_app.sd_err = 255;
	g_app.svc_state = SVC_FAIL;
	g_app.stab_sensor = 0;
	g_app.stab_span = -180.0f;
	g_app.battery_v = 99.99f;
	fake_tick = 4294967295u;
	n = usb_cli_ext_status_json(js, sizeof(js), fake_tick);
	CHECK(n > 0 && json_shape_ok(js));
	CHECK(n + 4 <= 1792); // буфер прошивки 2048, запас в кольце для ответа 1792
	printf("  наибольшая длина %u байт\n", (unsigned) n);

	// Образец для тестов программы на ПК (pc/tests): build_host/status_sample.json
	setup_app();
	n = usb_cli_ext_status_json(js, sizeof(js), fake_tick);
	FILE *f = fopen("build_host/status_sample.json", "wb");
	if (f) {
		fwrite(js, 1, n, f);
		fclose(f);
	}
	printf("  длина %u байт (буфер 2048)\n", (unsigned) n);
	printf("  ok\n");
}

static void test_commands(void) {
	group("status, set time, addr, zero");
	setup_app();
	tx_reset();
	CHECK(run("status"));
	split_lines();
	CHECK(s_nlines == 2 && s_lines[0][0] == '{' && strcmp(s_lines[1], "OK") == 0);
	tx_reset();
	CHECK(run("status now"));
	CHECK(strncmp(last_line(), "ERR usage", 9) == 0);
	// Кольцо почти полно — ответ ERR, а не обрезанный JSON
	tx_reset();
	s_tx_used = TX_SIZE - 600;
	CHECK(run("status"));
	CHECK(strcmp(last_line(), "ERR tx busy, retry") == 0 && s_nlines == 1);
	// Хост не слушает — ничего не выдаётся и не падает
	tx_reset();
	s_tx_ready = false;
	CHECK(run("status"));
	CHECK(s_cap_len == 0);

	// Чужие команды — в usb_cli.c
	tx_reset();
	CHECK(!run("set freq 10"));
	CHECK(!run("set"));
	CHECK(!run("help"));
	CHECK(!run("timeset"));
	CHECK(s_cap_len == 0);

	// set time
	s_set_time_calls = 0;
	CHECK(run("set time 2027-01-02 03:04:05"));
	CHECK(s_set_time_calls == 1 && g_app.time.year == 27 && g_app.time.month == 1
			&& g_app.time.date == 2 && g_app.time.seconds == 5);
	CHECK(strcmp(last_line(), "OK time=2027-01-02 03:04:05 (rtc yes)") == 0);
	tx_reset();
	CHECK(run("set time  2027-02-30 03:04:05"));
	CHECK(s_set_time_calls == 1 && strncmp(last_line(), "ERR usage: set time", 19) == 0);
	CHECK(run("set time"));
	CHECK(s_set_time_calls == 1 && strncmp(last_line(), "ERR usage", 9) == 0);

	// addr
	setup_app();
	tx_reset();
	s_addr_calls = 0;
	CHECK(run("addr 1 3")); // Д3 не отвечает — можно
	CHECK(g_app.svc_state == SVC_BUSY && s_addr_old == 1 && s_addr_new == 3);
	CHECK(strcmp(last_line(), "OK addr 1->3 started (result: status \"svc\")") == 0);
	CHECK(run("addr 1 3"));
	CHECK(strcmp(last_line(), "ERR busy: address change in progress") == 0 && s_addr_calls == 1);
	g_app.svc_state = SVC_OK;
	CHECK(run("addr 1 2")); // Д2 отвечает — адрес занят
	CHECK(g_app.svc_state == SVC_FAIL);
	CHECK(strcmp(last_line(), "ERR addr 1->2 rejected: address 2 answers on the bus") == 0);
	CHECK(run("addr 2 2"));
	CHECK(strncmp(last_line(), "ERR addresses must be", 21) == 0);
	CHECK(run("addr 0 5"));
	CHECK(strncmp(last_line(), "ERR addresses must be", 21) == 0);
	CHECK(run("addr 1 248"));
	CHECK(strncmp(last_line(), "ERR addresses must be", 21) == 0);
	CHECK(run("addr 1"));
	CHECK(strncmp(last_line(), "ERR usage: addr", 15) == 0);
	CHECK(run("addr 1 2 3"));
	CHECK(strncmp(last_line(), "ERR usage: addr", 15) == 0);
	CHECK(run("addr a b"));
	CHECK(strncmp(last_line(), "ERR usage: addr", 15) == 0);
	g_app.sd_state = SD_RECORDING;
	CHECK(run("addr 1 3"));
	CHECK(strcmp(last_line(), "ERR busy recording") == 0);
	CHECK(s_addr_calls == 2);

	// zero
	setup_app();
	tx_reset();
	s_zero_calls = s_zero_reset_calls = 0;
	CHECK(run("zero"));
	CHECK(s_zero_calls == 1 && strcmp(last_line(), "OK zero set: D2") == 0);
	g_app.sensor[1].status = SENSOR_OK;
	CHECK(run("zero"));
	CHECK(s_zero_calls == 2 && strcmp(last_line(), "OK zero set: D2 D3") == 0);
	CHECK(run("zero reset"));
	CHECK(s_zero_reset_calls == 1 && strcmp(last_line(), "OK zero reset") == 0);
	g_app.sensor[0].status = SENSOR_LOST;
	g_app.sensor[1].status = SENSOR_ABSENT;
	CHECK(run("zero"));
	CHECK(s_zero_calls == 2 && strcmp(last_line(), "ERR no answering sensors") == 0);
	CHECK(run("zero all"));
	CHECK(strncmp(last_line(), "ERR usage: zero", 15) == 0);

	// help
	tx_reset();
	usb_cli_ext_help();
	split_lines();
	CHECK(s_nlines == 7);
	printf("  ok\n");
}

// Проходы суперцикла: хост забирает всё между проходами
static int pump(int max_passes, uint32_t ms_per_pass) {
	int passes = 0;
	for (; passes < max_passes; passes++) {
		uint32_t reads_before = s_reads;
		tx_drain();
		fake_tick += ms_per_pass;
		usb_cli_ext_task(fake_tick);
		CHECK(s_reads - reads_before <= 1); // не больше одного чтения карты за проход
		split_lines();
		if (s_nlines && (strncmp(s_lines[s_nlines - 1], "OK", 2) == 0
				|| strncmp(s_lines[s_nlines - 1], "ERR", 3) == 0)) {
			break;
		}
	}
	return passes;
}

static uint8_t *make_data(uint32_t n, uint32_t seed) {
	uint8_t *d = malloc(n ? n : 1);
	for (uint32_t i = 0; i < n; i++) {
		seed = seed * 1103515245u + 12345u;
		d[i] = (uint8_t) (seed >> 16);
	}
	return d;
}

static void test_files(void) {
	group("files: список *.CSV, по частям");
	setup_app();
	fs_clear();
	tx_reset();
	uint8_t x[3] = { 1, 2, 3 };
	fs_add("2026-10-06_M006_D2.CSV", x, 3, AM_ARC);
	fs_add("readme.txt", x, 3, AM_ARC);
	fs_add("2026-10-06_M006_D3.CSV", x, 2, AM_ARC);
	fs_add("System Volume Information", NULL, 0, AM_DIR | AM_HID | AM_SYS);
	fs_add("M_005_2.csv", x, 1, AM_ARC);
	fs_add("hidden.CSV", x, 1, AM_ARC | AM_HID);
	ffake_t *big = fs_add("2026-10-07_M007_D2.CSV", x, 0, AM_ARC);
	big->size = 1234567; // размер только из каталога
	big->fdate = fat_date(2026, 10, 7);
	big->ftime = fat_time(9, 3);
	fs_add("folder.csv", NULL, 0, AM_DIR);
	CHECK(run("files"));
	// Первые CLI_EXT_LIST_STEP записей — сразу, остальное — в проходах
	CHECK(s_readdirs <= 4);
	int passes = pump(50, 1);
	CHECK(passes >= 1 && passes < 10);
	split_lines();
	CHECK(s_nlines == 5);
	if (s_nlines == 5) {
		CHECK(strcmp(s_lines[0], "F,2026-10-06_M006_D2.CSV,3,2026-10-07 14:05") == 0);
		CHECK(strcmp(s_lines[1], "F,2026-10-06_M006_D3.CSV,2,2026-10-07 14:07") == 0);
		CHECK(strcmp(s_lines[2], "F,M_005_2.csv,1,2026-10-07 14:09") == 0);
		CHECK(strcmp(s_lines[3], "F,2026-10-07_M007_D2.CSV,1234567,2026-10-07 09:03") == 0);
		CHECK(strcmp(s_lines[4], "OK 4") == 0);
	}
	CHECK(s_open_dirs == 0);

	// Кольцо занято: список ждёт, ничего не теряется
	tx_reset();
	s_tx_used = TX_SIZE - 1000; // меньше запаса
	CHECK(run("files"));
	CHECK(s_cap_len == 0);
	fake_tick += 10;
	usb_cli_ext_task(fake_tick);
	CHECK(s_cap_len == 0);
	pump(50, 1);
	CHECK(strcmp(last_line(), "OK 4") == 0 && s_nlines == 5);
	CHECK(s_tx_dropped == 0);

	// Пустая карта
	fs_clear();
	tx_reset();
	CHECK(run("files"));
	pump(10, 1);
	CHECK(strcmp(last_line(), "OK 0") == 0 && s_nlines == 1);

	// Нет карты, запись, ошибка карты, лишний аргумент
	tx_reset();
	g_app.sd_state = SD_NO_CARD;
	CHECK(run("files"));
	CHECK(strcmp(last_line(), "ERR no card") == 0);
	g_app.sd_state = SD_RECORDING;
	CHECK(run("files"));
	CHECK(strcmp(last_line(), "ERR busy recording") == 0);
	g_app.sd_state = SD_ERROR;
	g_app.sd_err = 1;
	CHECK(run("files"));
	CHECK(strncmp(last_line(), "ERR card error (FatFs 1)", 24) == 0);
	g_app.sd_state = SD_READY;
	CHECK(run("files all"));
	CHECK(strcmp(last_line(), "ERR usage: files") == 0);
	CHECK(s_open_dirs == 0 && s_open_files == 0);
	printf("  ok\n");
}

static void check_get(const char *cmd, const uint8_t *data, uint32_t size, uint32_t ofs) {
	tx_reset();
	uint32_t reads0 = s_reads;
	CHECK(run(cmd));
	pump(100000, 1);
	get_result_t r;
	parse_get(&r);
	CHECK(r.err[0] == '\0');
	CHECK(r.g_ok && r.g_size == size && r.g_ofs == ofs);
	CHECK(r.len == size - ofs && memcmp(r.data, data + ofs, size - ofs) == 0);
	CHECK(r.e_ok && r.e_bytes == size - ofs);
	CHECK(r.e_crc == usb_cli_ext_crc32(0, data + ofs, size - ofs));
	CHECK(r.ok);
	CHECK(r.d_max <= 48);
	CHECK(s_read_max <= 512 && s_read_cross == 0);
	CHECK(s_open_files == 0);
	CHECK(s_tx_dropped == 0);
	(void) reads0;
	free(r.data);
}

static void test_get(void) {
	group("get: передача файла по частям");
	setup_app();
	fs_clear();
	uint32_t size = 5000;
	uint8_t *data = make_data(size, 7);
	fs_add("2026-10-07_M007_D2.CSV", data, size, AM_ARC);
	uint8_t *small = make_data(47, 9);
	fs_add("S.CSV", small, 47, AM_ARC);
	fs_add("EMPTY.CSV", NULL, 0, AM_ARC);
	uint8_t *exact = make_data(1024, 3);
	fs_add("EXACT.CSV", exact, 1024, AM_ARC);

	check_get("get 2026-10-07_m007_d2.csv", data, size, 0); // регистр не важен
	check_get("get 2026-10-07_M007_D2.CSV 1000", data, size, 1000);
	check_get("get 2026-10-07_M007_D2.CSV 4999", data, size, 4999);
	check_get("get 2026-10-07_M007_D2.CSV 5000", data, size, 5000); // нечего передавать
	check_get("get 2026-10-07_M007_D2.CSV 700", data, size, 700);  // не с границы сектора
	check_get("get s.csv", small, 47, 0);
	check_get("get exact.csv", exact, 1024, 0);

	// Пустой файл: G, E и OK сразу, без проходов
	tx_reset();
	CHECK(run("get empty.csv"));
	split_lines();
	CHECK(s_nlines == 3 && strcmp(s_lines[0], "G,empty.csv,0,0") == 0
			&& strcmp(s_lines[1], "E,0,00000000") == 0 && strcmp(s_lines[2], "OK") == 0);
	CHECK(s_open_files == 0);

	// Скорость: 5000 байт — по сектору за проход, ~10 проходов
	tx_reset();
	CHECK(run("get 2026-10-07_M007_D2.CSV"));
	int passes = pump(1000, 1);
	CHECK(passes >= 8 && passes <= 12);

	// Ошибки
	tx_reset();
	CHECK(run("get nofile.csv"));
	CHECK(strcmp(last_line(), "ERR no such file") == 0);
	CHECK(run("get 2026-10-07_M007_D2.CSV 5001"));
	CHECK(strcmp(last_line(), "ERR offset 5001 > size 5000") == 0);
	CHECK(run("get dir/file.csv"));
	CHECK(strcmp(last_line(), "ERR only files in the card root") == 0);
	CHECK(run("get"));
	CHECK(strncmp(last_line(), "ERR usage: get", 14) == 0);
	g_app.sd_state = SD_RECORDING;
	CHECK(run("get s.csv"));
	CHECK(strcmp(last_line(), "ERR busy recording") == 0);
	g_app.sd_state = SD_NO_CARD;
	CHECK(run("get s.csv"));
	CHECK(strcmp(last_line(), "ERR no card") == 0);
	g_app.sd_state = SD_READY;
	CHECK(s_open_files == 0);

	// get abort посреди передачи
	tx_reset();
	CHECK(run("get 2026-10-07_M007_D2.CSV"));
	pump(2, 1);
	CHECK(run("get abort"));
	CHECK(strcmp(last_line(), "OK aborted") == 0 && s_open_files == 0);
	CHECK(run("get abort"));
	CHECK(strcmp(last_line(), "OK nothing to abort") == 0);

	// Запись началась посреди передачи: файл закрыт (блокировка FatFs снята)
	tx_reset();
	CHECK(run("get 2026-10-07_M007_D2.CSV"));
	pump(2, 1);
	g_app.sd_state = SD_RECORDING;
	pump(5, 1);
	CHECK(strcmp(last_line(), "ERR aborted: recording started") == 0 && s_open_files == 0);
	CHECK(strstr(s_cap, "E,") == NULL);
	g_app.sd_state = SD_READY;

	// Карту вынули
	tx_reset();
	CHECK(run("get 2026-10-07_M007_D2.CSV"));
	g_app.sd_state = SD_NO_CARD;
	pump(5, 1);
	CHECK(strcmp(last_line(), "ERR aborted: card removed") == 0 && s_open_files == 0);
	g_app.sd_state = SD_READY;

	// Как в суперцикле прибора: usb_cli_task(now) берёт now в начале прохода, а
	// команда (f_open читает карту) идёт миллисекунды, так что остаток прохода
	// вызывает usb_cli_ext_task со временем РАНЬШЕ начала передачи. Прошивка 1.4
	// считала это «хост не читает» и обрывала get/files после первой порции.
	for (int k = 0; k < 2; k++) {
		tx_reset();
		uint32_t pass_start = fake_tick;
		fake_tick += 3; // команда заняла 3 мс
		CHECK(run(k ? "files" : "get 2026-10-07_M007_D2.CSV"));
		usb_cli_ext_task(pass_start);
		split_lines();
		CHECK(strncmp(last_line(), "ERR", 3) != 0);
		pump(100000, 1);
		CHECK(strncmp(last_line(), "OK", 2) == 0 && s_open_files == 0 && s_open_dirs == 0);
		if (!k) {
			get_result_t rr;
			parse_get(&rr);
			CHECK(rr.ok && rr.len == size && memcmp(rr.data, data, size) == 0);
			free(rr.data);
		}
	}

	// Хост не забирает данные: передача ждёт, через 5 с — прервана
	tx_reset();
	CHECK(run("get 2026-10-07_M007_D2.CSV"));
	uint32_t cap0 = s_cap_len;
	for (int i = 0; i < 60; i++) {
		fake_tick += 100;
		usb_cli_ext_task(fake_tick); // без tx_drain()
	}
	CHECK(s_open_files == 0);
	CHECK(s_cap_len - cap0 < 4096);
	// ... а если забирает медленно — доходит до конца
	tx_reset();
	CHECK(run("get 2026-10-07_M007_D2.CSV"));
	for (int i = 0; i < 2000 && s_open_files; i++) {
		fake_tick += 300;
		if (i % 5 == 0) {
			tx_drain();
		}
		usb_cli_ext_task(fake_tick);
	}
	get_result_t r;
	parse_get(&r);
	CHECK(r.ok && r.len == size && memcmp(r.data, data, size) == 0);
	CHECK(r.e_crc == usb_cli_ext_crc32(0, data, size));
	free(r.data);

	// Хост закрыл / открыл порт посреди передачи — прервана молча, файл закрыт
	tx_reset();
	CHECK(run("get 2026-10-07_M007_D2.CSV"));
	pump(2, 1);
	uint32_t cap1 = s_cap_len;
	usb_cli_ext_abort();
	CHECK(s_open_files == 0 && s_cap_len == cap1);
	pump(3, 1);
	CHECK(s_cap_len == cap1); // больше ничего не выдаётся
	usb_cli_ext_abort();      // повторно — без последствий
	CHECK(s_open_files == 0);

	// Новая команда files отменяет передачу (файл закрыт)
	tx_reset();
	CHECK(run("get 2026-10-07_M007_D2.CSV"));
	CHECK(run("files"));
	CHECK(s_open_files == 0);
	pump(20, 1);

	// status во время передачи помещается (запас в кольце)
	tx_reset();
	CHECK(run("get 2026-10-07_M007_D2.CSV"));
	usb_cli_ext_task(++fake_tick); // кольцо заполнено до запаса
	CHECK(run("status"));
	CHECK(strstr(s_cap, "\"xfer\":\"get\"") != NULL);
	CHECK(s_tx_dropped == 0);
	pump(1000, 1);
	CHECK(s_open_files == 0);

	free(data);
	free(small);
	free(exact);
	fs_clear();
	printf("  ok\n");
}

int main(void) {
	test_base64();
	test_crc32();
	test_parse_time();
	test_status_json();
	test_commands();
	test_files();
	test_get();
	printf("\n%d проверок, %d ошибок\n", s_checks, s_fails);
	return s_fails ? 1 : 0;
}
