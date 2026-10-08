/*
 * sd_logger.c — запись замеров на SD-карту (FatFs, SPI2).
 *
 * На каждый датчик свой файл "YYYY-MM-DD_MNNN_Dk.CSV", например
 * 2026-10-06_M007_D2.CSV: дата начала замера (часы прибора), NNN — номер
 * замера (g_app.file_number, не меньше трёх цифр), k — Modbus-адрес датчика.
 * Номера сквозные, не по дням: следующий = наибольший номер на карте + 1
 * (учитываются и старые имена M_NNN_k.CSV). Существующий файл никогда не
 * перезаписывается (FA_CREATE_NEW, при FR_EXIST — следующий номер).
 *
 * Строки копятся в RAM-буфере файла, f_write вызывается целыми кусками по
 * 512 байт (остаток сдвигается memmove). Раз в секунду остаток буфера
 * дописывается и делается f_sync — при выдёргивании карты теряется не больше
 * ~1 с записи.
 *
 * Состояния (g_app.sd_state):
 *   SD_NO_CARD   — раз в 2 с попытка инициализировать и смонтировать карту;
 *   SD_READY     — раз в 2 с прямое чтение сектора 0 с короткими таймаутами
 *                  (карту вынули?);
 *                  тумблер в REC -> старт записи;
 *   SD_RECORDING — запись; тумблер в STOP -> закрыть файлы;
 *   SD_ERROR     — сбой записи: файлы закрыты, карта отмонтирована, ждём,
 *                  пока тумблер вернут в STOP (без авто-перезапуска).
 *
 * Без карты ни одна из периодических операций не стоит дольше ~10 мс
 * (см. fatfs_sd.c): попытка монтирования — 1-3 мс, проверка — до ~5 мс.
 * Карту вынули посреди записи — один таймаут занятости карты (до 500 мс),
 * дальше драйвер отвечает «не готова» сразу.
 *
 * Место на карте (g_app.sd_total_mb, sd_free_mb) — см. space_update():
 * f_getfree только после монтирования и после остановки записи, во время
 * записи — счётчик свободных кластеров, который FatFs ведёт сама.
 */
#include "sd_logger.h"
#include "main.h"
#include "ff.h"
#include "fatfs_sd.h"
#include <string.h>

#define SD_CHUNK       512                           // f_write только целыми секторами
#define SD_BUF_SIZE    (SD_CHUNK + 2 * SD_ROW_MAX)   // буфер файла: кусок + строки сверху
#define SD_SYNC_MS     1000                          // f_sync не чаще раза в секунду
#define SD_CHECK_MS    2000                          // проверка/монтирование карты без записи
#define SD_BAD_RETRY_MS 10000                        // карта отвечает, но не монтируется (нет FAT и т. п.)

// Заголовок CSV. Столбцы через «;», дробная часть — через запятую: Excel с
// русскими настройками открывает файл сразу, без мастера импорта.
//   Date, Time — дата ДД.ММ.ГГГГ и время ЧЧ:ММ:СС часов прибора;
//   RawX, RawY — сырой ответ датчика без какой-либо обработки: регистр угла,
//       переведённый в градусы по формуле датчика (код - 10000) / 100,
//       2 знака — ровно шаг датчика 0,01°;
//   OffsetX, OffsetY — смещение нуля (кнопка «Ноль»; 0, пока ноль не задан);
//   CalcX, CalcY — пересчитанный угол: RawX - OffsetX, RawY - OffsetY — ровно
//       разность записанных в строке чисел;
//   BatV — напряжение аккумулятора, В;
//   Ms — миллисекунды от начала записи (момент ответа датчика): в одной секунде
//       столбца Time бывает до 50 строк.
// Медиана и EMA (сглаживание) — только для экрана и расчёта качки, в файл
// не попадают. Ноль снимается со сглаженного угла в момент нажатия «Ноль».
static const char s_header[] =
		"Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms\n";

typedef struct {
	FIL fil;
	uint16_t len;            // байт в buf
	bool open;
	char buf[SD_BUF_SIZE];
} sd_file_t;

static FATFS s_fs;
static sd_file_t s_file[APP_SENSOR_COUNT];
static app_time_t s_rec_date;                    // дата начала текущего замера (для имён)
static bool s_any_file;                          // в текущем замере создан хоть один файл
static uint32_t s_sync_ms;
static uint32_t s_check_ms;
static uint32_t s_mount_period = SD_CHECK_MS;    // период попыток монтирования

/* ------------------------------------------------------------------------ */
/* Форматирование (чистые функции)                                          */
/* ------------------------------------------------------------------------ */

// Два десятичных разряда с ведущим нулём ("%02u")
static char* put_u2(char *p, uint8_t v) {
	*p++ = (char) ('0' + (v / 10) % 10);
	*p++ = (char) ('0' + v % 10);
	return p;
}

// Беззнаковое целое ("%lu")
static char* put_u32(char *p, uint32_t v) {
	char tmp[10];
	uint8_t n = 0;
	do {
		tmp[n++] = (char) ('0' + v % 10);
		v /= 10;
	} while (v);
	while (n) {
		*p++ = tmp[--n];
	}
	return p;
}

static const uint32_t s_pow10[] = { 1u, 10u, 100u, 1000u };

// v * 10^dec (dec 0..3), округлённое к ближайшему целому — то же, что
// printf("%.*f"): произведение точное (24 бита мантиссы float * 10^dec),
// на точной половине — к чётному. Без printf: быстро, без кучи и одинаково
// на МК и в host-тестах.
static int32_t to_scaled(float v, uint8_t dec) {
	double a = (v < 0.0f) ? -(double) v : (double) v;
	if (!(a < 1.0e6)) {
		a = 999999.0; // NaN или переполнение — в норме не бывает
	}
	double prod = a * (double) s_pow10[dec];
	uint32_t q = (uint32_t) prod;
	double frac = prod - (double) q;
	if (frac > 0.5 || (frac == 0.5 && (q & 1u))) {
		q++;
	}
	return (v < 0.0f) ? -(int32_t) q : (int32_t) q;
}

// Целое v / 10^dec с dec знаками после десятичной ЗАПЯТОЙ: (-46, 2) -> "-0,46"
static char* put_scaled(char *p, int32_t v, uint8_t dec) {
	uint32_t scale = s_pow10[dec];
	uint32_t a = (v < 0) ? 0u - (uint32_t) v : (uint32_t) v;
	if (v < 0) {
		*p++ = '-';
	}
	p = put_u32(p, a / scale);
	if (dec) {
		uint32_t f = a % scale;
		*p++ = ',';
		for (uint32_t d = scale / 10; d; d /= 10) {
			*p++ = (char) ('0' + (f / d) % 10);
		}
	}
	return p;
}

// Строка CSV одного отсчёта датчика (столбцы — см. s_header):
// "01.06.2026;09:05:07;-0,46;1,25;0,125;0,000;-0,585;1,250;12,4;1234".
// Сырой угол — в сотых градуса (ровно регистр датчика), смещение нуля — в
// тысячных; пересчитанный считается в целых от уже округлённых чисел, поэтому
// в файле CalcX = RawX - OffsetX точно.
uint16_t sd_format_row(char *out, const app_time_t *t, const app_sensor_t *s,
		float bat_v, uint32_t ms) {
	int32_t raw_x = to_scaled(s->raw_x, 2), raw_y = to_scaled(s->raw_y, 2);
	int32_t off_x = to_scaled(s->off_x, 3), off_y = to_scaled(s->off_y, 3);
	char *p = out;
	p = put_u2(p, t->date);
	*p++ = '.';
	p = put_u2(p, t->month);
	*p++ = '.';
	*p++ = '2';
	*p++ = '0';
	p = put_u2(p, t->year);
	*p++ = ';';
	p = put_u2(p, t->hours);
	*p++ = ':';
	p = put_u2(p, t->minutes);
	*p++ = ':';
	p = put_u2(p, t->seconds);
	*p++ = ';';
	// Порядок столбцов: RawX;RawY;OffsetX;OffsetY;CalcX;CalcY
	p = put_scaled(p, raw_x, 2);
	*p++ = ';';
	p = put_scaled(p, raw_y, 2);
	*p++ = ';';
	p = put_scaled(p, off_x, 3);
	*p++ = ';';
	p = put_scaled(p, off_y, 3);
	*p++ = ';';
	p = put_scaled(p, raw_x * 10 - off_x, 3);
	*p++ = ';';
	p = put_scaled(p, raw_y * 10 - off_y, 3);
	*p++ = ';';
	p = put_scaled(p, to_scaled(bat_v, 1), 1);
	*p++ = ';';
	p = put_u32(p, ms);
	*p++ = '\n';
	*p = '\0';
	return (uint16_t) (p - out);
}

// Имя файла "YYYY-MM-DD_MNNN_Dk.CSV"
void sd_make_name(char *out, const app_time_t *date, uint16_t num, uint8_t addr) {
	char *p = out;
	*p++ = '2';
	*p++ = '0';
	p = put_u2(p, date->year);
	*p++ = '-';
	p = put_u2(p, date->month);
	*p++ = '-';
	p = put_u2(p, date->date);
	*p++ = '_';
	*p++ = 'M';
	if (num < 100) { // не меньше трёх цифр: M007
		*p++ = '0';
	}
	if (num < 10) {
		*p++ = '0';
	}
	p = put_u32(p, num);
	*p++ = '_';
	*p++ = 'D';
	p = put_u32(p, addr);
	memcpy(p, ".CSV", 5);
}

static bool is_digit(char c) {
	return c >= '0' && c <= '9';
}

static char to_upper(char c) {
	return (c >= 'a' && c <= 'z') ? (char) (c - 'a' + 'A') : c;
}

// Число из min..max цифр, за которым не цифра. NULL — не подходит.
static const char* parse_digits(const char *p, uint8_t min, uint8_t max,
		uint16_t *v) {
	uint16_t x = 0;
	uint8_t n = 0;
	while (n < max && is_digit(*p)) {
		x = (uint16_t) (x * 10 + (*p++ - '0'));
		n++;
	}
	if (n < min || is_digit(*p)) {
		return NULL;
	}
	*v = x;
	return p;
}

// Ровно ".CSV" (регистр не важен) и конец строки
static bool is_csv_ext(const char *p) {
	return p[0] == '.' && to_upper(p[1]) == 'C' && to_upper(p[2]) == 'S'
			&& to_upper(p[3]) == 'V' && p[4] == '\0';
}

// Разобрать "YYYY-MM-DD_MNNN_Dk.CSV" или старое "M_NNN_k.CSV" (регистр не важен)
bool sd_parse_name(const char *name, uint16_t *num, uint8_t *addr) {
	uint16_t n = 0, k = 0, d;
	const char *p = name;
	if (to_upper(p[0]) == 'M' && p[1] == '_') {
		// M_NNN_k.CSV: номер ровно из трёх цифр, датчик — одна цифра
		p = parse_digits(p + 2, 3, 3, &n);
		if (!p || *p != '_') {
			return false;
		}
		p = parse_digits(p + 1, 1, 1, &k);
	} else {
		// YYYY-MM-DD_MNNN_Dk.CSV: дата (проверяется только вид), номер 3-4
		// цифры, адрес 1-3 цифры
		p = parse_digits(p, 4, 4, &d);
		if (!p || *p != '-') {
			return false;
		}
		p = parse_digits(p + 1, 2, 2, &d);
		if (!p || *p != '-') {
			return false;
		}
		p = parse_digits(p + 1, 2, 2, &d);
		if (!p || p[0] != '_' || to_upper(p[1]) != 'M') {
			return false;
		}
		p = parse_digits(p + 2, 3, 4, &n);
		if (!p || p[0] != '_' || to_upper(p[1]) != 'D') {
			return false;
		}
		p = parse_digits(p + 2, 1, 3, &k);
	}
	if (!p || !is_csv_ext(p) || k > 255) {
		return false;
	}
	*num = n;
	*addr = (uint8_t) k;
	return true;
}

/* ------------------------------------------------------------------------ */
/* Номера замеров                                                           */
/* ------------------------------------------------------------------------ */

uint16_t sd_number_after(uint16_t num) {
	return (num < SD_NUM_MAX) ? (uint16_t) (num + 1) : 0;
}

// Один проход f_readdir по корню: наибольший номер замера (имена обоих видов,
// любой датчик) -> g_app.file_number = он + 1
static FRESULT scan_numbers(void) {
	static DIR dir;       // static: не тратить стек суперцикла
	static FILINFO fno;   // с длинным именем (_USE_LFN) — ~270 байт
	uint16_t highest = 0;
	FRESULT res = f_opendir(&dir, "/");
	if (res != FR_OK) {
		return res;
	}
	for (;;) {
		res = f_readdir(&dir, &fno);
		if (res != FR_OK || fno.fname[0] == '\0') {
			break;
		}
		uint16_t num;
		uint8_t addr;
		if (!(fno.fattrib & AM_DIR) && sd_parse_name(fno.fname, &num, &addr)
				&& num > highest) {
			highest = num;
		}
	}
	f_closedir(&dir);
	if (res == FR_OK) {
		g_app.file_number = sd_number_after(highest);
	}
	return res;
}

/* ------------------------------------------------------------------------ */
/* Место на карте                                                           */
/* ------------------------------------------------------------------------ */

// Кластеров -> МБ (сектор всегда 512 байт: _MIN_SS = _MAX_SS)
static uint32_t clusters_mb(DWORD clusters) {
	return (uint32_t) (((uint64_t) clusters * s_fs.csize * _MAX_SS) >> 20);
}

// Свободные кластеры известны FatFs без чтения карты: из FSInfo (FAT32,
// _FS_NOFSINFO = 0 — счётчику FSInfo верим) или после прошлого f_getfree;
// при записи FatFs уменьшает счётчик сама
static bool free_known(void) {
	return s_fs.free_clst <= s_fs.n_fatent - 2u;
}

// Во время записи: только счётчик FatFs, карту не трогаем
static void space_from_cache(void) {
	if (free_known()) {
		g_app.sd_free_mb = clusters_mb(s_fs.free_clst);
	}
}

// После монтирования и после записи. Если счётчик неизвестен, f_getfree
// обходит всю FAT: на FAT12/16 это до 256 секторов (~0.1 с, раз при
// монтировании), а на большой FAT32 без FSInfo — десятки тысяч секторов
// (секунды, суперцикл стоит) — такой обход не делаем, место «неизвестно»
static void space_update(void) {
	g_app.sd_total_mb = clusters_mb(s_fs.n_fatent - 2u);
	g_app.sd_free_mb = APP_SD_FREE_UNKNOWN;
	if (s_fs.fs_type == FS_FAT32 && !free_known()) {
		return;
	}
	DWORD nfree;
	FATFS *fs;
	if (f_getfree("", &nfree, &fs) == FR_OK) {
		g_app.sd_free_mb = clusters_mb(nfree);
	}
}

static void space_clear(void) {
	g_app.sd_total_mb = 0;
	g_app.sd_free_mb = APP_SD_FREE_UNKNOWN;
}

/* ------------------------------------------------------------------------ */
/* Карта                                                                    */
/* ------------------------------------------------------------------------ */

// Инициализировать карту и смонтировать FatFs.
// SD_disk_initialize вызывается напрямую: обёртка ST (diskio.c) вызывает
// инициализацию драйвера только один раз за всё время работы, а после замены
// карты её нужно повторить.
static void sd_mount(void) {
	f_mount(NULL, "", 0);
	FRESULT res = FR_NOT_READY;
	s_mount_period = SD_CHECK_MS;
	if (!(SD_disk_initialize(0) & STA_NOINIT)) {
		res = f_mount(&s_fs, "", 1);
		if (res == FR_OK) {
			res = scan_numbers();
		}
		if (res != FR_OK) {
			// Карта есть, но не годится: инициализация карты занимает до сотен
			// миллисекунд — не повторять её каждые 2 с
			s_mount_period = SD_BAD_RETRY_MS;
		}
	}
	if (res != FR_OK) {
		f_mount(NULL, "", 0);
		g_app.sd_state = SD_NO_CARD;
		g_app.sd_err = (uint8_t) res;
		g_app.file_number = 0;
		space_clear();
		return;
	}
	g_app.sd_state = SD_READY;
	g_app.sd_err = FR_OK;
	space_update();
}

// Карта на месте? Чтение сектора 0 в обход FatFs (её кэш не заметит замену),
// с короткими таймаутами: без карты — не дольше ~5 мс при любом MISO.
// Буфер — файловый буфер первого датчика: вне записи он свободен.
static void sd_check_card(void) {
	if (SD_disk_check(0, (BYTE*) s_file[0].buf) != RES_OK) {
		f_mount(NULL, "", 0);
		g_app.sd_state = SD_NO_CARD;
		g_app.sd_err = FR_NOT_READY;
		g_app.file_number = 0;
		space_clear();
	}
}

/* ------------------------------------------------------------------------ */
/* Файлы замера                                                             */
/* ------------------------------------------------------------------------ */

// Записать накопленное: целые куски по 512 байт или (all) всё
static FRESULT file_flush(sd_file_t *f, bool all) {
	uint16_t n = all ? f->len : (uint16_t) ((f->len / SD_CHUNK) * SD_CHUNK);
	if (n == 0) {
		return FR_OK;
	}
	UINT bw = 0;
	FRESULT res = f_write(&f->fil, f->buf, n, &bw);
	if (res == FR_OK && bw != n) {
		res = FR_DENIED; // записалось меньше — карта заполнена
	}
	if (res != FR_OK) {
		return res;
	}
	f->len = (uint16_t) (f->len - n);
	memmove(f->buf, &f->buf[n], f->len);
	return FR_OK;
}

// Закрыть все файлы без проверки ошибок (после сбоя)
static void close_all_quiet(void) {
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		if (s_file[i].open) {
			f_close(&s_file[i].fil);
			s_file[i].open = false;
		}
		s_file[i].len = 0;
	}
	g_app.rec_sensor_mask = 0;
}

// Сбой во время записи: закрыть что можно, отмонтировать, ждать STOP
static void rec_fail(FRESULT res) {
	close_all_quiet();
	f_mount(NULL, "", 0); // заодно освобождает блокировки FatFs (_FS_LOCK)
	g_app.sd_state = SD_ERROR;
	g_app.sd_err = (uint8_t) res;
	space_clear();
}

// Создать файл датчика i в текущем замере. false — сбой (уже обработан).
static bool file_open(uint8_t i) {
	sd_file_t *f = &s_file[i];
	char name[SD_NAME_LEN];
	FRESULT res;
	for (;;) {
		if (g_app.file_number == 0) {
			rec_fail(FR_DENIED); // номера кончились (SD_NUM_MAX)
			return false;
		}
		sd_make_name(name, &s_rec_date, g_app.file_number, APP_SENSOR_ADDR(i));
		// FA_CREATE_NEW: никогда не перезаписывать существующий замер
		res = f_open(&f->fil, name, FA_CREATE_NEW | FA_WRITE);
		if (res == FR_EXIST && !s_any_file) {
			// Номер занят (не должно случаться после scan_numbers) — взять следующий
			g_app.file_number = sd_number_after(g_app.file_number);
			continue;
		}
		break;
	}
	if (res != FR_OK) {
		rec_fail(res);
		return false;
	}
	f->open = true;
	memcpy(f->buf, s_header, sizeof(s_header) - 1);
	f->len = sizeof(s_header) - 1;
	s_any_file = true;
	g_app.rec_sensor_mask |= (uint8_t) (1u << i);
	return true;
}

// Тумблер в REC: файлы для всех отвечающих датчиков
static void rec_start(uint32_t now) {
	g_app.rec_start_ms = now;
	s_rec_date = g_app.time; // все файлы замера — с датой начала (и после полуночи)
	g_app.rec_rows = 0;
	g_app.rec_sensor_mask = 0;
	g_app.sd_state = SD_RECORDING;
	s_any_file = false;
	s_sync_ms = now;
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		s_file[i].open = false;
		s_file[i].len = 0;
	}
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		if (g_app.sensor[i].status == SENSOR_OK && !file_open(i)) {
			return;
		}
	}
}

// Тумблер в STOP: дописать буферы, закрыть файлы, следующий номер
static void rec_stop(void) {
	FRESULT err = FR_OK;
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		sd_file_t *f = &s_file[i];
		if (!f->open) {
			continue;
		}
		FRESULT res = file_flush(f, true);
		FRESULT res_close = f_close(&f->fil);
		if (res == FR_OK) {
			res = res_close;
		}
		if (res != FR_OK && err == FR_OK) {
			err = res;
		}
		f->open = false;
		f->len = 0;
	}
	g_app.rec_sensor_mask = 0;
	if (err != FR_OK) {
		// Скорее всего карту вынули: дальше — повторное монтирование
		f_mount(NULL, "", 0);
		g_app.sd_state = SD_NO_CARD;
		g_app.sd_err = (uint8_t) err;
		g_app.file_number = 0;
		space_clear();
		return;
	}
	if (s_any_file) {
		g_app.file_number = sd_number_after(g_app.file_number);
	}
	g_app.sd_state = SD_READY;
	space_update(); // счётчик FatFs уже известен — без чтения карты
}

// Раз в секунду: дописать остатки буферов и f_sync
static void rec_sync(void) {
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		sd_file_t *f = &s_file[i];
		if (!f->open) {
			continue;
		}
		FRESULT res = file_flush(f, true);
		if (res == FR_OK) {
			res = f_sync(&f->fil);
		}
		if (res != FR_OK) {
			rec_fail(res);
			return;
		}
	}
	space_from_cache();
}

/* ------------------------------------------------------------------------ */
/* Интерфейс модуля                                                         */
/* ------------------------------------------------------------------------ */

void sd_logger_init(void) {
	g_app.sd_state = SD_NO_CARD;
	g_app.file_number = 0;
	space_clear();
	sd_mount();
	s_check_ms = HAL_GetTick();
}

void sd_logger_task(void) {
	uint32_t now = HAL_GetTick();
	switch (g_app.sd_state) {
	case SD_RECORDING:
		if (!g_app.rec_switch_on) {
			rec_stop();
		} else if (now - s_sync_ms >= SD_SYNC_MS) {
			s_sync_ms = now;
			rec_sync();
		}
		break;

	case SD_ERROR:
		// Без авто-перезапуска: только когда тумблер вернули в STOP
		if (!g_app.rec_switch_on) {
			s_check_ms = now;
			sd_mount();
		}
		break;

	case SD_READY:
		if (g_app.rec_switch_on) {
			rec_start(now);
		} else if (now - s_check_ms >= SD_CHECK_MS) {
			s_check_ms = now;
			sd_check_card();
		}
		break;

	case SD_NO_CARD:
	default:
		if (now - s_check_ms >= s_mount_period) {
			s_check_ms = now;
			sd_mount();
		}
		break;
	}
}

bool sd_logger_file_name(uint8_t i, char out[SD_NAME_LEN]) {
	if (i >= APP_SENSOR_COUNT || g_app.file_number == 0) {
		return false;
	}
	const app_time_t *date = (g_app.sd_state == SD_RECORDING) ? &s_rec_date : &g_app.time;
	sd_make_name(out, date, g_app.file_number, APP_SENSOR_ADDR(i));
	return true;
}

void sd_logger_write_cycle(const bool fresh[APP_SENSOR_COUNT]) {
	if (g_app.sd_state != SD_RECORDING) {
		return;
	}
	for (uint8_t i = 0; i < APP_SENSOR_COUNT; i++) {
		sd_file_t *f = &s_file[i];
		const app_sensor_t *s = &g_app.sensor[i];
		if (!f->open) {
			// Датчик появился во время записи — завести его файл
			if (s->status != SENSOR_OK || !file_open(i)) {
				if (g_app.sd_state != SD_RECORDING) {
					return; // file_open не удался, запись остановлена
				}
				continue;
			}
		}
		if (!fresh[i]) {
			continue;
		}
		// Отсчёт мог прийти из цикла, начатого чуть раньше старта записи
		int32_t ms = (int32_t) (s->last_ok_ms - g_app.rec_start_ms);
		f->len = (uint16_t) (f->len
				+ sd_format_row(&f->buf[f->len], &g_app.time, s,
						g_app.battery_v, ms > 0 ? (uint32_t) ms : 0u));
		g_app.rec_rows++;
		if (f->len >= SD_CHUNK) {
			FRESULT res = file_flush(f, false);
			if (res != FR_OK) {
				rec_fail(res);
				return;
			}
		}
	}
}
