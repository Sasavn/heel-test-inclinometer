/*
 * fake_hal.c — имитация HAL, шины RS485 с датчиками BWM427 и FatFs в памяти.
 */
#include "fake.h"
#include "ff.h"
#include "fatfs_sd.h"
#include "bwm427.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GPIO_TypeDef fake_gpioa, fake_gpiob, fake_gpioc;
UART_HandleTypeDef huart1 = { .Init = { .BaudRate = 115200 }, .RxState = HAL_UART_STATE_READY };
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;

uint32_t fake_tick;
int fake_sw_rec;
uint32_t SystemCoreClock = 96000000u;

uint32_t HAL_GetTick(void) {
	return fake_tick;
}

void Error_Handler(void) {
	fprintf(stderr, "Error_Handler\n");
	exit(2);
}

/* ---------------- GPIO ---------------- */

static int s_de, s_re;

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state) {
	if (port == RS485_DE_GPIO_Port && pin == RS485_DE_Pin) {
		s_de = state;
	}
	if (port == RS485_RE_GPIO_Port && pin == RS485_RE_Pin) {
		s_re = state;
	}
	if (port == SD_CS_GPIO_Port && pin == SD_CS_Pin) {
		fake_sd_cs(state == GPIO_PIN_RESET);
	}
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin) {
	if (port == SW_RECORD_GPIO_Port && pin == SW_RECORD_Pin) {
		return fake_sw_rec ? GPIO_PIN_RESET : GPIO_PIN_SET;
	}
	return GPIO_PIN_SET;
}

/* ---------------- ADC: батарея fake_adc_bat (2000 — 6,5 В), VREFINT = калибровке ---------------- */

static uint32_t s_adc_ch;
uint32_t fake_adc_bat = 2000u;

// VDDA = 3,3 В (VREFINT = калибровке), делитель 4,03
uint32_t fake_adc_for_volts(float v) {
	float a = v / 4.03f / 3.3f * 4095.0f + 0.5f;
	return a >= 4095.0f ? 4095u : (uint32_t) a;
}

int fake_adc_eoc(void) {
	return 1;
}
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *h, ADC_ChannelConfTypeDef *c) {
	(void) h;
	s_adc_ch = c->Channel;
	return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h) {
	(void) h;
	return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h, uint32_t t) {
	(void) h;
	(void) t;
	return HAL_OK;
}
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h) {
	(void) h;
	return s_adc_ch == ADC_CHANNEL_VREFINT ? 1500u : fake_adc_bat;
}

/* ---------------- I2C: DS3231 нет ---------------- */

HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t a, uint32_t n, uint32_t t) {
	(void) h; (void) a; (void) n; (void) t;
	return HAL_ERROR;
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t a, uint16_t r, uint16_t rs,
		uint8_t *d, uint16_t n, uint32_t t) {
	(void) h; (void) a; (void) r; (void) rs; (void) d; (void) n; (void) t;
	return HAL_ERROR;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t a, uint16_t r, uint16_t rs,
		uint8_t *d, uint16_t n, uint32_t t) {
	(void) h; (void) a; (void) r; (void) rs; (void) d; (void) n; (void) t;
	return HAL_ERROR;
}

/* ---------------- Время, таймер шины (TIM5), DWT ---------------- */

fake_dwt_t fake_dwt;
uint32_t fake_us_offset;
bool fake_timer_dead;
static uint32_t s_sub_us;          // позиция внутри текущей миллисекунды, мкс
static bool s_alarm_on;
static uint32_t s_alarm_at;        // во времени fake_now_us()

uint32_t fake_now_us(void) {
	return fake_tick * 1000u + s_sub_us;
}

void bwm427_port_init(void) {
	s_alarm_on = false;
}

uint32_t bwm427_port_us(void) {
	return fake_timer_dead ? 12345u : fake_now_us() + fake_us_offset;
}

// Момент в прошлом сработает при ближайшей обработке событий — в то же время
// шины, как прерывание на МК сразу после разрешения
void bwm427_port_alarm(uint32_t at_us) {
	if (fake_timer_dead) {
		return;
	}
	s_alarm_at = at_us - fake_us_offset;
	s_alarm_on = true;
}

void bwm427_port_alarm_off(void) {
	s_alarm_on = false;
}

/* ---------------- UART + шина ---------------- */

#define CH_US(n) BWM427_CHARS_US(n)

fake_sensor_t fake_sensor[FAKE_SENSORS];
uint32_t fake_tx_us[FAKE_TX_LOG];
uint8_t fake_tx_addr[FAKE_TX_LOG];
uint32_t fake_rx_done_us[FAKE_TX_LOG];
uint32_t fake_tx_count;
bool fake_bus_violation;

static uint8_t s_tx[8];
static bool s_tx_busy;
static uint32_t s_tc_at;           // флаг TC: конец передачи
static uint8_t *s_rx_ptr;
static uint16_t s_rx_size, s_rx_cnt;
static bool s_rx_armed;
// Ответ в пути: байты и моменты конца каждого из них
static uint8_t s_reply[32];
static uint32_t s_byte_at[32];
static uint8_t s_reply_n, s_reply_i;
static bool s_idle_on;             // флаг IDLE: символ тишины после последнего байта
static uint32_t s_idle_at;
static uint32_t s_rand = 1;

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *h) {
	(void) h;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *h, const uint8_t *d, uint16_t n) {
	(void) h;
	if (s_tx_busy || n != 8) {
		return HAL_BUSY;
	}
	if (!s_de || !s_re || s_rx_armed) {
		fake_bus_violation = true;
	}
	memcpy(s_tx, d, 8);
	s_tx_busy = true;
	s_tc_at = fake_now_us() + CH_US(8);
	if (fake_tx_count < FAKE_TX_LOG) {
		fake_tx_us[fake_tx_count] = fake_now_us();
		fake_tx_addr[fake_tx_count] = d[0];
		fake_rx_done_us[fake_tx_count] = 0;
	}
	fake_tx_count++;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *h, uint8_t *d, uint16_t n) {
	if (s_rx_armed || n == 0) {
		return HAL_BUSY;
	}
	s_rx_ptr = d;
	s_rx_size = n;
	s_rx_cnt = 0;
	s_rx_armed = true;
	h->RxState = 0x22;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_AbortReceive_IT(UART_HandleTypeDef *h) {
	s_rx_armed = false;
	h->RxState = HAL_UART_STATE_READY;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_AbortTransmit_IT(UART_HandleTypeDef *h) {
	(void) h;
	s_tx_busy = false;
	return HAL_OK;
}

static void put_crc(uint8_t *b, uint16_t n) {
	uint16_t crc = bwm427_crc16(b, n);
	b[n] = (uint8_t) crc;
	b[n + 1] = (uint8_t) (crc >> 8);
}

// Поставить в очередь n байт ответа подряд, первый начинается в t0
static void queue_bytes(const uint8_t *b, uint16_t n, uint32_t t0) {
	for (uint16_t k = 0; k < n && s_reply_n < sizeof(s_reply); k++) {
		s_reply[s_reply_n] = b[k];
		s_byte_at[s_reply_n] = t0 + CH_US(k + 1);
		s_reply_n++;
	}
}

// Сформировать ответ датчика на s_tx (запрос закончился сейчас)
static void sensor_respond(void) {
	s_reply_n = 0;
	s_reply_i = 0;
	uint16_t crc = bwm427_crc16(s_tx, 6);
	if (s_tx[6] != (uint8_t) crc || s_tx[7] != (uint8_t) (crc >> 8)) {
		return;
	}
	fake_sensor_t *s = NULL;
	for (int i = 0; i < FAKE_SENSORS; i++) {
		if (fake_sensor[i].present && fake_sensor[i].addr == s_tx[0]) {
			s = &fake_sensor[i];
		}
	}
	if (!s) {
		return;
	}
	s->requests++;
	uint8_t r[16];
	uint16_t len = 0;
	uint16_t reg = (uint16_t) ((s_tx[2] << 8) | s_tx[3]);
	uint16_t val = (uint16_t) ((s_tx[4] << 8) | s_tx[5]);
	if (s->mode == FS_EXCEPTION) {
		r[0] = s->addr;
		r[1] = (uint8_t) (s_tx[1] | 0x80);
		r[2] = 0x02;
		put_crc(r, 3);
		len = 5;
	} else if (s_tx[1] == 0x03 && reg == 0x0001 && val == 2) {
		r[0] = s->addr;
		r[1] = 0x03;
		r[2] = 4;
		r[3] = (uint8_t) (s->reg_x >> 8);
		r[4] = (uint8_t) s->reg_x;
		r[5] = (uint8_t) (s->reg_y >> 8);
		r[6] = (uint8_t) s->reg_y;
		put_crc(r, 7);
		len = 9;
	} else if (s_tx[1] == 0x06) {
		memcpy(r, s_tx, 8);
		if (reg == 0x000D) {
			s->addr = (uint8_t) val;
			if (s->reply_from_new) {
				r[0] = s->addr;
				put_crc(r, 6);
			}
		} else if (reg == 0x000F) {
			s->saved = true;
		}
		len = 8;
	} else {
		return;
	}
	if (s->mode == FS_BAD_CRC) {
		r[len - 1] ^= 0x55;
	}
	uint32_t now = fake_now_us();
	uint32_t lat = s->latency_us;
	if (s->jitter_us) {
		s_rand = s_rand * 1103515245u + 12345u;
		lat += (s_rand >> 8) % (s->jitter_us + 1u);
	}
	uint32_t t = now + lat;
	if (s->mode == FS_GARBAGE_SPLIT) {
		// помеха 0x00 сразу после переключения, потом ответ двумя кусками
		// с паузой 1 мс посередине
		static const uint8_t junk = 0x00;
		queue_bytes(&junk, 1, now);
		queue_bytes(r, 4, t);
		queue_bytes(&r[4], (uint16_t) (len - 4), t + CH_US(4) + 1000u);
	} else {
		queue_bytes(r, len, t);
	}
}

// Байт ответа пришёл (конец его стоп-бита)
static void on_byte(void) {
	uint8_t b = s_reply[s_reply_i];
	uint32_t t = s_byte_at[s_reply_i];
	s_reply_i++;
	s_idle_on = true;
	s_idle_at = t + CH_US(1);
	if (!s_rx_armed) {
		return; // приём не включён — байт теряется
	}
	s_rx_ptr[s_rx_cnt++] = b;
	if (fake_tx_count && fake_tx_count <= FAKE_TX_LOG) {
		fake_rx_done_us[fake_tx_count - 1] = t;
	}
	if (s_rx_cnt >= s_rx_size) {
		s_rx_armed = false;
		huart1.RxState = HAL_UART_STATE_READY;
		HAL_UARTEx_RxEventCallback(&huart1, s_rx_cnt);
	}
}

// Линия молчит символ после последнего байта
static void on_idle(void) {
	s_idle_on = false;
	if (s_rx_armed && s_rx_cnt > 0) {
		s_rx_armed = false;
		huart1.RxState = HAL_UART_STATE_READY;
		HAL_UARTEx_RxEventCallback(&huart1, s_rx_cnt);
	}
}

// Выполнить события шины с моментом раньше end (по порядку; при равенстве —
// TC, байт, IDLE, таймер: прерывание USART1 приоритетнее TIM5)
static void bus_events_until(uint32_t end) {
	for (;;) {
		uint32_t now = fake_now_us();
		int which = -1;
		uint32_t best = 0;
		const bool on[4] = { s_tx_busy, s_reply_i < s_reply_n, s_idle_on, s_alarm_on };
		const uint32_t at[4] = { s_tc_at, on[1] ? s_byte_at[s_reply_i] : 0u, s_idle_at, s_alarm_at };
		for (int k = 0; k < 4; k++) {
			if (!on[k]) {
				continue;
			}
			int32_t d = (int32_t) (at[k] - now);
			uint32_t dd = d > 0 ? (uint32_t) d : 0u;
			if (which < 0 || dd < best) {
				which = k;
				best = dd;
			}
		}
		if (which < 0 || (int32_t) (now + best - end) >= 0) {
			return;
		}
		s_sub_us += best;
		switch (which) {
		case 0:
			s_tx_busy = false;
			sensor_respond();
			HAL_UART_TxCpltCallback(&huart1);
			break;
		case 1:
			on_byte();
			break;
		case 2:
			on_idle();
			break;
		default:
			s_alarm_on = false;
			bwm427_alarm_isr();
			break;
		}
	}
}

void fake_ms(void) {
	bus_events_until(fake_tick * 1000u + 1000u);
	fake_tick++;
	s_sub_us = 0;
	bwm427_tick_1ms(); // SysTick
}

/* ---------------- SD-карта и FatFs в памяти ---------------- */

#define FF_FILES 64
typedef struct {
	bool exists;
	char name[40];
	char *data;
	uint32_t len, synced, cap;
	bool partial_pending;   // последний f_write был не кратен 512
} ffile_t;

static ffile_t s_ff[FF_FILES];
static FATFS *s_mounted;
bool fake_card_present = true;
uint32_t fake_fwrite_calls, fake_fwrite_bad_size, fake_fsync_calls;

void fake_fs_format(void) {
	for (int i = 0; i < FF_FILES; i++) {
		free(s_ff[i].data);
		memset(&s_ff[i], 0, sizeof(s_ff[i]));
	}
}

void fake_fs_add(const char *name) {
	for (int i = 0; i < FF_FILES; i++) {
		if (!s_ff[i].exists) {
			s_ff[i].exists = true;
			snprintf(s_ff[i].name, sizeof(s_ff[i].name), "%s", name);
			return;
		}
	}
}

static int ff_find(const char *name) {
	for (int i = 0; i < FF_FILES; i++) {
		if (s_ff[i].exists && strcmp(s_ff[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

const char* fake_fs_get(const char *name, uint32_t *len) {
	int i = ff_find(name);
	if (i < 0) {
		return NULL;
	}
	*len = s_ff[i].len;
	return s_ff[i].data ? s_ff[i].data : "";
}

int fake_fs_count(void) {
	int n = 0;
	for (int i = 0; i < FF_FILES; i++) {
		n += s_ff[i].exists;
	}
	return n;
}

void fake_card_pull(void) {
	fake_card_present = false;
	for (int i = 0; i < FF_FILES; i++) {
		s_ff[i].len = s_ff[i].synced; // всё, что не дошло до f_sync, потеряно
	}
}

void fake_card_insert(void) {
	fake_card_present = true;
}

DSTATUS SD_disk_initialize(BYTE pdrv) {
	(void) pdrv;
	return fake_card_present ? 0 : STA_NOINIT;
}

DRESULT SD_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count) {
	(void) pdrv; (void) sector;
	if (!fake_card_present) {
		return RES_ERROR;
	}
	memset(buff, 0, 512u * count);
	return RES_OK;
}

DRESULT SD_disk_check(BYTE pdrv, BYTE *buff) {
	return SD_disk_read(pdrv, buff, 0, 1);
}

FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt) {
	(void) path;
	s_mounted = NULL;
	if (!fs) {
		return FR_OK;
	}
	if (opt == 1 && !fake_card_present) {
		return FR_NOT_READY;
	}
	s_mounted = fs;
	return FR_OK;
}

static ffile_t* ff_of(FIL *fp) {
	if (!s_mounted || !fake_card_present || fp->obj.id >= FF_FILES) {
		return NULL;
	}
	return &s_ff[fp->obj.id];
}

FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode) {
	if (!s_mounted) {
		return FR_NOT_ENABLED;
	}
	if (!fake_card_present) {
		return FR_DISK_ERR;
	}
	if (ff_find(path) >= 0 && (mode & FA_CREATE_NEW)) {
		return FR_EXIST;
	}
	for (int i = 0; i < FF_FILES; i++) {
		if (!s_ff[i].exists) {
			memset(&s_ff[i], 0, sizeof(s_ff[i]));
			s_ff[i].exists = true;
			snprintf(s_ff[i].name, sizeof(s_ff[i].name), "%s", path);
			fp->obj.id = (WORD) i;
			return FR_OK;
		}
	}
	return FR_DENIED;
}

FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw) {
	ffile_t *f = ff_of(fp);
	*bw = 0;
	if (!f) {
		return FR_DISK_ERR;
	}
	fake_fwrite_calls++;
	if (f->partial_pending) {
		fake_fwrite_bad_size++; // запись после «хвоста» без f_sync между ними
	}
	f->partial_pending = (btw % 512) != 0;
	if (f->len + btw > f->cap) {
		f->cap = (f->len + btw) * 2 + 1024;
		f->data = realloc(f->data, f->cap);
	}
	memcpy(&f->data[f->len], buff, btw);
	f->len += btw;
	*bw = btw;
	return FR_OK;
}

FRESULT f_sync(FIL *fp) {
	ffile_t *f = ff_of(fp);
	if (!f) {
		return FR_DISK_ERR;
	}
	fake_fsync_calls++;
	f->partial_pending = false;
	f->synced = f->len;
	return FR_OK;
}

FRESULT f_close(FIL *fp) {
	FRESULT r = f_sync(fp);
	fp->obj.id = 0xFFFF;
	return r;
}

FRESULT f_opendir(DIR *dp, const TCHAR *path) {
	(void) path;
	if (!s_mounted || !fake_card_present) {
		return FR_DISK_ERR;
	}
	dp->dptr = 0;
	return FR_OK;
}

FRESULT f_readdir(DIR *dp, FILINFO *fno) {
	while (dp->dptr < FF_FILES && !s_ff[dp->dptr].exists) {
		dp->dptr++;
	}
	if (dp->dptr >= FF_FILES) {
		fno->fname[0] = '\0';
		return FR_OK;
	}
	snprintf(fno->fname, sizeof(fno->fname), "%s", s_ff[dp->dptr].name);
	fno->fattrib = AM_ARC;
	dp->dptr++;
	return FR_OK;
}

FRESULT f_closedir(DIR *dp) {
	(void) dp;
	return FR_OK;
}

/* ---------------- Флеш: сектор настроек ---------------- */

uint32_t fake_flash[SETTINGS_FLASH_SIZE / 4];
uint32_t fake_flash_erases, fake_flash_words;
int32_t fake_flash_cut = -1;

void fake_flash_reset(void) {
	memset(fake_flash, 0xFF, sizeof(fake_flash));
	fake_flash_erases = 0;
	fake_flash_words = 0;
	fake_flash_cut = -1;
}

const uint32_t* settings_flash_words(void) {
	return fake_flash;
}

bool settings_flash_erase(void) {
	if (fake_flash_cut == 0) {
		return false; // питания нет
	}
	memset(fake_flash, 0xFF, sizeof(fake_flash));
	fake_flash_erases++;
	return true;
}

// NOR: запись только сбрасывает биты (как у STM32F4 без проверки «стёрто»)
bool settings_flash_write(uint32_t offset, const uint32_t *words, uint32_t n) {
	if (offset % 4u || offset + 4u * n > SETTINGS_FLASH_SIZE) {
		return false;
	}
	for (uint32_t k = 0; k < n; k++) {
		if (fake_flash_cut == 0) {
			return false;
		}
		if (fake_flash_cut > 0) {
			fake_flash_cut--;
		}
		fake_flash[offset / 4u + k] &= words[k];
		fake_flash_words++;
	}
	return true;
}

void fake_reset(void) {
	fake_tick = 0;
	s_sub_us = 0;
	fake_us_offset = 0;
	fake_timer_dead = false;
	s_alarm_on = false;
	fake_sw_rec = 0;
	fake_adc_bat = 2000u;
	memset(fake_sensor, 0, sizeof(fake_sensor));
	fake_tx_count = 0;
	fake_bus_violation = false;
	s_tx_busy = false;
	s_rx_armed = false;
	s_reply_n = 0;
	s_reply_i = 0;
	s_idle_on = false;
	huart1.RxState = HAL_UART_STATE_READY;
	fake_card_present = true;
	fake_fwrite_calls = fake_fwrite_bad_size = fake_fsync_calls = 0;
	fake_fs_format();
	fake_flash_reset();
}
