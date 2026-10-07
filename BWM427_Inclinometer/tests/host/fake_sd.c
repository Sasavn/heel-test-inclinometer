/*
 * fake_sd.c — шина SPI2 и SD-карта для настоящего драйвера Core/Src/fatfs_sd.c
 * (собран с -DSD_SPI_FAKE в sd_drv.c, функции под именами drv_disk_*).
 *
 * Время: каждый байт SPI сдвигает fake_tick на длительность передачи на
 * текущей скорости (375 кГц — 21,3 мкс, 12 МГц — 0,67 мкс). Накладные расходы
 * процессора не учитываются — оценка снизу, но таймауты драйвера (HAL_GetTick)
 * отрабатывают так же, как на МК.
 *
 * Линия MISO: настоящая карта (FAKE_MISO_CARD) или «карты нет» — модуль читает
 * постоянно 0x00, постоянно 0xFF или шум.
 *
 * Карта: SDHC в режиме SPI. CMD0/8/55/41/58/16/17/24, ответ R1 через один
 * байт после команды, занятость после записи блока (DO = 0 при CS = 0).
 */
#include "fake.h"
#include <string.h>

fake_miso_t fake_miso = FAKE_MISO_CARD;
uint16_t fake_sd_acmd41_polls;   // сколько ACMD41 карта ещё отвечает «idle»
uint32_t fake_sd_busy_us;        // занятость после записи блока
uint8_t fake_sd_cmd0_junk;       // первые N ответов на CMD0 — «не idle» (карта посреди обмена)
uint32_t fake_spi_bytes;
uint8_t fake_sd_mem[FAKE_SD_SECTORS][512];

static uint64_t s_now_ns;        // время шины, нс (fake_tick — его целые мс)
static uint32_t s_byte_ns = 21333;
static uint32_t s_seed = 1;
static bool s_cs_low;

// Состояние карты
static bool s_spi_mode;          // был CMD0 при CS = 0
static bool s_ready;             // вышла из idle (ACMD41)
static bool s_app;               // предыдущая команда — CMD55
static uint8_t s_cmd[6];
static uint8_t s_cmd_n;
static uint8_t s_out[600];       // очередь ответа на DO
static uint16_t s_out_n, s_out_i;
static bool s_busy_after_out;    // по опустошении очереди — занятость
static uint64_t s_busy_end_ns;
static bool s_wr_token;          // ждём маркер данных 0xFE
static bool s_wr_data;           // принимаем блок
static uint16_t s_wr_n;
static uint32_t s_wr_sector;
static uint8_t s_wr_buf[514];

uint64_t fake_time_us(void) {
	return s_now_ns / 1000u;
}

static void advance(uint32_t ns) {
	s_now_ns += ns;
	fake_tick = (uint32_t) (s_now_ns / 1000000u);
}

static uint8_t noise(void) {
	s_seed = s_seed * 1103515245u + 12345u;
	return (uint8_t) (s_seed >> 16);
}

static void q(uint8_t b) {
	if (s_out_n < sizeof(s_out)) {
		s_out[s_out_n++] = b;
	}
}

static void card_exec(void) {
	uint8_t c = s_cmd[0] & 0x3F;
	uint32_t arg = ((uint32_t) s_cmd[1] << 24) | ((uint32_t) s_cmd[2] << 16)
			| ((uint32_t) s_cmd[3] << 8) | s_cmd[4];
	bool app = s_app;
	s_app = false;
	s_out_n = s_out_i = 0;
	if (c == 0) {
		s_spi_mode = true;
		s_ready = false;
		q(0xFF);
		if (fake_sd_cmd0_junk) {
			fake_sd_cmd0_junk--;
			q(0x00); // ответ «не idle»: карта осталась в каком-то обмене
		} else {
			q(0x01);
		}
		return;
	}
	if (!s_spi_mode) {
		return; // до CMD0 карта в режиме SD и по SPI не отвечает
	}
	uint8_t r1 = s_ready ? 0x00 : 0x01;
	q(0xFF); // NCR: ответ через байт
	switch (c) {
	case 8:
		q(r1);
		q(0x00);
		q(0x00);
		q((uint8_t) (arg >> 8) & 0x0F);
		q((uint8_t) arg);
		break;
	case 55:
		s_app = true;
		q(r1);
		break;
	case 41:
		if (!app) {
			q(r1 | 0x04);
		} else if (fake_sd_acmd41_polls) {
			fake_sd_acmd41_polls--;
			q(0x01);
		} else {
			s_ready = true;
			q(0x00);
		}
		break;
	case 58:
		q(r1);
		q(s_ready ? 0xC0 : 0x80); // готова + CCS (SDHC)
		q(0xFF);
		q(0x80);
		q(0x00);
		break;
	case 16:
		q(r1);
		break;
	case 17:
		if (!s_ready || arg >= FAKE_SD_SECTORS) {
			q(0x04 | r1);
			break;
		}
		q(0x00);
		q(0xFF);
		q(0xFF);
		q(0xFE);
		for (int i = 0; i < 512; i++) {
			q(fake_sd_mem[arg][i]);
		}
		q(0x00);
		q(0x00);
		break;
	case 24:
		if (!s_ready || arg >= FAKE_SD_SECTORS) {
			q(0x04 | r1);
			break;
		}
		q(0x00);
		s_wr_token = true;
		s_wr_sector = arg;
		break;
	default:
		q(r1 | 0x04); // неизвестная команда
		break;
	}
}

static void card_in(uint8_t b) {
	if (s_wr_token) {
		if (b == 0xFE) {
			s_wr_token = false;
			s_wr_data = true;
			s_wr_n = 0;
		}
		return;
	}
	if (s_wr_data) {
		s_wr_buf[s_wr_n++] = b;
		if (s_wr_n == sizeof(s_wr_buf)) { // 512 байт + CRC
			s_wr_data = false;
			memcpy(fake_sd_mem[s_wr_sector], s_wr_buf, 512);
			s_out_n = s_out_i = 0;
			q(0xE5); // данные приняты (xxx0 0101)
			s_busy_after_out = true;
		}
		return;
	}
	if (s_cmd_n > 0 || (b & 0xC0) == 0x40) {
		s_cmd[s_cmd_n++] = b;
		if (s_cmd_n == 6) {
			s_cmd_n = 0;
			card_exec();
		}
	}
}

// Что карта выдаёт на DO в этом байте
static uint8_t card_out(void) {
	if (s_now_ns < s_busy_end_ns) {
		return 0x00; // занята записью во флеш
	}
	if (s_out_i < s_out_n) {
		uint8_t b = s_out[s_out_i++];
		if (s_out_i == s_out_n && s_busy_after_out) {
			s_busy_after_out = false;
			s_busy_end_ns = s_now_ns + (uint64_t) fake_sd_busy_us * 1000u;
		}
		return b;
	}
	return 0xFF;
}

void spi_set_speed(uint32_t prescaler) {
	uint32_t div = 2u << ((prescaler >> 3) & 7u); // BR[2:0]: /2../256 от 48 МГц
	s_byte_ns = 8000u * div / 48u;
}

uint8_t spi_xchg(uint8_t out) {
	advance(s_byte_ns);
	fake_spi_bytes++;
	switch (fake_miso) {
	case FAKE_MISO_00:
		return 0x00;
	case FAKE_MISO_FF:
		return 0xFF;
	case FAKE_MISO_NOISE:
		return noise();
	case FAKE_MISO_CARD:
	default:
		break;
	}
	if (!s_cs_low) {
		return 0xFF; // CS = 1: карта отпустила DO (подтяжка)
	}
	uint8_t in = card_out();
	card_in(out);
	return in;
}

void spi_tx_block(const uint8_t *buf, uint16_t n) {
	for (uint16_t i = 0; i < n; i++) {
		spi_xchg(buf[i]);
	}
}

void fake_sd_cs(bool low) {
	if (s_cs_low && !low) {
		// CS снят: незаконченный ответ и приём команды пропадают (занятость — нет)
		s_out_n = s_out_i = 0;
		s_cmd_n = 0;
		s_wr_token = s_wr_data = false;
		if (s_busy_after_out) {
			s_busy_after_out = false;
			s_busy_end_ns = s_now_ns + (uint64_t) fake_sd_busy_us * 1000u;
		}
	}
	s_cs_low = low;
}

void fake_sd_reset(fake_miso_t miso, uint32_t seed) {
	fake_miso = miso;
	s_seed = seed;
	s_now_ns = (uint64_t) fake_tick * 1000000u;
	s_byte_ns = 21333;
	s_cs_low = false;
	s_spi_mode = s_ready = s_app = false;
	s_cmd_n = 0;
	s_out_n = s_out_i = 0;
	s_busy_after_out = false;
	s_busy_end_ns = 0;
	s_wr_token = s_wr_data = false;
	fake_sd_acmd41_polls = 50;
	fake_sd_busy_us = 300;
	fake_sd_cmd0_junk = 0;
	fake_spi_bytes = 0;
}
