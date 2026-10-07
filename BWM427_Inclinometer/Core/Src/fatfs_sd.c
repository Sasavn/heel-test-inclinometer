/*
 * fatfs_sd.c — драйвер SD-карты в режиме SPI (SPI2, CS = PA4) для FatFs.
 *
 * Поддерживаются SDv1, SDv2 SDSC (байтовая адресация, CMD16 = 512) и
 * SDHC/SDXC (блочная адресация, бит CCS в OCR). Инициализация на 375 кГц
 * (спецификация требует <= 400 кГц), обмен на 12 МГц.
 *
 * Ожидания. Без карты многие модули читают MISO как 0x00, а не 0xFF, и
 * ожидание готовности карты (0xFF на DO) тянулось бы весь таймаут — раньше
 * 500 мс на КАЖДУЮ команду, т. е. секунды на попытку инициализации каждые
 * 2 с. Поэтому:
 *  - при инициализации и проверке наличия карты (SD_disk_check) готовность
 *    ждём SD_PROBE_READY_MS: карта, которая ничего не пишет во флеш, отпускает
 *    DO сразу;
 *  - инициализация бросается, как только CMD0 остался без ответа, а ответы
 *    не по протоколу (шум на линии) обрывают её на первом же шаге;
 *  - долгие ожидания (занятость после записи блока — до 250..500 мс) — только
 *    для чтения/записи данных уже инициализированной карты;
 *  - любая ошибка обмена снимает признак инициализации (STA_NOINIT): дальше
 *    все функции возвращаются сразу, пока sd_logger.c не инициализирует карту
 *    заново. Выдернутая карта стоит не больше одного таймаута.
 * Без карты попытка инициализации занимает: MISO = 0xFF — ~0,7 мс, MISO = 0x00 —
 * ~3 мс, шум — обычно до ~9 мс (host-тест tests/host: «драйвер SD»).
 *
 * Байты гоняются через регистры SPI2 напрямую: вызов HAL на каждый байт
 * в несколько раз медленнее самой передачи.
 */
#include "fatfs_sd.h"
#include <stdbool.h>

#define SD_CS_LOW()  HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET)
#define SD_CS_HIGH() HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET)

// Скорость SPI2 (APB1 = 48 МГц)
#define SD_SPI_SLOW          SPI_BAUDRATEPRESCALER_128 // 375 кГц: инициализация
#define SD_SPI_FAST          SPI_BAUDRATEPRESCALER_4   // 12 МГц: обмен (/2 = 24 МГц тоже в пределах SD)

// Ограничения ожиданий
#define SD_INIT_TIMEOUT_MS   1000   // выход карты из idle (ACMD41), по спецификации до 1 с
#define SD_READY_TIMEOUT_MS  500    // занятость после записи блока (SDHC до 250 мс, SDXC до 500 мс)
#define SD_TOKEN_TIMEOUT_MS  200    // маркер начала данных при чтении (по спецификации до 100 мс)
#define SD_PROBE_READY_MS    3      // готовность при инициализации и проверке наличия (реально 2..3 мс)
#define SD_PROBE_TOKEN_MS    5      // маркер данных при проверке наличия
#define SD_CMD0_TRIES        3      // попыток перевести карту в idle, если она отвечает не тем...
#define SD_CMD0_BUDGET_MS    5      // ...пока на них ушло меньше (шум на MISO: не дольше ~9 мс)
#define SD_R1_POLLS          10     // ответ R1 приходит не позже 8 байт после команды
#define SD_SPI_GUARD         100000u // защита от зависания SPI (итераций ожидания флага)

// Ответ R1
#define R1_IDLE              0x01   // карта в idle (идёт инициализация)
#define R1_ILLEGAL_IDLE      0x05   // idle + «неизвестная команда» (SDv1 на CMD8)
#define R1_NONE              0xFF   // нет ответа

// Команды SD (ACMD — с префиксом CMD55)
#define CMD0     (0)    // GO_IDLE_STATE
#define CMD8     (8)    // SEND_IF_COND
#define CMD16    (16)   // SET_BLOCKLEN
#define CMD17    (17)   // READ_SINGLE_BLOCK
#define CMD24    (24)   // WRITE_BLOCK
#define CMD55    (55)   // APP_CMD
#define CMD58    (58)   // READ_OCR
#define ACMD41   (0x80 + 41) // SD_SEND_OP_COND

// Тип карты
#define CT_SD1   0x01   // SD версии 1
#define CT_SD2   0x02   // SD версии 2+
#define CT_BLOCK 0x04   // адресация блоками (SDHC/SDXC)

static volatile DSTATUS s_stat = STA_NOINIT;
static uint8_t s_type;
// Текущие ограничения ожиданий: короткие (проба) или для обмена данными
static uint32_t s_ready_ms = SD_PROBE_READY_MS;
static uint32_t s_token_ms = SD_PROBE_TOKEN_MS;

/* ------------------------------------------------------------------------ */
/* SPI                                                                      */
/* ------------------------------------------------------------------------ */

#ifndef SD_SPI_FAKE

extern SPI_HandleTypeDef hspi2;

// Сменить скорость SPI2 (и включить его — HAL оставляет SPE выключенным)
static void spi_set_speed(uint32_t prescaler) {
	SPI_TypeDef *spi = hspi2.Instance;
	spi->CR1 &= ~SPI_CR1_SPE;
	MODIFY_REG(spi->CR1, SPI_CR1_BR, prescaler);
	hspi2.Init.BaudRatePrescaler = prescaler;
	spi->CR1 |= SPI_CR1_SPE;
	(void) spi->DR; // сбросить RXNE/OVR от прошлых обменов
	(void) spi->SR;
}

// Обменяться одним байтом. При зависании SPI возвращает 0xFF ("нет ответа").
static uint8_t spi_xchg(uint8_t out) {
	SPI_TypeDef *spi = hspi2.Instance;
	uint32_t guard = SD_SPI_GUARD;
	while (!(spi->SR & SPI_SR_TXE)) {
		if (--guard == 0) {
			return 0xFF;
		}
	}
	*(__IO uint8_t*) &spi->DR = out;
	guard = SD_SPI_GUARD;
	while (!(spi->SR & SPI_SR_RXNE)) {
		if (--guard == 0) {
			return 0xFF;
		}
	}
	return *(__IO uint8_t*) &spi->DR;
}

// Передать блок без ожидания каждого принятого байта (они не нужны)
static void spi_tx_block(const uint8_t *buf, uint16_t n) {
	SPI_TypeDef *spi = hspi2.Instance;
	uint32_t guard;
	for (uint16_t i = 0; i < n; i++) {
		guard = SD_SPI_GUARD;
		while (!(spi->SR & SPI_SR_TXE) && --guard) {
		}
		*(__IO uint8_t*) &spi->DR = buf[i];
	}
	guard = SD_SPI_GUARD;
	while (!(spi->SR & SPI_SR_TXE) && --guard) {
	}
	guard = SD_SPI_GUARD;
	while ((spi->SR & SPI_SR_BSY) && --guard) {
	}
	(void) spi->DR; // чтение DR, затем SR сбрасывает OVR
	(void) spi->SR;
}

#else

// host-тесты (tests/host/fake_sd.c): шина SPI и карта — имитация, время
// идёт по байтам на текущей скорости
void spi_set_speed(uint32_t prescaler);
uint8_t spi_xchg(uint8_t out);
void spi_tx_block(const uint8_t *buf, uint16_t n);

#endif

// Принять блок (на MOSI — 0xFF, как требует карта)
static void spi_rx_block(uint8_t *buf, uint16_t n) {
	for (uint16_t i = 0; i < n; i++) {
		buf[i] = spi_xchg(0xFF);
	}
}

/* ------------------------------------------------------------------------ */
/* Протокол SD                                                              */
/* ------------------------------------------------------------------------ */

// Короткие ожидания (инициализация, проверка наличия) или для обмена данными
static void sd_timeouts(bool probe) {
	s_ready_ms = probe ? SD_PROBE_READY_MS : SD_READY_TIMEOUT_MS;
	s_token_ms = probe ? SD_PROBE_TOKEN_MS : SD_TOKEN_TIMEOUT_MS;
}

// Дождаться, пока карта отпустит DO (0xFF = свободна)
static int sd_wait_ready(uint32_t timeout_ms) {
	uint32_t start = HAL_GetTick();
	do {
		if (spi_xchg(0xFF) == 0xFF) {
			return 1;
		}
	} while (HAL_GetTick() - start < timeout_ms);
	return 0;
}

// Снять выбор карты; лишний байт — чтобы карта освободила DO
static void sd_deselect(void) {
	SD_CS_HIGH();
	spi_xchg(0xFF);
}

// Выбрать карту и дождаться готовности (s_ready_ms)
static int sd_select(void) {
	SD_CS_LOW();
	spi_xchg(0xFF);
	if (sd_wait_ready(s_ready_ms)) {
		return 1;
	}
	sd_deselect();
	return 0;
}

// Отправить команду, вернуть ответ R1 (R1_NONE — нет ответа или карта не
// освободилась). CS остаётся низким.
static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg) {
	uint8_t res;
	if (cmd & 0x80) { // ACMD<n> = CMD55 + CMD<n>
		cmd &= 0x7F;
		res = sd_send_cmd(CMD55, 0);
		if (res > 1) {
			return res;
		}
	}
	sd_deselect();
	if (!sd_select()) {
		return R1_NONE;
	}
	spi_xchg((uint8_t) (0x40 | cmd));
	spi_xchg((uint8_t) (arg >> 24));
	spi_xchg((uint8_t) (arg >> 16));
	spi_xchg((uint8_t) (arg >> 8));
	spi_xchg((uint8_t) arg);
	uint8_t crc = 0x01; // в режиме SPI CRC проверяется только у CMD0 и CMD8
	if (cmd == CMD0) {
		crc = 0x95;
	} else if (cmd == CMD8) {
		crc = 0x87;
	}
	spi_xchg(crc);
	uint8_t n = SD_R1_POLLS;
	do {
		res = spi_xchg(0xFF);
	} while ((res & 0x80) && --n);
	return res;
}

// Принять блок данных 512 байт после команды чтения
static int sd_rx_datablock(uint8_t *buf) {
	uint32_t start = HAL_GetTick();
	uint8_t token;
	do {
		token = spi_xchg(0xFF);
	} while (token == 0xFF && HAL_GetTick() - start < s_token_ms);
	if (token != 0xFE) {
		return 0; // таймаут или маркер ошибки
	}
	spi_rx_block(buf, 512);
	spi_xchg(0xFF); // CRC не проверяем
	spi_xchg(0xFF);
	return 1;
}

// Передать блок данных 512 байт после команды записи
static int sd_tx_datablock(const uint8_t *buf) {
	if (!sd_wait_ready(s_ready_ms)) {
		return 0;
	}
	spi_xchg(0xFE); // маркер начала данных
	spi_tx_block(buf, 512);
	spi_xchg(0xFF); // фиктивный CRC
	spi_xchg(0xFF);
	// Ответ данных xxx0 0101 = принято; запись во флеш карта делает уже
	// после — её ждёт следующий sd_select()
	return (spi_xchg(0xFF) & 0x1F) == 0x05;
}

// Ошибка обмена: карту вынули или она сбоит. До повторной инициализации все
// функции возвращаются сразу (а не ждут таймаутов на каждом вызове FatFs).
static void sd_fail(void) {
	s_stat |= STA_NOINIT;
}

// Ждать выхода из idle по ACMD41 (arg — флаг HCS). Любой ответ, кроме «ещё
// idle» и «готова», — не карта (шум на линии): сразу выход.
static uint8_t sd_wait_op_cond(uint32_t arg, uint32_t start) {
	uint8_t r;
	do {
		r = sd_send_cmd(ACMD41, arg);
	} while (r == R1_IDLE && HAL_GetTick() - start < SD_INIT_TIMEOUT_MS);
	return r;
}

/* ------------------------------------------------------------------------ */
/* Интерфейс для FatFs                                                      */
/* ------------------------------------------------------------------------ */

DSTATUS SD_disk_initialize(BYTE pdrv) {
	uint8_t ocr[4];
	if (pdrv) {
		return STA_NOINIT;
	}
	s_stat = STA_NOINIT;
	s_type = 0;
	sd_timeouts(true);

	spi_set_speed(SD_SPI_SLOW);
	SD_CS_HIGH();
	for (uint8_t n = 0; n < 10; n++) { // >= 74 тактов при CS = 1: карта переходит в рабочий режим
		spi_xchg(0xFF);
	}

	// CMD0. Нет ответа (0xFF, в том числе «DO не освободился» при MISO = 0x00)
	// — карты нет, выходим сразу. Повторять стоит, только если кто-то ответил,
	// но не «idle»: карта осталась посреди обмена после сброса МК.
	uint8_t r = R1_NONE;
	uint32_t t0 = HAL_GetTick();
	for (uint8_t t = 0; t < SD_CMD0_TRIES && HAL_GetTick() - t0 < SD_CMD0_BUDGET_MS; t++) {
		r = sd_send_cmd(CMD0, 0);
		if (r == R1_IDLE || r == R1_NONE) {
			break;
		}
	}

	uint8_t type = 0;
	if (r == R1_IDLE) {
		uint32_t start = HAL_GetTick();
		r = sd_send_cmd(CMD8, 0x1AA);
		if (r == R1_IDLE) {
			// SDv2: проверить эхо 0x1AA (карта работает при 2,7-3,6 В)
			for (uint8_t n = 0; n < 4; n++) {
				ocr[n] = spi_xchg(0xFF);
			}
			if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
				// Ждать выхода из idle с флагом HCS (поддерживаем SDHC/SDXC)
				if (sd_wait_op_cond(1UL << 30, start) == 0 && sd_send_cmd(CMD58, 0) == 0) {
					for (uint8_t n = 0; n < 4; n++) {
						ocr[n] = spi_xchg(0xFF);
					}
					// CCS = 1 — SDHC/SDXC, адрес в блоках; иначе SDSC, адрес в байтах
					type = (ocr[0] & 0x40) ? (CT_SD2 | CT_BLOCK) : CT_SD2;
				}
			}
		} else if (r == R1_ILLEGAL_IDLE) {
			// SDv1: CMD8 не поддерживается
			if (sd_wait_op_cond(0, start) == 0) {
				type = CT_SD1;
			}
		}
		// Карты с байтовой адресацией: явно задать блок 512 байт
		if (type && !(type & CT_BLOCK) && sd_send_cmd(CMD16, 512) != 0) {
			type = 0;
		}
	}
	sd_deselect();

	if (type) {
		s_type = type;
		s_stat &= (DSTATUS) ~STA_NOINIT;
		spi_set_speed(SD_SPI_FAST);
		sd_timeouts(false);
	}
	return s_stat;
}

DSTATUS SD_disk_status(BYTE pdrv) {
	return pdrv ? STA_NOINIT : s_stat;
}

DRESULT SD_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count) {
	if (pdrv || !count) {
		return RES_PARERR;
	}
	if (s_stat & STA_NOINIT) {
		return RES_NOTRDY;
	}
	DRESULT res = RES_OK;
	for (UINT c = 0; c < count; c++) {
		DWORD addr = sector + c;
		if (!(s_type & CT_BLOCK)) {
			addr *= 512; // SDSC: адрес в байтах
		}
		if (sd_send_cmd(CMD17, addr) != 0 || !sd_rx_datablock(&buff[c * 512])) {
			res = RES_ERROR;
			sd_fail();
			break;
		}
	}
	sd_deselect();
	return res;
}

DRESULT SD_disk_check(BYTE pdrv, BYTE *buff) {
	// Карта не пишет (файлы закрыты, f_sync дождался конца записи) — DO должен
	// освободиться сразу, долгие ожидания не нужны
	sd_timeouts(true);
	DRESULT res = SD_disk_read(pdrv, buff, 0, 1);
	sd_timeouts(false);
	return res;
}

DRESULT SD_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count) {
	if (pdrv || !count) {
		return RES_PARERR;
	}
	if (s_stat & STA_NOINIT) {
		return RES_NOTRDY;
	}
	DRESULT res = RES_OK;
	for (UINT c = 0; c < count; c++) {
		DWORD addr = sector + c;
		if (!(s_type & CT_BLOCK)) {
			addr *= 512;
		}
		if (sd_send_cmd(CMD24, addr) != 0 || !sd_tx_datablock(&buff[c * 512])) {
			res = RES_ERROR;
			sd_fail();
			break;
		}
	}
	sd_deselect();
	return res;
}

DRESULT SD_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
	if (pdrv) {
		return RES_PARERR;
	}
	if (s_stat & STA_NOINIT) {
		return RES_NOTRDY;
	}
	DRESULT res = RES_PARERR;
	switch (cmd) {
	case CTRL_SYNC:
		// Дождаться окончания внутренней записи карты
		res = sd_select() ? RES_OK : RES_ERROR;
		sd_deselect();
		if (res != RES_OK) {
			sd_fail();
		}
		break;
	case GET_SECTOR_SIZE:
		*(WORD*) buff = 512;
		res = RES_OK;
		break;
	default:
		break;
	}
	return res;
}
