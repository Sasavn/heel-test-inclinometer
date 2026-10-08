/*
 * Host-тесты логики прибора: чистые функции (CRC, разбор Modbus, углы,
 * фильтры, календарь, строка CSV, имена файлов, журнал настроек), драйвер
 * SD-карты поверх имитации SPI (время по байтам) и сценарии целиком —
 * настоящие app.c, bwm427.c, sd_logger.c и settings.c поверх имитации шины,
 * часов, SD-карты и флеша.
 *
 * Сборка и запуск: tests/host/run.sh
 */
#include "fake.h"
#include "app.h"
#include "bwm427.h"
#include "sd_logger.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_checks, s_fails;

#define CHECK(c) do { s_checks++; if (!(c)) { s_fails++; \
	printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_NEAR(a, b, eps) do { double _a = (a), _b = (b); s_checks++; \
	if (fabs(_a - _b) > (eps)) { s_fails++; \
	printf("  FAIL %s:%d: %s = %g, ожидалось %g\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

/* ------------------------------------------------------------------------ */
/* Чистые функции                                                           */
/* ------------------------------------------------------------------------ */

static void test_crc(void) {
	bwm427_xfer_t x;
	uint8_t req[8];
	bwm427_xfer_read(&x, 1);
	bwm427_build_request(&x, req);
	const uint8_t expect[8] = { 0x01, 0x03, 0x00, 0x01, 0x00, 0x02, 0x95, 0xCB };
	CHECK(memcmp(req, expect, 8) == 0);
	const uint8_t ref[6] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A }; // классический пример: C5 CD
	CHECK(bwm427_crc16(ref, 6) == 0xCDC5);
	CHECK(bwm427_crc16(req, 8) == 0); // CRC кадра вместе с CRC = 0
}

static void test_decode(void) {
	CHECK_NEAR(bwm427_decode_angle(10000), 0.0, 1e-6);
	CHECK_NEAR(bwm427_decode_angle(10123), 1.23, 1e-5);
	CHECK_NEAR(bwm427_decode_angle(9000), -10.0, 1e-5);
	CHECK_NEAR(bwm427_decode_angle(19000), 90.0, 1e-5);
	CHECK_NEAR(bwm427_decode_angle(1000), -90.0, 1e-5);
	uint16_t regs[2] = { 10050, 9950 };
	float x, y;
	bwm427_decode_xy(regs, &x, &y);
	CHECK_NEAR(x, 0.5, 1e-5);
	CHECK_NEAR(y, -0.5, 1e-5);
}

static void test_filters(void) {
	const float p[6][3] = { { 1, 2, 3 }, { 1, 3, 2 }, { 2, 1, 3 }, { 2, 3, 1 }, { 3, 1, 2 }, { 3, 2, 1 } };
	for (int i = 0; i < 6; i++) {
		CHECK(bwm427_median3(p[i][0], p[i][1], p[i][2]) == 2.0f);
	}
	CHECK(bwm427_median3(5, 5, 1) == 5.0f);
	CHECK(bwm427_median3(-1, 7, 7) == 7.0f);
	CHECK_NEAR(bwm427_ema(0.0f, 10.0f, 0.15f), 1.5, 1e-6);

	bwm427_filter_t f;
	float ox, oy;
	bwm427_filter_reset(&f);
	bwm427_filter_step(&f, 1.0f, 2.0f, 0.15f, &ox, &oy);
	CHECK(ox == 1.0f && oy == 2.0f); // первый отсчёт проходит как есть
	bwm427_filter_step(&f, 1.0f, 2.0f, 0.15f, &ox, &oy);
	bwm427_filter_step(&f, 10.0f, 2.0f, 0.15f, &ox, &oy); // одиночный выброс
	CHECK(ox == 1.0f);
	bwm427_filter_step(&f, 1.0f, 2.0f, 0.15f, &ox, &oy);
	CHECK(ox == 1.0f);
	bwm427_filter_step(&f, 1.0f, 2.0f, 0.15f, &ox, &oy); // выброс ушёл из окна
	// ступенька: медиана пропускает со второго отсчёта, дальше EMA
	bwm427_filter_step(&f, 3.0f, 2.0f, 0.5f, &ox, &oy);
	CHECK(ox == 1.0f);
	bwm427_filter_step(&f, 3.0f, 2.0f, 0.5f, &ox, &oy);
	CHECK_NEAR(ox, 2.0, 1e-6);
	bwm427_filter_reset(&f);
	bwm427_filter_step(&f, -4.0f, 0.0f, 0.15f, &ox, &oy);
	CHECK(ox == -4.0f);
}

static void frame(uint8_t *b, uint16_t n) { // дописать CRC в b[n], b[n+1]
	uint16_t c = bwm427_crc16(b, n);
	b[n] = (uint8_t) c;
	b[n + 1] = (uint8_t) (c >> 8);
}

static void test_parse(void) {
	bwm427_xfer_t x;
	bwm427_xfer_read(&x, 2);
	uint8_t ok[9] = { 0x02, 0x03, 0x04, 0x27, 0x10, 0x26, 0xAC };
	frame(ok, 7);
	CHECK(bwm427_parse_reply(&x, ok, 9) == BWM427_OK);
	CHECK(x.regs[0] == 10000 && x.regs[1] == 9900);

	uint8_t b[16];
	memcpy(b, ok, 9);
	b[0] = 3;
	frame(b, 7);
	CHECK(bwm427_parse_reply(&x, b, 9) == BWM427_BAD_FRAME); // чужой адрес
	memcpy(b, ok, 9);
	b[8] ^= 1;
	CHECK(bwm427_parse_reply(&x, b, 9) == BWM427_CRC);
	memcpy(b, ok, 9);
	b[2] = 2;
	frame(b, 5);
	CHECK(bwm427_parse_reply(&x, b, 7) == BWM427_BAD_FRAME); // не тот счётчик
	CHECK(bwm427_parse_reply(&x, ok, 4) == BWM427_BAD_FRAME); // обрывок
	uint8_t exc[5] = { 0x02, 0x83, 0x02 };
	frame(exc, 3);
	CHECK(bwm427_parse_reply(&x, exc, 5) == BWM427_EXCEPTION && x.exc_code == 2);

	bwm427_xfer_write(&x, 1, BWM427_REG_ADDR, 3);
	uint8_t req[8];
	bwm427_build_request(&x, req);
	CHECK(bwm427_parse_reply(&x, req, 8) == BWM427_OK); // эхо
	memcpy(b, req, 8);
	b[5] = 4;
	frame(b, 6);
	CHECK(bwm427_parse_reply(&x, b, 8) == BWM427_BAD_FRAME); // эхо с другим значением
	memcpy(b, req, 8);
	b[0] = 3;
	frame(b, 6);
	CHECK(bwm427_parse_reply(&x, b, 8) == BWM427_BAD_FRAME); // эхо с нового адреса...
	x.alt_addr = 3;
	CHECK(bwm427_parse_reply(&x, b, 8) == BWM427_OK);        // ...если разрешено

	bwm427_xfer_read(&x, 2);
	CHECK(bwm427_frame_need(&x, ok, 0) == 0);
	CHECK(bwm427_frame_need(&x, ok, 1) == 0);
	CHECK(bwm427_frame_need(&x, ok, 2) == 0);
	CHECK(bwm427_frame_need(&x, ok, 3) == 9);
	CHECK(bwm427_frame_need(&x, exc, 2) == 5);
	const uint8_t wrong_fn[3] = { 0x02, 0x10, 0x00 };
	CHECK(bwm427_frame_need(&x, wrong_fn, 3) == -1);
	const uint8_t wrong_cnt[3] = { 0x02, 0x03, 0x02 };
	CHECK(bwm427_frame_need(&x, wrong_cnt, 3) == -1);
	CHECK(bwm427_reply_len(&x) == 9);
}

static bool time_eq(const app_time_t *t, int d, int mo, int y, int h, int mi, int s) {
	return t->date == d && t->month == mo && t->year == y && t->hours == h
			&& t->minutes == mi && t->seconds == s;
}

static void test_calendar(void) {
	CHECK(app_days_in_month(1, 26) == 31);
	CHECK(app_days_in_month(2, 26) == 28);
	CHECK(app_days_in_month(2, 24) == 29);
	CHECK(app_days_in_month(2, 0) == 29); // 2000 — високосный
	CHECK(app_days_in_month(4, 26) == 30);
	CHECK(app_days_in_month(12, 26) == 31);

	app_time_t t = { .year = 25, .month = 12, .date = 31, .hours = 23, .minutes = 59, .seconds = 59 };
	app_time_add_second(&t);
	CHECK(time_eq(&t, 1, 1, 26, 0, 0, 0));
	t = (app_time_t ) { .year = 25, .month = 2, .date = 28, .hours = 23, .minutes = 59, .seconds = 59 };
	app_time_add_second(&t);
	CHECK(time_eq(&t, 1, 3, 25, 0, 0, 0));
	t = (app_time_t ) { .year = 24, .month = 2, .date = 28, .hours = 23, .minutes = 59, .seconds = 59 };
	app_time_add_second(&t);
	CHECK(time_eq(&t, 29, 2, 24, 0, 0, 0));
	app_time_add_second(&t);
	CHECK(time_eq(&t, 29, 2, 24, 0, 0, 1));
	t = (app_time_t ) { .year = 24, .month = 2, .date = 29, .hours = 23, .minutes = 59, .seconds = 59 };
	app_time_add_second(&t);
	CHECK(time_eq(&t, 1, 3, 24, 0, 0, 0));
	t = (app_time_t ) { .year = 26, .month = 4, .date = 30, .hours = 23, .minutes = 59, .seconds = 59 };
	app_time_add_second(&t);
	CHECK(time_eq(&t, 1, 5, 26, 0, 0, 0));
	t = (app_time_t ) { .year = 26, .month = 1, .date = 31, .hours = 12, .minutes = 59, .seconds = 59 };
	app_time_add_second(&t);
	CHECK(time_eq(&t, 31, 1, 26, 13, 0, 0));
	t = (app_time_t ) { .year = 99, .month = 12, .date = 31, .hours = 23, .minutes = 59, .seconds = 59 };
	app_time_add_second(&t);
	CHECK(time_eq(&t, 1, 1, 0, 0, 0, 0));

	// Год целиком: 365 / 366 суток
	t = (app_time_t ) { .year = 26, .month = 1, .date = 1 };
	for (long i = 0; i < 365L * 86400; i++) {
		app_time_add_second(&t);
	}
	CHECK(time_eq(&t, 1, 1, 27, 0, 0, 0));
	t = (app_time_t ) { .year = 28, .month = 1, .date = 1 };
	for (long i = 0; i < 365L * 86400; i++) {
		app_time_add_second(&t);
	}
	CHECK(time_eq(&t, 31, 12, 28, 0, 0, 0));

	CHECK(app_time_from_build("Oct  6 2026", "15:21:07", &t));
	CHECK(time_eq(&t, 6, 10, 26, 15, 21, 7));
	CHECK(app_time_from_build("Feb 29 2024", "00:00:00", &t));
	CHECK(time_eq(&t, 29, 2, 24, 0, 0, 0));
	CHECK(!app_time_from_build("Feb 29 2025", "00:00:00", &t));
	CHECK(!app_time_from_build("Foo  1 2026", "00:00:00", &t));
	CHECK(!app_time_from_build("Jan  1 2026", "25:00:00", &t));
	CHECK(app_time_from_build(__DATE__, __TIME__, &t));
}

static void test_csv(void) {
	app_time_t t = { .year = 26, .month = 6, .date = 1, .hours = 9, .minutes = 5, .seconds = 7 };
	app_sensor_t s;
	memset(&s, 0, sizeof(s));
	char row[SD_ROW_MAX], ref[256];
	srand(12345);
	const float special[] = { 0.0f, -0.0001f, 0.0005f, 0.0015f, 1.0625f, -1.0625f, 2.5f,
			179.9995f, -179.9995f, 99.99951f, 0.12345f, -12.3456f, 1e-7f, 359.999f };
	int nspecial = (int) (sizeof(special) / sizeof(special[0]));
	int mismatches = 0;
	for (int i = 0; i < 200000; i++) {
		float v[7];
		for (int k = 0; k < 7; k++) {
			if (i < nspecial) {
				v[k] = special[(i + k) % nspecial];
			} else {
				v[k] = ((float) rand() / RAND_MAX - 0.5f) * 400.0f;
				if (k == 6) {
					v[k] = (float) rand() / RAND_MAX * 15.0f;
				}
			}
		}
		// Сырой угол — всегда из регистра датчика (любой 16-битный код)
		uint16_t regs[2] = { (uint16_t) rand(), (uint16_t) (rand() * 7u) };
		if (i < 4) {
			regs[0] = (uint16_t[]) { 10000, 9954, 0, 65535 }[i];
			regs[1] = (uint16_t[]) { 10001, 9999, 42768, 32767 }[i];
		}
		bwm427_decode_xy(regs, &s.raw_x, &s.raw_y);
		s.filt_x = v[0]; // фильтр и экранный угол в файл не идут
		s.filt_y = v[1];
		s.x = v[4];
		s.y = v[5];
		s.off_x = v[2];
		s.off_y = v[3];
		uint32_t ms = (uint32_t) rand() * 7919u;
		uint16_t n = sd_format_row(row, &t, &s, v[6], ms);
		// Эталон: printf, затем десятичная запятая; пересчитанный — из записанных чисел
		int raw_c[2] = { (int16_t) regs[0] - 10000, (int16_t) regs[1] - 10000 };
		char off_s[2][24], calc_s[2][24], raw_s[2][24], bat_s[16];
		for (int a = 0; a < 2; a++) {
			snprintf(raw_s[a], sizeof(raw_s[a]), "%s%d.%02d", raw_c[a] < 0 ? "-" : "",
					abs(raw_c[a]) / 100, abs(raw_c[a]) % 100);
			snprintf(off_s[a], sizeof(off_s[a]), "%.3f", a ? v[3] : v[2]);
			if (strcmp(off_s[a], "-0.000") == 0) {
				strcpy(off_s[a], "0.000"); // прибор не пишет «минус ноль»
			}
			long off_m = lround(strtod(off_s[a], NULL) * 1000.0);
			long calc_m = raw_c[a] * 10L - off_m;
			snprintf(calc_s[a], sizeof(calc_s[a]), "%s%ld.%03ld", calc_m < 0 ? "-" : "",
					labs(calc_m) / 1000, labs(calc_m) % 1000);
		}
		snprintf(bat_s, sizeof(bat_s), "%.1f", v[6]);
		if (strcmp(bat_s, "-0.0") == 0) {
			strcpy(bat_s, "0.0");
		}
		snprintf(ref, sizeof(ref), "%02d.%02d.20%02d;%02d:%02d:%02d;%s;%s;%s;%s;%s;%s;%s;%lu\n",
				t.date, t.month, t.year, t.hours, t.minutes, t.seconds,
				raw_s[0], raw_s[1], off_s[0], off_s[1], calc_s[0], calc_s[1], bat_s,
				(unsigned long) ms);
		for (char *c = ref; *c; c++) {
			if (*c == '.' && c > ref + 10) { // точки даты оставить
				*c = ',';
			}
		}
		if (strcmp(row, ref) != 0 || n != strlen(ref) || n >= SD_ROW_MAX) {
			if (mismatches++ < 5) {
				printf("  CSV: got  %s        want %s", row, ref);
			}
		}
	}
	CHECK(mismatches == 0);
	t = (app_time_t ) { .year = 5, .month = 12, .date = 31, .hours = 23, .minutes = 59, .seconds = 0 };
	s.raw_x = 1.5f;
	s.raw_y = -0.46f;
	s.filt_x = 1.4f;
	s.filt_y = -0.25f;
	s.off_x = 0.5f;
	s.off_y = -0.0004f;
	s.x = 0.9f;
	s.y = -0.25f;
	sd_format_row(row, &t, &s, 7.36f, 0);
	CHECK(strcmp(row, "31.12.2005;23:59:00;1,50;-0,46;0,500;0,000;1,000;-0,460;7,4;0\n") == 0);
	s.raw_x = -0.05f;
	s.off_x = 0.0125f; // смещение пишется с округлением, разность — от записанного
	s.raw_y = 327.67f;
	s.off_y = -327.68f;
	sd_format_row(row, &t, &s, 12.0f, 4294967295u);
	CHECK(strcmp(row, "31.12.2005;23:59:00;-0,05;327,67;0,013;-327,680;-0,063;655,350;12,0;4294967295\n")
			== 0);

	char name[SD_NAME_LEN];
	uint16_t num;
	uint8_t k;
	const app_time_t d = { .year = 26, .month = 10, .date = 6, .hours = 23, .minutes = 59, .seconds = 59 };
	sd_make_name(name, &d, 7, 2);
	CHECK(strcmp(name, "2026-10-06_M007_D2.CSV") == 0);
	const app_time_t d2 = { .year = 5, .month = 1, .date = 9 };
	sd_make_name(name, &d2, 999, 3);
	CHECK(strcmp(name, "2005-01-09_M999_D3.CSV") == 0);
	sd_make_name(name, &d, 1234, 247); // самое длинное имя
	CHECK(strcmp(name, "2026-10-06_M1234_D247.CSV") == 0 && strlen(name) < SD_NAME_LEN);
	// Новые имена
	CHECK(sd_parse_name("2026-10-06_M007_D2.CSV", &num, &k) && num == 7 && k == 2);
	CHECK(sd_parse_name("2026-10-06_m123_d3.csv", &num, &k) && num == 123 && k == 3);
	CHECK(sd_parse_name("2026-10-06_M1234_D247.CSV", &num, &k) && num == 1234 && k == 247);
	CHECK(!sd_parse_name("2026-10-06_M07_D2.CSV", &num, &k));
	CHECK(!sd_parse_name("2026-10-06_M12345_D2.CSV", &num, &k));
	CHECK(!sd_parse_name("2026-10-06_M007_D.CSV", &num, &k));
	CHECK(!sd_parse_name("2026-10-06_M007_D999.CSV", &num, &k));
	CHECK(!sd_parse_name("2026-10-06_M007_D2.TXT", &num, &k));
	CHECK(!sd_parse_name("2026-10-06_M007_D2.CSV.BAK", &num, &k));
	CHECK(!sd_parse_name("2026-1-06_M007_D2.CSV", &num, &k));
	CHECK(!sd_parse_name("2026-10-06-M007_D2.CSV", &num, &k));
	CHECK(!sd_parse_name("2026-10-06_X007_D2.CSV", &num, &k));
	CHECK(!sd_parse_name("", &num, &k));
	// Туда и обратно
	int bad = 0;
	for (uint16_t n = 1; n <= SD_NUM_MAX; n++) {
		uint8_t a = (uint8_t) (1 + n % 247);
		sd_make_name(name, &d, n, a);
		if (!sd_parse_name(name, &num, &k) || num != n || k != a) {
			bad++;
		}
	}
	CHECK(bad == 0);
	// Старые имена (для нумерации)
	CHECK(sd_parse_name("M_123_3.CSV", &num, &k) && num == 123 && k == 3);
	CHECK(sd_parse_name("m_001_1.csv", &num, &k) && num == 1 && k == 1);
	CHECK(!sd_parse_name("M_12_1.CSV", &num, &k));
	CHECK(!sd_parse_name("M_1234_1.CSV", &num, &k));
	CHECK(!sd_parse_name("X_001_1.CSV", &num, &k));
	CHECK(!sd_parse_name("M_001_1.TXT", &num, &k));
	CHECK(!sd_parse_name("M_001_1.CSV.BAK", &num, &k));
	CHECK(sd_number_after(7) == 8 && sd_number_after(SD_NUM_MAX) == 0);
}

/* ------------------------------------------------------------------------ */
/* Драйвер SD: настоящий fatfs_sd.c, имитация SPI и карты                   */
/* ------------------------------------------------------------------------ */

static void fill(BYTE *b, uint8_t seed) {
	for (int i = 0; i < 512; i++) {
		b[i] = (BYTE) (seed + i * 7);
	}
}

static void test_sd_driver(void) {
	BYTE buf[512], ref[512];
	uint64_t t0, dt;
	fake_reset();

	// Карты нет: одна попытка инициализации при любом MISO — считанные мс
	static const struct {
		fake_miso_t m;
		const char *name;
	} nc[] = { { FAKE_MISO_00, "0x00" }, { FAKE_MISO_FF, "0xFF" } };
	for (int i = 0; i < 2; i++) {
		fake_sd_reset(nc[i].m, 1);
		t0 = fake_time_us();
		DSTATUS st = drv_disk_initialize(0);
		dt = fake_time_us() - t0;
		printf("  нет карты, MISO = %s: попытка инициализации %.2f мс (%u байт SPI)\n",
				nc[i].name, dt / 1000.0, (unsigned) fake_spi_bytes);
		CHECK(st & STA_NOINIT);
		CHECK(dt <= 5000);
		t0 = fake_time_us();
		CHECK(drv_disk_read(0, buf, 0, 1) == RES_NOTRDY);
		CHECK(drv_disk_check(0, buf) == RES_NOTRDY);
		CHECK(drv_disk_write(0, buf, 0, 1) == RES_NOTRDY);
		CHECK(fake_time_us() == t0); // без инициализации — ни байта по SPI
	}
	uint64_t worst = 0, sum = 0;
	int inited = 0;
	for (uint32_t seed = 1; seed <= 1000; seed++) {
		fake_sd_reset(FAKE_MISO_NOISE, seed);
		t0 = fake_time_us();
		if (!(drv_disk_initialize(0) & STA_NOINIT)) {
			inited++;
		}
		dt = fake_time_us() - t0;
		sum += dt;
		if (dt > worst) {
			worst = dt;
		}
	}
	printf("  нет карты, шум на MISO: 1000 попыток, в среднем %.2f мс, худшая %.2f мс\n",
			sum / 1000.0 / 1000.0, worst / 1000.0);
	CHECK(inited == 0);
	CHECK(worst <= 15000);

	// Карта SDHC: инициализация, запись и чтение
	fake_sd_reset(FAKE_MISO_CARD, 1);
	fake_sd_acmd41_polls = 100;
	t0 = fake_time_us();
	CHECK(drv_disk_initialize(0) == 0);
	printf("  карта SDHC (100 ответов ACMD41 «idle»): инициализация %.1f мс\n",
			(fake_time_us() - t0) / 1000.0);
	CHECK(!(drv_disk_status(0) & STA_NOINIT));
	fill(ref, 3);
	CHECK(drv_disk_write(0, ref, 3, 1) == RES_OK);
	memset(buf, 0, sizeof(buf));
	CHECK(drv_disk_read(0, buf, 3, 1) == RES_OK && memcmp(buf, ref, 512) == 0);
	CHECK(memcmp(fake_sd_mem[3], ref, 512) == 0);
	CHECK(drv_disk_check(0, buf) == RES_OK);
	CHECK(drv_disk_ioctl(0, CTRL_SYNC, NULL) == RES_OK);
	// Долгая занятость после записи (120 мс) законна: ждём, не сбоим
	fake_sd_busy_us = 120000;
	fill(ref, 4);
	CHECK(drv_disk_write(0, ref, 4, 1) == RES_OK);
	t0 = fake_time_us();
	CHECK(drv_disk_write(0, ref, 5, 1) == RES_OK);
	dt = fake_time_us() - t0;
	CHECK(dt >= 119000 && dt <= 125000);
	CHECK(drv_disk_ioctl(0, CTRL_SYNC, NULL) == RES_OK);
	CHECK(memcmp(fake_sd_mem[5], ref, 512) == 0);

	// Первый CMD0 — ответ не «idle» (карта осталась посреди обмена): повтор
	fake_sd_reset(FAKE_MISO_CARD, 1);
	fake_sd_cmd0_junk = 1;
	CHECK(drv_disk_initialize(0) == 0);
	fake_sd_reset(FAKE_MISO_CARD, 1);
	fake_sd_cmd0_junk = 5;
	CHECK(drv_disk_initialize(0) & STA_NOINIT); // сдаётся, повтор — через 2 с

	// Карту вынули без записи: проверка наличия — быстро, дальше сразу NOTRDY
	for (int i = 0; i < 2; i++) {
		fake_sd_reset(FAKE_MISO_CARD, 1);
		CHECK(drv_disk_initialize(0) == 0);
		fake_miso = nc[i].m;
		t0 = fake_time_us();
		CHECK(drv_disk_check(0, buf) == RES_ERROR);
		dt = fake_time_us() - t0;
		printf("  карту вынули, MISO = %s: проверка наличия %.2f мс\n", nc[i].name, dt / 1000.0);
		CHECK(dt <= 5000);
		CHECK(drv_disk_status(0) & STA_NOINIT);
		t0 = fake_time_us();
		CHECK(drv_disk_read(0, buf, 0, 1) == RES_NOTRDY);
		CHECK(drv_disk_write(0, buf, 0, 1) == RES_NOTRDY);
		CHECK(drv_disk_ioctl(0, CTRL_SYNC, NULL) == RES_NOTRDY);
		CHECK(fake_time_us() == t0);
	}

	// Вынули посреди записи: MISO = 0x00 неотличим от занятой карты — один
	// таймаут занятости (500 мс), дальше сразу NOTRDY; MISO = 0xFF — сразу
	for (int i = 0; i < 2; i++) {
		fake_sd_reset(FAKE_MISO_CARD, 1);
		CHECK(drv_disk_initialize(0) == 0);
		CHECK(drv_disk_write(0, ref, 6, 1) == RES_OK);
		fake_miso = nc[i].m;
		t0 = fake_time_us();
		CHECK(drv_disk_write(0, ref, 7, 1) == RES_ERROR);
		dt = fake_time_us() - t0;
		printf("  карту вынули при записи, MISO = %s: ошибка через %.1f мс, дальше 0 мс\n",
				nc[i].name, dt / 1000.0);
		CHECK(dt <= (i == 0 ? 505000u : 1000u));
		t0 = fake_time_us();
		CHECK(drv_disk_write(0, ref, 8, 1) == RES_NOTRDY);
		CHECK(drv_disk_ioctl(0, CTRL_SYNC, NULL) == RES_NOTRDY);
		CHECK(fake_time_us() == t0);
	}
	// Вставили обратно — инициализируется
	fake_sd_reset(FAKE_MISO_CARD, 1);
	CHECK(drv_disk_initialize(0) == 0);
	CHECK(drv_disk_read(0, buf, 3, 1) == RES_OK);
}

/* ------------------------------------------------------------------------ */
/* Журнал настроек во флеше: settings.c, имитация сектора                  */
/* ------------------------------------------------------------------------ */

static bool set_eq(const settings_t *a, const settings_t *b) {
	return a->log_freq_hz == b->log_freq_hz && a->ema_alpha == b->ema_alpha
			&& a->bus_gap_ms == b->bus_gap_ms && a->theme == b->theme
			&& memcmp(&a->bat_alarm_v, &b->bat_alarm_v, sizeof(float)) == 0
			&& a->roll_window_s == b->roll_window_s && a->roll_rate_hz == b->roll_rate_hz
			&& a->roll_calm_cdeg == b->roll_calm_cdeg
			&& a->roll_hyst_cdeg == b->roll_hyst_cdeg;
}

static void test_settings(void) {
	settings_t s = { .log_freq_hz = 25, .ema_alpha = 0.3f, .bus_gap_ms = 20, .theme = APP_THEME_LIGHT,
			.bat_alarm_v = 10.5f, .roll_window_s = 30, .roll_rate_hz = 4, .roll_calm_cdeg = 125,
			.roll_hyst_cdeg = 15 };
	settings_t r;
	const settings_info_t *in = settings_get_info();
	CHECK(settings_crc32("123456789", 9) == 0xCBF43926u); // контрольное значение CRC-32

	fake_flash_reset();
	CHECK(!settings_load(&r));
	CHECK(!in->loaded && in->used == 0 && in->seq == 0);
	CHECK(settings_store(&s) && fake_flash_words == SETTINGS_REC_WORDS);
	memset(&r, 0, sizeof(r));
	CHECK(settings_load(&r) && set_eq(&r, &s));
	CHECK(in->loaded && in->used == 1 && in->seq == 1);
	// Те же значения — флеш не трогаем
	CHECK(settings_store(&s) && fake_flash_words == SETTINGS_REC_WORDS);
	// Несколько сохранений — берётся последнее
	for (uint16_t i = 1; i <= 10; i++) {
		s.log_freq_hz = i;
		CHECK(settings_store(&s));
	}
	CHECK(settings_load(&r) && r.log_freq_hz == 10 && in->used == 11 && in->seq == 11);
	// Испорченная последняя запись — берётся предыдущая
	fake_flash[10 * SETTINGS_REC_WORDS + 2] ^= 0x00000100u;
	CHECK(settings_load(&r) && r.log_freq_hz == 9 && in->used == 11);
	// ...а новая пишется после испорченной
	s.log_freq_hz = 33;
	CHECK(settings_store(&s) && in->used == 12);
	CHECK(settings_load(&r) && r.log_freq_hz == 33);
	// Питание пропало посреди записи (3 слова из 8): после включения — прежние
	// значения, следующая запись — на чистое место
	fake_flash_cut = 3;
	s.log_freq_hz = 44;
	CHECK(!settings_store(&s));
	fake_flash_cut = -1;
	CHECK(settings_load(&r) && r.log_freq_hz == 33 && in->used == 13);
	CHECK(settings_store(&s));
	CHECK(settings_load(&r) && r.log_freq_hz == 44 && in->used == 14);
	// Чужой формат (другая magic) не берётся
	fake_flash_reset();
	settings_load(&r);
	CHECK(settings_store(&s));
	fake_flash[0] = 0x32544553u; // "SET2"
	CHECK(!settings_load(&r));

	// Сектор заполнен: стирание и запись с начала, номер записи растёт дальше
	fake_flash_reset();
	settings_load(&r);
	for (uint32_t i = 0; i < SETTINGS_CAPACITY; i++) {
		s.log_freq_hz = (uint16_t) (1 + i % 50);
		settings_store(&s);
	}
	CHECK(fake_flash_erases == 0 && in->used == SETTINGS_CAPACITY && in->saves == SETTINGS_CAPACITY);
	s.log_freq_hz = 50;
	s.theme = APP_THEME_DARK;
	CHECK(settings_store(&s) && fake_flash_erases == 1 && in->used == 1);
	CHECK(settings_load(&r) && set_eq(&r, &s) && in->seq == SETTINGS_CAPACITY + 1);
	printf("  журнал: %u записей по %u байт на одно стирание сектора\n",
			(unsigned) SETTINGS_CAPACITY, (unsigned) SETTINGS_REC_SIZE);
}

/* ------------------------------------------------------------------------ */
/* Шина: настоящий bwm427.c, имитация USART и датчиков                      */
/* ------------------------------------------------------------------------ */

static void bus_ms(uint32_t ms) {
	while (ms--) {
		fake_ms(); // прерывания шины и таймера, SysTick
	}
}

static fake_sensor_t* add_sensor(int i, uint8_t addr) {
	fake_sensor_t *s = &fake_sensor[i];
	s->present = true;
	s->addr = addr;
	s->reg_x = (uint16_t) (10000 + 100 * addr);
	s->reg_y = (uint16_t) (10000 - 10 * addr);
	s->latency_us = 2000;
	return s;
}

// Выполнить пакет, вернуть длительность (мс) до bwm427_take()
static uint32_t bus_run(bwm427_xfer_t *x, uint8_t n) {
	uint32_t t0 = fake_tick;
	CHECK(bwm427_start(x, n));
	for (int i = 0; i < 1000; i++) {
		bus_ms(1);
		if (bwm427_take()) {
			return fake_tick - t0;
		}
	}
	CHECK(!"пакет не завершился");
	return 0;
}

static void test_bus(void) {
	bwm427_xfer_t x[4];

	fake_reset();
	bwm427_init();
	bwm427_set_gap_ms(BWM427_GAP_MS); // тайминги ниже — для минимальной паузы
	add_sensor(0, 1);
	bwm427_xfer_read(&x[0], 1);
	uint32_t dt = bus_run(x, 1);
	CHECK(x[0].result == BWM427_OK && x[0].regs[0] == 10100 && x[0].regs[1] == 9990);
	CHECK(dt <= 6);
	CHECK(!bwm427_start(x, 0));

	// Нет датчика: таймаут
	bwm427_xfer_read(&x[0], 5);
	dt = bus_run(x, 1);
	CHECK(x[0].result == BWM427_TIMEOUT);
	CHECK(dt >= BWM427_TIMEOUT_MS && dt <= BWM427_TIMEOUT_MS + 6);
	printf("  таймаут транзакции без ответа: %u мс (BWM427_TIMEOUT_MS = %u, пауза %u)\n",
			(unsigned) dt, (unsigned) BWM427_TIMEOUT_MS, (unsigned) BWM427_GAP_MS);

	// Исключение — сразу, без таймаута
	fake_sensor[0].mode = FS_EXCEPTION;
	bwm427_xfer_read(&x[0], 1);
	dt = bus_run(x, 1);
	CHECK(x[0].result == BWM427_EXCEPTION && x[0].exc_code == 2 && dt <= 6);

	fake_sensor[0].mode = FS_BAD_CRC;
	bwm427_xfer_read(&x[0], 1);
	bus_run(x, 1);
	CHECK(x[0].result == BWM427_CRC);

	// Помеха перед кадром и пауза внутри кадра
	fake_sensor[0].mode = FS_GARBAGE_SPLIT;
	bwm427_xfer_read(&x[0], 1);
	bus_run(x, 1);
	CHECK(x[0].result == BWM427_OK && x[0].regs[0] == 10100);

	// Пакет из трёх: паузы между кадрами, занятость шины
	fake_sensor[0].mode = FS_NORMAL;
	add_sensor(1, 2);
	add_sensor(2, 3);
	for (int i = 0; i < 3; i++) {
		bwm427_xfer_read(&x[i], (uint8_t) (i + 1));
	}
	uint32_t first = fake_tx_count;
	CHECK(bwm427_start(x, 3));
	CHECK(!bwm427_start(x, 3));
	CHECK(!bwm427_idle());
	bus_ms(100);
	CHECK(bwm427_take());
	CHECK(!bwm427_take());
	CHECK(bwm427_idle());
	CHECK(x[0].result == BWM427_OK && x[1].result == BWM427_OK && x[2].result == BWM427_OK);
	CHECK(x[2].regs[0] == 10300);
	for (uint32_t k = first; k + 1 < fake_tx_count; k++) {
		// конец ответа -> следующий старт: пауза BWM427_GAP_MS с точностью таймера
		uint32_t gap = fake_tx_us[k + 1] - fake_rx_done_us[k];
		CHECK(gap >= BWM427_GAP_MS * 1000u && gap <= BWM427_GAP_MS * 1000u + 2u);
	}
	CHECK(!fake_bus_violation);
}

// Тайминги шины в мкс: задержка ответа, паузы, таймауты, переполнение счётчика,
// запасной путь по SysTick
static void test_bus_timing(void) {
	bwm427_xfer_t x[4];
	const uint32_t L = 14870; // задержка ответа, при которой 2 датчика давали 15,7 Гц

	fake_reset();
	bwm427_init();
	bwm427_set_gap_ms(15);
	fake_sensor_t *s2 = add_sensor(0, 2);
	fake_sensor_t *s3 = add_sensor(1, 3);
	s2->latency_us = L;
	s3->latency_us = L;
	bus_ms(20); // пауза от инициализации
	bwm427_xfer_read(&x[0], 2);
	bwm427_xfer_read(&x[1], 3);
	uint32_t fb0 = bwm427_fallbacks();
	bus_run(x, 2);
	CHECK(x[0].result == BWM427_OK && x[1].result == BWM427_OK);
	for (int i = 0; i < 2; i++) {
		CHECK_NEAR(x[i].lat_first_us, L, 2);
		CHECK_NEAR(x[i].lat_done_us, L + BWM427_CHARS_US(9), 2);
		CHECK_NEAR(x[i].dur_us, BWM427_CHARS_US(8) + L + BWM427_CHARS_US(9), 3);
	}
	CHECK_NEAR(x[1].gap_us, 15000, 2);     // пауза ровно 15 мс, не 15..16
	CHECK(x[0].gap_us >= 15000);
	printf("  транзакция: запрос %u мкс + задержка %u + ответ %u = %u мкс, пауза перед 2-м %u мкс\n",
			(unsigned) BWM427_CHARS_US(8), (unsigned) x[1].lat_first_us,
			(unsigned) BWM427_CHARS_US(9), (unsigned) x[1].dur_us, (unsigned) x[1].gap_us);

	// Помеха перед кадром и пауза 1 мс внутри: задержка — до байта адреса
	s2->mode = FS_GARBAGE_SPLIT;
	bwm427_xfer_read(&x[0], 2);
	bus_run(x, 1);
	CHECK(x[0].result == BWM427_OK);
	CHECK_NEAR(x[0].lat_first_us, L, 2);
	CHECK_NEAR(x[0].lat_done_us, L + BWM427_CHARS_US(4) + 1000 + BWM427_CHARS_US(5), 2);
	// Исключение (5 байт, конец по паузе на линии)
	s2->mode = FS_EXCEPTION;
	bwm427_xfer_read(&x[0], 2);
	bus_run(x, 1);
	CHECK(x[0].result == BWM427_EXCEPTION);
	CHECK_NEAR(x[0].lat_done_us, L + BWM427_CHARS_US(5), 2);
	s2->mode = FS_NORMAL;

	// Таймаут транзакции: свой (короче задержки датчика) и полный
	bwm427_xfer_read(&x[0], 3);
	x[0].timeout_us = 5000;
	bus_run(x, 1);
	CHECK(x[0].result == BWM427_TIMEOUT);
	CHECK_NEAR(x[0].dur_us, BWM427_CHARS_US(8) + 5000, 2);
	bwm427_xfer_read(&x[0], 9);
	bus_run(x, 1);
	CHECK(x[0].result == BWM427_TIMEOUT && x[0].lat_first_us == 0 && x[0].lat_done_us == 0);
	CHECK_NEAR(x[0].dur_us, BWM427_CHARS_US(8) + BWM427_TIMEOUT_US, 2);
	// Поздний ответ (после своего таймаута) не ломает следующую транзакцию
	bus_ms(40);
	bwm427_xfer_read(&x[0], 2);
	bus_run(x, 1);
	CHECK(x[0].result == BWM427_OK);
	CHECK(bwm427_fallbacks() == fb0);

	// Счётчик мкс переполняется посреди пакета
	fake_us_offset = 0xFFFFFFFFu - fake_now_us() - 20000u;
	bwm427_init();
	bwm427_set_gap_ms(15);
	bus_ms(20);
	bwm427_xfer_read(&x[0], 2);
	bwm427_xfer_read(&x[1], 3);
	uint32_t n0 = fake_tx_count;
	bus_run(x, 2);
	CHECK(x[0].result == BWM427_OK && x[1].result == BWM427_OK);
	CHECK_NEAR(x[1].gap_us, 15000, 2);
	CHECK_NEAR(x[1].lat_first_us, L, 2);
	CHECK(fake_tx_us[n0 + 1] - fake_rx_done_us[n0] >= 15000u);
	CHECK(bwm427_fallbacks() == fb0);

	// Шина молчит дольше половины периода счётчика (36 мин; здесь — минута и
	// скачок счётчика): разность мкс переполнилась бы, пауза оказалась бы «в
	// будущем» — передача должна начаться сразу, без запасного пути
	fake_us_offset = 0;
	bwm427_init();
	bwm427_set_gap_ms(15);
	bus_ms(61000);
	fake_us_offset += 0x90000000u;
	bwm427_xfer_read(&x[0], 2);
	uint32_t t0 = fake_tick;
	bus_run(x, 1);
	CHECK(x[0].result == BWM427_OK && fake_tick - t0 <= 17);
	CHECK(bwm427_fallbacks() == fb0);
	fake_us_offset = 0;
	bus_ms(20);

	// TIM5 не работает: пакет всё равно проходит (SysTick, с шагом 1 мс)
	fake_timer_dead = true;
	bwm427_xfer_read(&x[0], 2);
	bwm427_xfer_read(&x[1], 3);
	n0 = fake_tx_count;
	bus_run(x, 2);
	CHECK(x[0].result == BWM427_OK && x[1].result == BWM427_OK);
	CHECK(bwm427_fallbacks() > fb0);
	CHECK(fake_tx_us[n0 + 1] - fake_rx_done_us[n0] >= 15000u);
	printf("  без TIM5 (запасной путь SysTick): пауза %u мкс, срабатываний %u\n",
			(unsigned) (fake_tx_us[n0 + 1] - fake_rx_done_us[n0]),
			(unsigned) (bwm427_fallbacks() - fb0));
	bwm427_xfer_read(&x[0], 9); // таймаут — тоже запасным путём
	bus_run(x, 1);
	CHECK(x[0].result == BWM427_TIMEOUT);
	fake_timer_dead = false;
	CHECK(!fake_bus_violation);
}

/* ------------------------------------------------------------------------ */
/* Прибор целиком: app.c + bwm427.c + sd_logger.c                           */
/* ------------------------------------------------------------------------ */

static void run_ms(uint32_t ms) {
	while (ms--) {
		fake_ms();  // прерывания USART и TIM5, SysTick
		app_task(); // суперцикл
	}
}

// Флеш чистый (настройки по умолчанию), датчики — на адресах прибора
// APP_SENSOR_ADDR(0..nsensors-1)
static void world(int nsensors) {
	fake_reset();
	fake_tick = 1;
	for (int i = 0; i < nsensors; i++) {
		add_sensor(i, APP_SENSOR_ADDR(i));
	}
}

static const app_time_t T0 = { .year = 26, .month = 10, .date = 6, .hours = 12 };

// Имя файла датчика i замера num с датой d
static const char* fname(const app_time_t *d, uint16_t num, int i) {
	static char name[SD_NAME_LEN];
	sd_make_name(name, d, num, APP_SENSOR_ADDR(i));
	return name;
}

static uint32_t requests_to(uint8_t addr, uint32_t from) {
	uint32_t n = 0;
	for (uint32_t k = from; k < fake_tx_count && k < FAKE_TX_LOG; k++) {
		n += (fake_tx_addr[k] == addr);
	}
	return n;
}

// Разобрать CSV: число строк данных, проверить заголовок, монотонность Ms
// и CalcX = RawX - OffsetX (точно до 0,001). Возвращает -1 при ошибке формата.
static int csv_rows(const char *name, uint32_t *first_ms, uint32_t *last_ms) {
	uint32_t len;
	const char *d = fake_fs_get(name, &len);
	if (!d) {
		return -1;
	}
	char *buf = malloc(len + 1);
	memcpy(buf, d, len);
	buf[len] = 0;
	const char *hdr = "Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms\n";
	if (strncmp(buf, hdr, strlen(hdr)) != 0) {
		free(buf);
		return -1;
	}
	int rows = 0;
	long prev = -1;
	char *line = buf + strlen(hdr);
	while (*line) {
		char *nl = strchr(line, '\n');
		if (!nl) {
			rows = -1; // оборванная строка
			break;
		}
		*nl = 0;
		char tmp[SD_ROW_MAX];
		snprintf(tmp, sizeof(tmp), "%s", line);
		for (char *c = tmp; *c; c++) {
			if (*c == ',') {
				*c = '.'; // десятичная запятая -> точка для sscanf
			}
		}
		int dd, mo, yy, hh, mi, ss;
		double rx, ry, ox, oy, cx, cy, bv;
		unsigned long ms;
		if (strchr(line, '.') - line != 2 || strlen(line) >= SD_ROW_MAX
				|| sscanf(tmp, "%d.%d.%d;%d:%d:%d;%lf;%lf;%lf;%lf;%lf;%lf;%lf;%lu", &dd, &mo,
						&yy, &hh, &mi, &ss, &rx, &ry, &ox, &oy, &cx, &cy, &bv, &ms) != 14
				|| yy < 2000 || fabs(cx - (rx - ox)) > 1e-6 || fabs(cy - (ry - oy)) > 1e-6
				|| (long) ms <= prev) {
			printf("  плохая строка в %s: %s\n", name, line);
			rows = -1;
			break;
		}
		if (rows == 0 && first_ms) {
			*first_ms = (uint32_t) ms;
		}
		prev = (long) ms;
		rows++;
		line = nl + 1;
	}
	if (last_ms) {
		*last_ms = (uint32_t) prev;
	}
	free(buf);
	return rows;
}

static void test_app_discovery_and_rate(void) {
	world(APP_SENSOR_COUNT);
	app_init();
	CHECK(g_app.bus_gap_ms == BWM427_GAP_DEFAULT_MS);
	CHECK(g_app.log_freq_hz == 10 && g_app.theme == APP_THEME_DARK); // флеш чистый
	app_set_bus_gap(BWM427_GAP_MS); // предельные частоты — при минимальной паузе
	CHECK(g_app.sd_state == SD_READY && g_app.file_number == 1);
	CHECK(!g_app.rtc_present);
	CHECK_NEAR(g_app.battery_v, 2000.0 / 4095.0 * 3.3 * 4.03, 0.01);
	CHECK(app_sensor_index(2) == 0 && app_sensor_index(3) == 1);
	CHECK(app_sensor_index(1) == -1 && app_sensor_index(0) == -1 && app_sensor_index(4) == -1);
	run_ms(1500);
	for (int i = 0; i < APP_SENSOR_COUNT; i++) {
		CHECK(g_app.sensor[i].addr == APP_SENSOR_ADDR(i));
		CHECK(g_app.sensor[i].status == SENSOR_OK);
		CHECK(g_app.sensor[i].err_count == 0);
	}
	CHECK_NEAR(g_app.sensor[0].raw_x, 2.0, 1e-4);   // адрес 2: 10000 + 200
	CHECK_NEAR(g_app.sensor[1].filt_y, -0.3, 1e-4); // адрес 3: 10000 - 30
	CHECK_NEAR(g_app.actual_rate_hz, 10.0, 0.3);
	CHECK(requests_to(1, 0) == 0); // адрес 1 больше не опрашивается

	static const uint16_t freqs[] = { 1, 25, 50 };
	for (int f = 0; f < 3; f++) {
		app_set_log_freq(freqs[f]);
		uint32_t from = fake_tx_count;
		run_ms(freqs[f] == 1 ? 10000 : 4000);
		uint32_t span = freqs[f] == 1 ? 10000 : 4000;
		double cycles = requests_to(APP_SENSOR_ADDR(0), from) * 1000.0 / span;
		printf("  %d датчика, задано %2u Гц: actual_rate_hz = %.2f, циклов/с по шине = %.2f\n",
				APP_SENSOR_COUNT, freqs[f], g_app.actual_rate_hz, cycles);
		CHECK_NEAR(g_app.actual_rate_hz, freqs[f], freqs[f] * 0.06 + 0.05);
	}
	CHECK(!fake_bus_violation);

	// Ограничения настроек
	app_set_log_freq(0);
	CHECK(g_app.log_freq_hz == APP_FREQ_MIN_HZ);
	app_set_log_freq(500);
	CHECK(g_app.log_freq_hz == APP_FREQ_MAX_HZ);
	app_set_ema_alpha(0.0f);
	CHECK(g_app.ema_alpha == APP_ALPHA_MIN);
	app_set_ema_alpha(3.0f);
	CHECK(g_app.ema_alpha == APP_ALPHA_MAX);
	app_set_ema_alpha(NAN);
	CHECK(g_app.ema_alpha == APP_ALPHA_MIN);
	app_set_ema_alpha(0.3f);
	CHECK_NEAR(g_app.ema_alpha, 0.3, 1e-6);
	app_set_bus_gap(0);
	CHECK(g_app.bus_gap_ms == BWM427_GAP_MS);
	app_set_bus_gap(250);
	CHECK(g_app.bus_gap_ms == BWM427_GAP_MAX_MS);
	app_set_theme(APP_THEME_LIGHT);
	CHECK(g_app.theme == APP_THEME_LIGHT);
	app_set_theme(7);
	CHECK(g_app.theme == APP_THEME_LIGHT);
	app_set_theme(APP_THEME_DARK);
	CHECK(g_app.theme == APP_THEME_DARK);
}

static void test_app_lost_and_probe(void) {
	world(APP_SENSOR_COUNT);
	app_init();
	run_ms(1000);
	CHECK(g_app.sensor[1].status == SENSOR_OK);
	run_ms(2500); // дальше пробы — в обычном темпе (после APP_BOOT_FAST_MS)
	fake_sensor[1].present = false;
	run_ms(150);
	CHECK(g_app.sensor[1].status == SENSOR_OK); // 2 неудачи — ещё не потерян
	run_ms(250);
	CHECK(g_app.sensor[1].status == SENSOR_LOST);
	CHECK(g_app.sensor[0].status == SENSOR_OK);
	uint32_t from = fake_tx_count;
	run_ms(5000);
	uint32_t probes = requests_to(APP_SENSOR_ADDR(1), from);
	printf("  потерянный датчик: %u проб за 5 с\n", (unsigned) probes);
	CHECK(probes >= 4 && probes <= 6); // ~1 проба в секунду
	uint32_t errs = g_app.sensor[1].err_count;

	// Вернулся с другим углом: фильтр сброшен, первое значение проходит как есть
	fake_sensor[1].present = true;
	fake_sensor[1].reg_x = 10555;
	run_ms(1100);
	CHECK(g_app.sensor[1].status == SENSOR_OK);
	CHECK(g_app.sensor[1].err_count == errs);
	CHECK(g_app.sensor[1].filt_x > 5.0f); // без сброса EMA был бы у 3°

	// Один датчик на 50 Гц, второго нет: пробы не съедают шину (предельная
	// частота — при минимальной паузе шины, как в test_app_discovery_and_rate)
	world(1);
	app_init();
	app_set_bus_gap(BWM427_GAP_MS);
	app_set_log_freq(50);
	run_ms(3000);
	from = fake_tx_count;
	run_ms(10000);
	uint32_t p = requests_to(APP_SENSOR_ADDR(1), from), p1 = requests_to(1, from);
	printf("  1 датчик + 1 отсутствует, 50 Гц: actual_rate_hz = %.2f, проб адреса %u за 10 с: %u\n",
			g_app.actual_rate_hz, (unsigned) APP_SENSOR_ADDR(1), (unsigned) p);
	CHECK(p >= 9 && p <= 11 && p1 == 0);
	CHECK(g_app.actual_rate_hz > 45.0f);
	CHECK(g_app.sensor[1].status == SENSOR_ABSENT && g_app.sensor[1].err_count == 0);

	// Нет ни одного датчика: частота 0, пробы раз в секунду
	world(0);
	app_init();
	run_ms(5000);
	CHECK(g_app.actual_rate_hz == 0.0f);
	CHECK(!g_app.is_stable);
}

static void test_app_stability_and_zero(void) {
	world(1);
	app_init();
	run_ms(500);
	CHECK(!g_app.is_stable); // окно ещё не заполнено
	run_ms(10000);
	CHECK(!g_app.is_stable && g_app.stab_fill_s >= 9 && g_app.stab_fill_s <= 11);
	run_ms(10000);
	CHECK(g_app.is_stable && g_app.stab_span < 1e-6f);
	CHECK(g_app.stab_fill_s == APP_STAB_WINDOW_S);
	CHECK(g_app.stab_sensor == 0);
	// Ноль не влияет на стабильность
	app_zero_all();
	CHECK(g_app.sensor[0].x == 0.0f && g_app.sensor[0].off_x == g_app.sensor[0].filt_x);
	CHECK(g_app.sensor[1].off_x == 0.0f); // нет датчика — ноль не трогаем
	run_ms(200);
	CHECK(g_app.is_stable);
	CHECK_NEAR(g_app.sensor[0].x, 0.0, 1e-5);
	app_zero_reset();
	CHECK(g_app.sensor[0].off_x == 0.0f && g_app.sensor[0].x == g_app.sensor[0].filt_x);
	// Качка 6° с периодом 1 с — нестабильно (после EMA 0,15 размах ~2,3°;
	// 3° EMA сглаживает до ~1,2° — это ещё «стабильно»)
	for (int i = 0; i < 6; i++) {
		fake_sensor[0].reg_y = (uint16_t) ((i & 1) ? 10600 : 10000);
		run_ms(500);
	}
	CHECK(!g_app.is_stable && g_app.stab_span > 1.5f);
	fake_sensor[0].present = false;
	run_ms(500);
	CHECK(!g_app.is_stable);
}

// Медленная качка ±2° с периодом 10 с: окно в 1 с (как было) видело бы за
// секунду изменение ~1,2° и показывало «ГОТОВ»; окно 20 с видит полный размах
static void test_app_stability_slow_roll(void) {
	world(1);
	app_init();
	run_ms(21000);
	CHECK(g_app.is_stable);
	for (int k = 0; k < 250; k++) { // 25 с по 100 мс
		double a = 2.0 * sin(2.0 * 3.14159265358979 * k * 0.1 / 10.0);
		fake_sensor[0].reg_y = (uint16_t) (10000 + (int) lround(a * 100.0));
		run_ms(100);
	}
	printf("  качка ±2° / 10 с: размах за 20 с = %.2f°\n", (double) g_app.stab_span);
	CHECK(!g_app.is_stable && g_app.stab_span > 3.0f);

	// Отсчёты прореживаются до 5 Гц: при опросе 50 Гц окно по-прежнему 20 с
	app_set_log_freq(50);
	run_ms(25000);
	CHECK(g_app.stab_fill_s == APP_STAB_WINDOW_S);
	CHECK(g_app.is_stable);

	// Гистерезис: «ГОТОВ» держится при размахе 1,5…1,7°, снимается выше 1,7°
	world(1);
	app_init();
	app_set_log_freq(10);
	run_ms(21000);
	CHECK(g_app.is_stable);
	uint16_t base = fake_sensor[0].reg_y;
	fake_sensor[0].reg_y = (uint16_t) (base + 160); // скачок 1,6°: в зоне гистерезиса
	run_ms(3000);
	CHECK(g_app.stab_span > 1.5f && g_app.stab_span < 1.7f);
	CHECK(g_app.is_stable);
	fake_sensor[0].reg_y = (uint16_t) (base + 180); // 1,8° — больше порога + гистерезиса
	run_ms(3000);
	CHECK(!g_app.is_stable);
	// Снова «ГОТОВ» — только когда размах за все 20 с станет < 1,5°
	run_ms(10000);
	CHECK(!g_app.is_stable);
	run_ms(12000);
	CHECK(g_app.is_stable && g_app.stab_span < 1e-3f);
}

// Качка по каждой оси каждого датчика: Д2 в покое, у Д3 качается только X
static void test_app_roll_per_axis(void) {
	world(2);
	app_init();
	app_set_log_freq(10);
	run_ms(21000);
	CHECK(g_app.is_stable);
	for (int i = 0; i < 2; i++) {
		CHECK(g_app.sensor[i].calm_x && g_app.sensor[i].calm_y);
		CHECK(g_app.sensor[i].roll_fill_s == APP_STAB_WINDOW_S);
	}
	uint16_t base_x = fake_sensor[1].reg_x;
	for (int k = 0; k < 250; k++) { // ±3° с периодом 8 с, 25 с
		double a = 3.0 * sin(2.0 * 3.14159265358979 * k * 0.1 / 8.0);
		fake_sensor[1].reg_x = (uint16_t) (base_x + (int) lround(a * 100.0));
		run_ms(100);
	}
	printf("  Д%u.X: качка %.2f°, Д%u.Y: %.2f°, Д%u: %.2f° / %.2f°\n",
			(unsigned) APP_SENSOR_ADDR(1), (double) g_app.sensor[1].roll_x,
			(unsigned) APP_SENSOR_ADDR(1), (double) g_app.sensor[1].roll_y,
			(unsigned) APP_SENSOR_ADDR(0), (double) g_app.sensor[0].roll_x,
			(double) g_app.sensor[0].roll_y);
	CHECK(g_app.sensor[0].calm_x && g_app.sensor[0].calm_y);
	CHECK(!g_app.sensor[1].calm_x && g_app.sensor[1].roll_x > 4.0f);
	CHECK(g_app.sensor[1].calm_y && g_app.sensor[1].roll_y < 1e-3f);
	CHECK(!g_app.is_stable);
	CHECK(g_app.stab_sensor == 1 && g_app.stab_axis == 0);
	CHECK_NEAR(g_app.stab_span, g_app.sensor[1].roll_x, 1e-6);
	// Д3 пропал: сводка — только по Д2, его окно не сбрасывается
	fake_sensor[1].present = false;
	run_ms(2000);
	CHECK(g_app.sensor[1].status != SENSOR_OK && !g_app.sensor[1].calm_x
			&& g_app.sensor[1].roll_fill_s == 0);
	CHECK(g_app.is_stable && g_app.stab_sensor == 0);
}

// Параметры качки: умолчания, сохранение во флеш, запись старой прошивки,
// окно 10 с и 2 отсчёта в секунду
static void test_app_roll_settings(void) {
	world(1);
	fake_flash_reset();
	app_init();
	CHECK(g_app.roll_window_s == 20 && g_app.roll_rate_hz == 5);
	CHECK_NEAR(g_app.roll_calm_deg, 1.5, 1e-6);
	CHECK_NEAR(g_app.roll_hyst_deg, 0.2, 1e-6);
	// Пределы
	app_set_roll_window(200);
	CHECK(g_app.roll_window_s == APP_ROLL_WIN_MAX_S);
	app_set_roll_rate(9);
	CHECK(g_app.roll_rate_hz == APP_ROLL_RATE_MAX_HZ);
	app_set_roll_calm(-1.0f);
	CHECK_NEAR(g_app.roll_calm_deg, APP_ROLL_CALM_MIN_DEG, 1e-6);
	app_set_roll_hyst(5.0f);
	CHECK_NEAR(g_app.roll_hyst_deg, APP_ROLL_HYST_MAX_DEG, 1e-6);
	// Сохранение и чтение после «перезагрузки»
	app_set_roll_window(10);
	app_set_roll_rate(2);
	app_set_roll_calm(0.8f);
	app_set_roll_hyst(0.1f);
	run_ms(SETTINGS_SAVE_DELAY_MS + 500);
	app_init();
	CHECK(g_app.roll_window_s == 10 && g_app.roll_rate_hz == 2);
	CHECK_NEAR(g_app.roll_calm_deg, 0.8, 1e-5);
	CHECK_NEAR(g_app.roll_hyst_deg, 0.1, 1e-5);
	// Окно 10 с, 2 отсчёта/с: «покой» через 10 с, отсчётов в окне ~20
	app_set_log_freq(10);
	run_ms(9000);
	CHECK(!g_app.is_stable && g_app.sensor[0].roll_fill_s <= 9);
	run_ms(2000);
	CHECK(g_app.is_stable && g_app.sensor[0].roll_fill_s == 10);
	// Порог 0,8°: скачок на 1,0° — уже качка
	fake_sensor[0].reg_y = (uint16_t) (fake_sensor[0].reg_y + 100);
	run_ms(3000);
	CHECK(!g_app.sensor[0].calm_y && !g_app.is_stable);

	// Запись старой прошивки: поля качки 0xFF.. — умолчания
	fake_flash_reset();
	settings_t old;
	memset(&old, 0xFF, sizeof(old));
	old.log_freq_hz = 10;
	old.ema_alpha = 0.15f;
	old.bus_gap_ms = 15;
	old.theme = APP_THEME_DARK;
	settings_load(&old); // пустой журнал: подготовить место записи
	memset(&old.roll_window_s, 0xFF, 6);
	old.log_freq_hz = 12;
	CHECK(settings_store(&old));
	app_init();
	CHECK(g_app.log_freq_hz == 12);
	CHECK(g_app.roll_window_s == 20 && g_app.roll_rate_hz == 5);
	CHECK_NEAR(g_app.roll_calm_deg, 1.5, 1e-6);
	CHECK_NEAR(g_app.roll_hyst_deg, 0.2, 1e-6);
}

static void test_app_recording(void) {
	const uint8_t all = (uint8_t) ((1u << APP_SENSOR_COUNT) - 1u);
	char nm[SD_NAME_LEN];
	world(APP_SENSOR_COUNT);
	app_init();
	app_set_time(&T0); // 2026-10-06 12:00:00
	run_ms(1000);
	CHECK(sd_logger_file_name(0, nm) && strcmp(nm, "2026-10-06_M001_D2.CSV") == 0);
	fake_sw_rec = 1;
	run_ms(30);
	CHECK(g_app.sd_state == SD_READY); // дребезг: ещё не 50 мс
	run_ms(30);
	CHECK(g_app.sd_state == SD_RECORDING && g_app.rec_sensor_mask == all);
	CHECK(sd_logger_file_name(1, nm) && strcmp(nm, "2026-10-06_M001_D3.CSV") == 0);
	run_ms(2500);
	app_zero_all(); // ноль посреди записи — меняются колонки Offset, Calc
	run_ms(2500);
	fake_sw_rec = 0;
	run_ms(100);
	CHECK(g_app.sd_state == SD_READY && g_app.file_number == 2);
	CHECK(sd_logger_file_name(1, nm) && strcmp(nm, "2026-10-06_M002_D3.CSV") == 0);
	uint32_t total = 0;
	for (int i = 0; i < APP_SENSOR_COUNT; i++) {
		uint32_t first, last;
		int rows = csv_rows(fname(&T0, 1, i), &first, &last);
		CHECK(rows >= 48 && rows <= 52);
		CHECK(first < 150 && last > 4800);
		total += (uint32_t) (rows > 0 ? rows : 0);
	}
	CHECK(fake_fs_count() == APP_SENSOR_COUNT);
	CHECK(g_app.rec_rows == total);
	CHECK(fake_fwrite_bad_size == 0); // f_write кусками по 512, «хвост» только перед f_sync
	CHECK(fake_fsync_calls >= APP_SENSOR_COUNT * 4);
	printf("  запись 5 с, %d датчика, 10 Гц: строк %u, f_write %u, f_sync %u\n",
			APP_SENSOR_COUNT, (unsigned) total, (unsigned) fake_fwrite_calls,
			(unsigned) fake_fsync_calls);

	// Второй замер — через полночь; датчик Д3 появляется посреди записи, его
	// файл всё равно с датой начала замера
	fake_sensor[1].present = false;
	run_ms(1000);
	CHECK(g_app.sensor[1].status == SENSOR_LOST);
	app_time_t t = { .year = 26, .month = 10, .date = 6, .hours = 23, .minutes = 59, .seconds = 59 };
	app_set_time(&t);
	fake_sw_rec = 1;
	run_ms(1000);
	CHECK(g_app.rec_sensor_mask == 1);
	CHECK(g_app.time.date == 7);
	fake_sensor[1].present = true;
	run_ms(2000);
	CHECK(g_app.rec_sensor_mask == all);
	fake_sw_rec = 0;
	run_ms(100);
	uint32_t first3, last3, len;
	CHECK(csv_rows("2026-10-06_M002_D2.CSV", NULL, NULL) >= 28);
	int rows3 = csv_rows("2026-10-06_M002_D3.CSV", &first3, &last3);
	CHECK(rows3 >= 8 && first3 > 900);
	CHECK(fake_fs_get("2026-10-07_M002_D3.CSV", &len) == NULL);
	CHECK(g_app.file_number == 3);
	// Следующий замер — с сегодняшней датой
	CHECK(sd_logger_file_name(0, nm) && strcmp(nm, "2026-10-07_M003_D2.CSV") == 0);

	// Без датчиков запись не создаёт файлов и не тратит номер
	world(0);
	app_init();
	int files = fake_fs_count();
	fake_sw_rec = 1;
	run_ms(1000);
	CHECK(g_app.sd_state == SD_RECORDING && g_app.rec_sensor_mask == 0);
	fake_sw_rec = 0;
	run_ms(100);
	CHECK(fake_fs_count() == files && g_app.file_number == 1);
}

static void test_app_card(void) {
	// Нумерация: наибольший номер на карте + 1 (старые и новые имена, любой
	// датчик), чужие файлы не считаются
	world(APP_SENSOR_COUNT);
	fake_fs_add("M_001_2.CSV");
	fake_fs_add("M_003_1.CSV");
	fake_fs_add("M_002_7.CSV");
	fake_fs_add("2026-10-05_M005_D3.CSV");
	fake_fs_add("2026-10-05_M12_D2.CSV");  // не по формату
	fake_fs_add("M_900_1.TXT");
	fake_fs_add("NOTES.TXT");
	app_init();
	CHECK(g_app.file_number == 6);
	app_set_time(&T0);
	// Файл с именем следующего замера уже есть (скопировали с другого прибора):
	// не перезаписывать, взять следующий номер
	fake_fs_add("2026-10-06_M006_D2.CSV");

	// Выдернули карту во время записи
	run_ms(1000);
	fake_sw_rec = 1;
	run_ms(3560);
	CHECK(g_app.file_number == 7);
	uint32_t len = 1;
	CHECK(fake_fs_get("2026-10-06_M006_D2.CSV", &len) != NULL && len == 0);
	uint32_t pull_ms = fake_tick - g_app.rec_start_ms;
	fake_card_pull();
	run_ms(500);
	CHECK(g_app.sd_state == SD_ERROR && g_app.rec_sensor_mask == 0);
	uint32_t last = 0;
	int rows = csv_rows("2026-10-06_M007_D2.CSV", NULL, &last);
	CHECK(rows > 20);
	printf("  карту вынули на %u мс записи: на карте последняя строка Ms = %u (потеряно %u мс)\n",
			(unsigned) pull_ms, (unsigned) last, (unsigned) (pull_ms - last));
	CHECK(pull_ms - last <= 1100);
	// Вставили обратно, тумблер ещё в REC — без авто-перезапуска
	fake_card_insert();
	run_ms(5000);
	CHECK(g_app.sd_state == SD_ERROR);
	fake_sw_rec = 0;
	run_ms(100);
	CHECK(g_app.sd_state == SD_READY && g_app.file_number == 8);
	fake_sw_rec = 1;
	run_ms(1000);
	CHECK(g_app.sd_state == SD_RECORDING);
	fake_sw_rec = 0;
	run_ms(100);
	CHECK(csv_rows("2026-10-06_M008_D2.CSV", NULL, NULL) >= 8);
	CHECK(csv_rows("2026-10-06_M008_D3.CSV", NULL, NULL) >= 8);

	// Без записи: карту вынули — NO_CARD за <= 2 с, вставили — READY
	fake_card_pull();
	run_ms(2100);
	CHECK(g_app.sd_state == SD_NO_CARD && g_app.file_number == 0);
	char nm[SD_NAME_LEN];
	CHECK(!sd_logger_file_name(0, nm));
	fake_sw_rec = 1; // без карты запись не начинается
	run_ms(500);
	CHECK(g_app.sd_state == SD_NO_CARD);
	fake_sw_rec = 0;
	run_ms(100);
	fake_card_insert();
	run_ms(2100);
	CHECK(g_app.sd_state == SD_READY && g_app.file_number == 9);

	// Номера кончились: запись не начинается, ничего не перезаписано
	world(APP_SENSOR_COUNT);
	fake_fs_add("2026-01-01_M9999_D2.CSV");
	app_init();
	CHECK(g_app.sd_state == SD_READY && g_app.file_number == 0);
	run_ms(1000);
	fake_sw_rec = 1;
	run_ms(200);
	CHECK(g_app.sd_state == SD_ERROR && fake_fs_count() == 1);
	fake_sw_rec = 0;
	run_ms(100);
}

// Место на карте: f_getfree только при монтировании и после записи, без
// полного обхода FAT; во время записи — счётчик FatFs
static void test_app_sd_space(void) {
	// 8 ГБ, кластер 32 КБ, счётчик свободных в FSInfo
	world(APP_SENSOR_COUNT);
	app_init();
	CHECK(g_app.sd_state == SD_READY && g_app.sd_total_mb == 8192u);
	CHECK(g_app.sd_free_mb == (FAKE_FS_CLUSTERS - 1000u) / 32u);
	CHECK(fake_getfree_calls == 1 && fake_getfree_scans == 0);

	// Кластер 512 байт, свободно ровно 100 МБ: первый же кластер записи —
	// уже 99 МБ (округление вниз)
	world(APP_SENSOR_COUNT);
	fake_fs_csize = 1;
	fake_fs_free = 100u * 2048u;
	app_init();
	app_set_time(&T0);
	CHECK(g_app.sd_free_mb == 100u);
	run_ms(1000);
	uint32_t calls = fake_getfree_calls;
	fake_sw_rec = 1;
	run_ms(3000);
	CHECK(g_app.sd_state == SD_RECORDING && g_app.sd_free_mb == 99u);
	CHECK(fake_getfree_calls == calls); // во время записи f_getfree не зовётся
	fake_sw_rec = 0;
	run_ms(100);
	CHECK(g_app.sd_state == SD_READY && g_app.sd_free_mb == 99u);
	CHECK(fake_getfree_calls == calls + 1 && fake_getfree_scans == 0);
	run_ms(10000); // проверки карты раз в 2 с f_getfree не зовут
	CHECK(fake_getfree_calls == calls + 1);

	// FSInfo без счётчика: обход всей FAT32 не делаем — место неизвестно
	world(APP_SENSOR_COUNT);
	fake_fsinfo_valid = false;
	app_init();
	CHECK(g_app.sd_state == SD_READY && g_app.sd_total_mb == 8192u);
	CHECK(g_app.sd_free_mb == APP_SD_FREE_UNKNOWN);
	CHECK(fake_getfree_calls == 0 && fake_getfree_scans == 0);

	// Карту вынули — ёмкость и место неизвестны
	fake_card_pull();
	run_ms(2100);
	CHECK(g_app.sd_state == SD_NO_CARD && g_app.sd_total_mb == 0u);
	CHECK(g_app.sd_free_mb == APP_SD_FREE_UNKNOWN);
}

static void test_app_set_address(void) {
	// Новый датчик с заводским адресом 1 (не опрашивается) -> 3 (Д3)
	world(0);
	add_sensor(0, 1);
	app_init();
	run_ms(500);
	CHECK(fake_sensor[0].requests == 0);
	CHECK(g_app.sensor[0].status == SENSOR_ABSENT && g_app.sensor[1].status == SENSOR_ABSENT);
	app_sensor_set_address(1, 3);
	CHECK(g_app.svc_state == SVC_BUSY);
	run_ms(300);
	CHECK(g_app.svc_state == SVC_OK);
	CHECK(fake_sensor[0].addr == 3 && fake_sensor[0].saved);
	run_ms(500);
	CHECK(g_app.sensor[1].status == SENSOR_OK);

	// Д2 -> 3: старый адрес пропал, новый найден сразу
	world(1);
	app_init();
	run_ms(500);
	CHECK(g_app.sensor[0].status == SENSOR_OK);
	app_sensor_set_address(2, 3);
	run_ms(300);
	CHECK(g_app.svc_state == SVC_OK && fake_sensor[0].addr == 3 && fake_sensor[0].saved);
	CHECK(g_app.sensor[0].status == SENSOR_ABSENT);
	run_ms(500);
	CHECK(g_app.sensor[1].status == SENSOR_OK);

	// Эхо приходит уже с нового адреса
	world(0);
	add_sensor(0, 1);
	fake_sensor[0].reply_from_new = true;
	app_init();
	run_ms(500);
	app_sensor_set_address(1, 2);
	run_ms(300);
	CHECK(g_app.svc_state == SVC_OK && fake_sensor[0].addr == 2 && fake_sensor[0].saved);
	run_ms(500);
	CHECK(g_app.sensor[0].status == SENSOR_OK);

	// Любой адрес 1..247, в том числе не из опрашиваемых
	world(1);
	app_init();
	run_ms(500);
	app_sensor_set_address(2, 200);
	run_ms(300);
	CHECK(g_app.svc_state == SVC_OK && fake_sensor[0].addr == 200 && fake_sensor[0].saved);
	CHECK(g_app.sensor[0].status == SENSOR_ABSENT);

	// Датчик отвечает исключением
	world(0);
	add_sensor(0, 1);
	app_init();
	run_ms(500);
	fake_sensor[0].mode = FS_EXCEPTION;
	app_sensor_set_address(1, 2);
	run_ms(300);
	CHECK(g_app.svc_state == SVC_FAIL && fake_sensor[0].addr == 1);

	// Нет датчика: таймауты, отказ
	world(0);
	app_init();
	app_sensor_set_address(1, 2);
	run_ms(500);
	CHECK(g_app.svc_state == SVC_FAIL);

	// Недопустимые аргументы и занятый адрес
	world(2);
	app_init();
	run_ms(500);
	app_sensor_set_address(1, 3);
	CHECK(g_app.svc_state == SVC_FAIL); // 3 занят отвечающим датчиком Д3
	app_sensor_set_address(0, 5);
	CHECK(g_app.svc_state == SVC_FAIL);
	app_sensor_set_address(1, 1);
	CHECK(g_app.svc_state == SVC_FAIL);
	app_sensor_set_address(5, 248);
	CHECK(g_app.svc_state == SVC_FAIL);
}

static void test_app_settings(void) {
	const settings_info_t *in = settings_get_info();
	world(APP_SENSOR_COUNT);
	app_init();
	CHECK(!in->loaded && !in->pending);
	CHECK(g_app.log_freq_hz == 10 && g_app.bus_gap_ms == BWM427_GAP_DEFAULT_MS
			&& g_app.theme == APP_THEME_DARK);
	CHECK_NEAR(g_app.ema_alpha, 0.15, 1e-6);
	run_ms(500);
	// Изменения пачкой: одна запись через 3 с после последнего
	app_set_log_freq(25);
	app_set_ema_alpha(0.3f);
	run_ms(2000);
	app_set_theme(APP_THEME_LIGHT);
	run_ms(2900);
	CHECK(fake_flash_words == 0 && in->pending);
	run_ms(200);
	CHECK(fake_flash_words == SETTINGS_REC_WORDS && !in->pending && fake_flash_erases == 0);
	// «Перезагрузка» (флеш не трогаем): значения на месте, ноль датчиков — нет
	app_zero_all();
	CHECK(g_app.sensor[0].off_x != 0.0f);
	app_init();
	CHECK(in->loaded && in->used == 1);
	CHECK(g_app.log_freq_hz == 25 && g_app.theme == APP_THEME_LIGHT
			&& g_app.bus_gap_ms == BWM427_GAP_DEFAULT_MS);
	CHECK_NEAR(g_app.ema_alpha, 0.3, 1e-6);
	CHECK(g_app.sensor[0].off_x == 0.0f);
	run_ms(1500);
	CHECK(g_app.sensor[0].status == SENSOR_OK);
	CHECK_NEAR(g_app.actual_rate_hz, 25.0, 1.5); // сохранённая частота в работе

	// Во время записи замера не сохраняется — только после STOP
	fake_sw_rec = 1;
	run_ms(100);
	CHECK(g_app.sd_state == SD_RECORDING);
	app_set_bus_gap(20);
	run_ms(5000);
	CHECK(fake_flash_words == SETTINGS_REC_WORDS && in->pending);
	fake_sw_rec = 0;
	run_ms(100);
	CHECK(fake_flash_words == 2 * SETTINGS_REC_WORDS && !in->pending);
	// То же значение — не изменение; туда и обратно до сохранения — без записи
	app_set_bus_gap(20);
	CHECK(!in->pending);
	app_set_bus_gap(30);
	app_set_bus_gap(20);
	run_ms(3500);
	CHECK(fake_flash_words == 2 * SETTINGS_REC_WORDS && !in->pending);
	app_init();
	CHECK(g_app.bus_gap_ms == 20 && in->seq == 2);

	// Значения вне пределов (запись другой версии прошивки) приводятся в пределы
	settings_t bad = { .log_freq_hz = 500, .ema_alpha = NAN, .bus_gap_ms = 0, .theme = 9,
			.bat_alarm_v = 99.0f };
	CHECK(settings_store(&bad));
	app_init();
	CHECK(g_app.log_freq_hz == APP_FREQ_MAX_HZ && g_app.ema_alpha == APP_ALPHA_MIN
			&& g_app.bus_gap_ms == BWM427_GAP_MS && g_app.theme == APP_THEME_LIGHT
			&& g_app.bat_alarm_v == APP_BAT_ALARM_MAX_V);
}

static void test_app_clock(void) {
	world(0);
	app_init();
	app_time_t t = { .year = 25, .month = 12, .date = 31, .hours = 23, .minutes = 59, .seconds = 58 };
	app_set_time(&t);
	run_ms(1999);
	CHECK(time_eq(&g_app.time, 31, 12, 25, 23, 59, 59));
	run_ms(1);
	CHECK(time_eq(&g_app.time, 1, 1, 26, 0, 0, 0));
	t = (app_time_t ) { .year = 25, .month = 2, .date = 31, .hours = 30, .minutes = 61, .seconds = 99 };
	app_set_time(&t);
	CHECK(time_eq(&g_app.time, 28, 2, 25, 23, 59, 59));
	// Ход за час без накопления ошибки
	run_ms(3600000);
	CHECK(time_eq(&g_app.time, 1, 3, 25, 0, 59, 59));
}

// Первый запрос к адресу addr в журнале передач начиная с from (FAKE_TX_LOG — нет)
static uint32_t tx_find(uint8_t addr, uint32_t from) {
	for (uint32_t k = from; k < fake_tx_count && k < FAKE_TX_LOG; k++) {
		if (fake_tx_addr[k] == addr) {
			return k;
		}
	}
	return FAKE_TX_LOG;
}

static void test_app_bus_stats(void) {
	uint32_t fb0 = bwm427_fallbacks(); // счётчик драйвера — с «включения»
	world(APP_SENSOR_COUNT);
	fake_sensor[0].latency_us = 3000; // Д2: 3,0..3,4 мс
	fake_sensor[0].jitter_us = 400;
	fake_sensor[1].latency_us = 7000; // Д3: ровно 7 мс
	app_init();
	app_set_bus_gap(5);
	app_set_log_freq(50);
	run_ms(2000);
	CHECK(g_app.sensor[0].status == SENSOR_OK && g_app.sensor[1].status == SENSOR_OK);
	app_bus_stats_reset();
	CHECK(g_app.sensor[0].lat_first.n == 0 && g_app.bus.cycle.n == 0);
	run_ms(5000);
	const app_sensor_t *d2 = &g_app.sensor[0], *d3 = &g_app.sensor[1];
	CHECK(d2->lat_first.n > 100 && d2->lat_first.n == d3->lat_first.n);
	CHECK(d2->lat_first.min_us >= 3000 && d2->lat_first.max_us <= 3401);
	CHECK(d2->lat_first.max_us - d2->lat_first.min_us > 300); // разброс виден
	CHECK_NEAR(app_stat_avg_us(&d2->lat_first), 3200, 60);
	CHECK_NEAR(d3->lat_first.min_us, 7000, 2);
	CHECK_NEAR(d3->lat_first.max_us, 7000, 2);
	CHECK_NEAR(app_stat_avg_us(&d3->lat_done), 7000 + BWM427_CHARS_US(9), 2);
	CHECK(d2->timeout_count == 0 && d2->crc_count == 0 && d2->frame_count == 0);
	CHECK(d3->timeout_count == 0 && d3->crc_count == 0 && d3->frame_count == 0);
	// Паузы внутри цикла — ровно 5 мс; простоя перед циклом нет (шина не успевает за 50 Гц)
	CHECK(g_app.bus.gap.n > 100 && g_app.bus.gap.min_us >= 5000 && g_app.bus.gap.max_us <= 5002);
	CHECK(g_app.bus.idle.max_us <= 2);
	// Цикл = 2 × (пауза + запрос + ответ) + задержки обоих датчиков
	double cyc = 2.0 * (5000 + BWM427_CHARS_US(8) + BWM427_CHARS_US(9)) + 3200 + 7000;
	CHECK_NEAR(app_stat_avg_us(&g_app.bus.cycle), cyc, 70);
	CHECK_NEAR(g_app.actual_rate_hz, 1e6 / cyc, 0.5);
	CHECK(g_app.bus.timer_fallbacks == fb0 && bwm427_fallbacks() == fb0);
	printf("  Д2 задержка %u..%u (ср. %u) мкс, Д3 %u мкс; цикл %u мкс -> %.2f Гц (по шине %.2f Гц)\n",
			(unsigned) d2->lat_first.min_us, (unsigned) d2->lat_first.max_us,
			(unsigned) app_stat_avg_us(&d2->lat_first), (unsigned) d3->lat_first.max_us,
			(unsigned) app_stat_avg_us(&g_app.bus.cycle), g_app.actual_rate_hz,
			1e6 / app_stat_avg_us(&g_app.bus.cycle));

	// Таймаут известного датчика: 2 × наибольшая задержка + кадр + запас,
	// не меньше 10 мс
	CHECK(d2->timeout_us == 10000);
	CHECK_NEAR(d3->timeout_us, 2 * 7000 + BWM427_CHARS_US(9) + 2000, 2);
	// Д3 замолчал: первый пропуск — по короткому таймауту, дальше — полный
	fake_sensor[1].present = false;
	uint32_t from = fake_tx_count;
	run_ms(300);
	uint32_t k1 = tx_find(3, from), k2 = (k1 < FAKE_TX_LOG) ? tx_find(3, k1 + 1) : FAKE_TX_LOG;
	CHECK(k1 < FAKE_TX_LOG && k2 < FAKE_TX_LOG);
	if (k2 < FAKE_TX_LOG) {
		// после запроса к Д3: его таймаут, пауза, следующий цикл начинается с Д2
		uint32_t to1 = fake_tx_us[k1 + 1] - fake_tx_us[k1] - BWM427_CHARS_US(8) - 5000;
		uint32_t to2 = fake_tx_us[k2 + 1] - fake_tx_us[k2] - BWM427_CHARS_US(8) - 5000;
		printf("  Д3 пропал: таймаут 1-го пропуска %u мкс, 2-го %u мкс\n", (unsigned) to1,
				(unsigned) to2);
		CHECK_NEAR(to1, 2 * 7000 + BWM427_CHARS_US(9) + 2000, 3);
		CHECK_NEAR(to2, BWM427_TIMEOUT_US, 3);
	}
	CHECK(d3->status == SENSOR_LOST && d3->timeout_count >= 3);
	CHECK(d2->timeout_count == 0);
	// Пропавший датчик: пробы — с полным таймаутом
	CHECK(d3->timeout_us == BWM427_TIMEOUT_US);
	// Битая CRC и чужой кадр
	fake_sensor[1].present = true;
	run_ms(1500);
	CHECK(d3->status == SENSOR_OK);
	fake_sensor[1].mode = FS_BAD_CRC;
	run_ms(500); // три битых подряд — LOST, дальше пробы раз в секунду
	CHECK(d3->crc_count >= 3 && d3->frame_count == 0 && d3->status == SENSOR_LOST);
	uint32_t crc = d3->crc_count, tmo = d3->timeout_count;
	fake_sensor[1].mode = FS_EXCEPTION; // исключение — не битый кадр и не таймаут
	run_ms(1500);
	CHECK(d3->crc_count == crc && d3->frame_count == 0 && d3->timeout_count == tmo);
	CHECK(d3->last_err == BWM427_EXCEPTION);
	fake_sensor[1].mode = FS_NORMAL;
	// Сброс статистики: таймаут снова полный, пока задержка не известна
	app_bus_stats_reset();
	CHECK(d3->crc_count == 0 && d3->timeout_count == 0 && d2->lat_first.n == 0);
	CHECK(g_app.bus.gap.n == 0 && g_app.bus.idle.n == 0 && g_app.bus.cycle.n == 0);
	run_ms(30);
	CHECK(d2->timeout_us == BWM427_TIMEOUT_US);
	CHECK(!fake_bus_violation);
}

// Суперцикл, который раз в period мс занят block мс (отрисовка LVGL)
static void run_ms_busy(uint32_t ms, uint32_t period, uint32_t block) {
	for (uint32_t t = 0; t < ms; t++) {
		fake_ms();
		if (t % period >= block) {
			app_task();
		}
	}
}

// Модель предела 15,7 Гц: два датчика с задержкой ответа ~14,9 мс, пауза 15 мс
static void test_app_rate_model(void) {
	const uint32_t L = 14870;
	const double tx = BWM427_CHARS_US(8), rx = BWM427_CHARS_US(9);
	double old_cyc = 2.0 * (15000 + 500 + tx + L + rx); // пауза тиками: 15..16 мс
	double new_cyc = 2.0 * (15000 + tx + L + rx);
	printf("  модель: цикл = 2 × (пауза + запрос %.0f + задержка %u + ответ %.0f мкс)\n", tx,
			(unsigned) L, rx);
	printf("  прежняя прошивка (пауза 15,5 мс в среднем): %.0f мкс -> %.2f Гц\n", old_cyc,
			1e6 / old_cyc);
	world(APP_SENSOR_COUNT);
	fake_sensor[0].latency_us = L;
	fake_sensor[1].latency_us = L;
	app_init();
	CHECK(g_app.bus_gap_ms == 15);
	app_set_log_freq(50);
	run_ms(3000);
	app_bus_stats_reset();
	run_ms(5000);
	printf("  новая (пауза по TIM5): actual_rate_hz = %.2f, цикл %u мкс\n", g_app.actual_rate_hz,
			(unsigned) app_stat_avg_us(&g_app.bus.cycle));
	CHECK_NEAR(app_stat_avg_us(&g_app.bus.cycle), new_cyc, 3);
	CHECK_NEAR(g_app.actual_rate_hz, 1e6 / new_cyc, 0.1);
	CHECK(g_app.sensor[0].timeout_us == BWM427_TIMEOUT_US); // 2 × 14,9 мс > 30 мс

	// Предел по паузе (если датчики при ней ещё отвечают — проверяется на плате)
	static const uint8_t gaps[] = { 15, 12, 10, 8, 6, 5, 4, 3, 2 };
	printf("  пауза, мс -> предел, Гц (2 датчика, задержка %.2f мс):", L / 1000.0);
	for (size_t g = 0; g < sizeof(gaps); g++) {
		app_set_bus_gap(gaps[g]);
		run_ms(1500);
		app_bus_stats_reset();
		run_ms(2000);
		double c = 2.0 * (gaps[g] * 1000.0 + tx + L + rx);
		printf(" %u->%.1f", (unsigned) gaps[g], 1e6 / app_stat_avg_us(&g_app.bus.cycle));
		CHECK_NEAR(app_stat_avg_us(&g_app.bus.cycle), c, 3);
		CHECK_NEAR(g_app.actual_rate_hz, 1e6 / c, 0.25);
	}
	printf("\n");

	// Суперцикл занят 25 мс каждые 100 мс: цикл ждёт его сверх паузы (простой)
	static const uint8_t gaps2[] = { 15, 5 };
	for (size_t g = 0; g < sizeof(gaps2); g++) {
		app_set_bus_gap(gaps2[g]);
		run_ms(1000);
		app_bus_stats_reset();
		run_ms_busy(5000, 100, 25);
		printf("  пауза %u мс, суперцикл занят 25 мс из 100: %.2f Гц, простой перед циклом"
				" ср. %u / макс. %u мкс\n", (unsigned) gaps2[g], g_app.actual_rate_hz,
				(unsigned) app_stat_avg_us(&g_app.bus.idle), (unsigned) g_app.bus.idle.max_us);
		CHECK(g_app.bus.idle.max_us > (25u - gaps2[g]) * 1000u / 2u);
		CHECK(app_stat_avg_us(&g_app.bus.idle) > 100);
	}
	CHECK(!fake_bus_violation);
}

/* ------------------------------------------------------------------------ */
/* Загрузка процессора                                                      */
/* ------------------------------------------------------------------------ */

// Проходы суперцикла по pattern (тактов на проход, по кругу) до конца
// очередного секундного окна app_diag_loop. Время (fake_tick) — по тактам 96 МГц.
static uint64_t s_cpu_cyc;
static void cpu_window(const uint32_t *pattern, uint32_t n) {
	uint32_t up = g_app.diag.uptime_s;
	for (uint32_t k = 0; g_app.diag.uptime_s == up; k++) {
		s_cpu_cyc += pattern[k % n];
		fake_dwt.CYCCNT = (uint32_t) s_cpu_cyc;
		fake_tick = (uint32_t) (s_cpu_cyc / 96000u);
		app_diag_loop(0, 0);
	}
}

static void test_cpu_load(void) {
	uint32_t idle = 0;
	// Чистая функция: окно 96e6 тактов (1 с)
	CHECK(app_cpu_load_calc(96000000u, 50000u, 1920u, &idle) == 0 && idle == 1920);
	CHECK(app_cpu_load_calc(96000000u, 25000u, 1920u, &idle) == 50);
	CHECK(app_cpu_load_calc(96000000u, 0u, 0u, &idle) == 100);       // суперцикл стоял
	CHECK(app_cpu_load_calc(96000000u, 30u, 3200000u, &idle) == 100  // всё окно занято:
			&& idle == 1920);                                            // оценка не портится
	CHECK(app_cpu_load_calc(96000000u, 40000u, 2400u, &idle) == 0 && idle == 2400); // дрейф вверх
	CHECK(app_cpu_load_calc(96000000u, 40000u, 1200u, &idle) == 50 && idle == 1200); // и вниз
	CHECK(app_cpu_load_calc(96000000u, 100000u, 1200u, &idle) == 0); // не меньше 0

	// Через app_diag_loop и DWT: окно — секунда по HAL_GetTick
	fake_reset();
	s_cpu_cyc = 0;
	fake_dwt.CYCCNT = 0;
	static const uint32_t idle_only[] = { 1920 };
	cpu_window(idle_only, 1); // первое окно (неполное)
	cpu_window(idle_only, 1);
	CHECK(g_app.diag.cpu_load_pct == 0 && g_app.diag.idle_pass_cyc == 1920);
	CHECK(g_app.diag.pass_min_cyc == 1920);
	// 400 холостых по 20 мкс и один проход 2 мс (работа ~1,98 мс из 10 мс)
	static uint32_t mix[401];
	for (int i = 0; i < 400; i++) {
		mix[i] = 1920;
	}
	mix[400] = 192000;
	cpu_window(mix, 401);
	cpu_window(mix, 401);
	printf("  20 мкс холостой проход, раз в 10 мс работа 2 мс: загрузка %u %%\n",
			(unsigned) g_app.diag.cpu_load_pct);
	CHECK_NEAR(g_app.diag.cpu_load_pct, 20, 1);
	// Всё окно занято долгими проходами (33 мс) — почти 100 %, оценка холостого цела
	static const uint32_t busy[] = { 3200000 };
	cpu_window(busy, 1);
	cpu_window(busy, 1);
	CHECK(g_app.diag.cpu_load_pct >= 99 && g_app.diag.idle_pass_cyc == 1920);
	// Прерывания удлиняют проходы: каждый 4-й на 2 мкс длиннее -> ~2,5 %
	static const uint32_t irq[] = { 1920, 1920, 1920, 2112 };
	cpu_window(irq, 4);
	cpu_window(irq, 4);
	CHECK_NEAR(g_app.diag.cpu_load_pct, 2, 1);
}

/* ------------------------------------------------------------------------ */
/* Батарея                                                                  */
/* ------------------------------------------------------------------------ */

static void test_app_battery(void) {
	// Заряд 3S Li-ion по кривой ячейки
	CHECK(app_bat_pct(12.6f) == 100 && app_bat_pct(13.2f) == 100);
	CHECK(app_bat_pct(9.9f) == 0 && app_bat_pct(9.0f) == 0 && app_bat_pct(NAN) == 0);
	CHECK(app_bat_pct(11.4f) == 50);  // 3,80 В на ячейку
	CHECK(app_bat_pct(12.0f) == 78);  // 4,00
	CHECK(app_bat_pct(10.5f) == 8);   // 3,50
	CHECK(app_bat_pct(11.25f) == 42); // 3,75: между 33 и 50
	CHECK(app_bat_pct(12.45f) == 95); // 4,15: между 90 и 100
	uint8_t prev = 0;
	bool mono = true;
	for (float v = 9.0f; v <= 13.0f; v += 0.01f) {
		uint8_t p = app_bat_pct(v);
		mono = mono && p >= prev;
		prev = p;
	}
	CHECK(mono);

	// Наличие, процент, тревога с гистерезисом (порог 10,0, гистерезис 0,3)
	world(0);
	fake_adc_bat = fake_adc_for_volts(12.0f);
	app_init();
	CHECK(g_app.bat_alarm_v == APP_BAT_ALARM_DEFAULT_V);
	CHECK(g_app.battery_present && !g_app.battery_low && !g_app.battery_adc_sat);
	CHECK_NEAR(g_app.battery_v, 12.0, 0.02);
	CHECK_NEAR(g_app.battery_pct, 78, 1);
	fake_adc_bat = fake_adc_for_volts(9.8f);
	run_ms(700); // 1-2 средних ниже порога — ещё не тревога
	CHECK(!g_app.battery_low);
	run_ms(700);
	CHECK(g_app.battery_low && g_app.battery_present && g_app.battery_pct == 0);
	fake_adc_bat = fake_adc_for_volts(10.2f); // выше порога, но в полосе гистерезиса
	run_ms(1500);
	CHECK(g_app.battery_low);
	fake_adc_bat = fake_adc_for_volts(10.4f);
	run_ms(700);
	CHECK(!g_app.battery_low);
	// Одиночный провал (одно среднее) — без тревоги
	fake_adc_bat = fake_adc_for_volts(9.5f);
	run_ms(150);
	fake_adc_bat = fake_adc_for_volts(11.0f);
	run_ms(1500);
	CHECK(!g_app.battery_low);
	// Только USB (~4 В на делителе): АКБ нет, тревоги нет, процента нет
	fake_adc_bat = fake_adc_for_volts(9.0f);
	run_ms(1500);
	CHECK(g_app.battery_low);
	fake_adc_bat = fake_adc_for_volts(4.0f);
	run_ms(700);
	CHECK(!g_app.battery_present && !g_app.battery_low && g_app.battery_pct == APP_BAT_PCT_NONE);
	// АЦП у предела шкалы
	fake_adc_bat = 4095;
	run_ms(700);
	CHECK(g_app.battery_adc_sat && g_app.battery_v > 13.2f);
	printf("  полная 3S 12,6 В -> АЦП %u из 4095 (запас %.1f %%), предел шкалы %.2f В\n",
			(unsigned) fake_adc_for_volts(12.6f), 100.0 * (1.0 - 12.6 / (3.3 * 4.03)),
			3.3 * 4.03);

	// Порог: пределы, сохранение во флеш, старая запись без поля
	const settings_info_t *in = settings_get_info();
	app_set_bat_alarm(3.0f);
	CHECK(g_app.bat_alarm_v == APP_BAT_ALARM_MIN_V);
	app_set_bat_alarm(40.0f);
	CHECK(g_app.bat_alarm_v == APP_BAT_ALARM_MAX_V);
	app_set_bat_alarm(NAN);
	CHECK(g_app.bat_alarm_v == APP_BAT_ALARM_DEFAULT_V);
	run_ms(3500);
	CHECK(!in->pending);
	uint32_t w0 = fake_flash_words;
	app_set_bat_alarm(10.0f); // то же значение — не изменение
	CHECK(!in->pending);
	app_set_bat_alarm(10.5f);
	CHECK(in->pending);
	run_ms(3500);
	CHECK(!in->pending && fake_flash_words == w0 + SETTINGS_REC_WORDS);
	app_init();
	CHECK_NEAR(g_app.bat_alarm_v, 10.5, 1e-6);
	app_set_bat_alarm(10.5f);
	CHECK(!in->pending);
	// Запись старой прошивки: на месте поля — 0xFFFFFFFF (резерв)
	settings_rec_t r;
	memset(&r, 0xFF, sizeof(r));
	r.magic = SETTINGS_MAGIC;
	r.seq = 7;
	r.log_freq_hz = 25;
	r.bus_gap_ms = 15;
	r.theme = APP_THEME_LIGHT;
	r.ema_alpha = 0.2f;
	r.crc = settings_crc32(&r, SETTINGS_REC_SIZE - 4u);
	fake_flash_reset();
	memcpy(fake_flash, &r, sizeof(r));
	app_init();
	CHECK(in->loaded && g_app.log_freq_hz == 25 && g_app.theme == APP_THEME_LIGHT);
	CHECK(g_app.bat_alarm_v == APP_BAT_ALARM_DEFAULT_V);
	// Следующее сохранение — уже с порогом
	app_set_bat_alarm(11.0f);
	run_ms(3500);
	app_init();
	CHECK(g_app.log_freq_hz == 25 && g_app.bat_alarm_v == 11.0f && in->seq == 8);
}

int main(void) {
	struct {
		const char *name;
		void (*fn)(void);
	} tests[] = {
		{ "CRC16 и запрос", test_crc },
		{ "углы", test_decode },
		{ "медиана и EMA", test_filters },
		{ "разбор ответов Modbus", test_parse },
		{ "календарь", test_calendar },
		{ "строка CSV и имена файлов", test_csv },
		{ "драйвер SD: без карты, карта, выдёргивание", test_sd_driver },
		{ "журнал настроек во флеше", test_settings },
		{ "шина: транзакции, таймауты, паузы", test_bus },
		{ "шина: тайминги в мкс, задержка ответа, запасной путь", test_bus_timing },
		{ "прибор: поиск датчиков, частота", test_app_discovery_and_rate },
		{ "прибор: потеря датчика, пробы", test_app_lost_and_probe },
		{ "прибор: стабильность, ноль", test_app_stability_and_zero },
		{ "прибор: качка, окно 20 с, гистерезис", test_app_stability_slow_roll },
		{ "прибор: качка по каждой оси", test_app_roll_per_axis },
		{ "прибор: параметры качки", test_app_roll_settings },
		{ "прибор: запись на SD", test_app_recording },
		{ "прибор: карта, нумерация", test_app_card },
		{ "прибор: место на карте", test_app_sd_space },
		{ "прибор: смена адреса", test_app_set_address },
		{ "прибор: настройки во флеше", test_app_settings },
		{ "прибор: часы", test_app_clock },
		{ "прибор: статистика шины, таймаут ответа", test_app_bus_stats },
		{ "прибор: предел частоты (модель 15,7 Гц)", test_app_rate_model },
		{ "загрузка процессора", test_cpu_load },
		{ "прибор: АКБ — заряд, тревога, порог во флеше", test_app_battery },
	};
	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		int before = s_fails;
		printf("[%s]\n", tests[i].name);
		tests[i].fn();
		printf("  %s\n", s_fails == before ? "ok" : "ОШИБКИ");
	}
	printf("\n%d проверок, %d ошибок\n", s_checks, s_fails);
	return s_fails ? 1 : 0;
}
