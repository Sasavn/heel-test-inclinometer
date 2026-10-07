/*
 * fake.h — управление «железом» в host-тестах: время, тумблер, шина RS485
 * с имитацией датчиков BWM427, SD-карта в памяти (уровень FatFs), шина SPI
 * с SD-картой (для настоящего драйвера fatfs_sd.c), флеш-сектор настроек.
 */
#ifndef FAKE_H_
#define FAKE_H_

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "diskio.h"
#include "settings.h"

extern UART_HandleTypeDef huart1;
extern uint32_t fake_tick;
extern int fake_sw_rec;           // 1 = тумблер в REC

// --- Датчики на шине ---
typedef enum {
	FS_NORMAL = 0,
	FS_EXCEPTION,       // отвечает исключением 0x83/02
	FS_BAD_CRC,         // портит CRC
	FS_GARBAGE_SPLIT,   // байт-помеха, затем ответ двумя кусками с паузой
} fake_mode_t;

typedef struct {
	bool present;
	uint8_t addr;
	uint16_t reg_x, reg_y;
	fake_mode_t mode;
	uint32_t latency_us;    // от конца запроса до начала первого байта ответа
	uint32_t jitter_us;     // + случайно 0..jitter_us к задержке каждого ответа
	bool reply_from_new;    // при смене адреса эхо уже с нового адреса
	bool saved;             // была команда сохранения
	uint32_t requests;      // запросов, адресованных этому датчику
} fake_sensor_t;

#define FAKE_SENSORS 4
extern fake_sensor_t fake_sensor[FAKE_SENSORS];

// Время шины, мкс: fake_tick * 1000 + позиция внутри миллисекунды. Счётчик
// таймера шины (bwm427_port_us) = это время + fake_us_offset.
uint32_t fake_now_us(void);
extern uint32_t fake_us_offset;   // сдвиг счётчика (проверка переполнения 32 бит)
extern bool fake_timer_dead;      // TIM5 не работает: счётчик стоит, прерываний нет

// Журнал передач (для проверки пауз), время — fake_now_us()
#define FAKE_TX_LOG 4096
extern uint32_t fake_tx_us[FAKE_TX_LOG];      // начало передачи запроса
extern uint8_t fake_tx_addr[FAKE_TX_LOG];
extern uint32_t fake_tx_count;
extern uint32_t fake_rx_done_us[FAKE_TX_LOG]; // конец последнего принятого байта ответа (0 — не было)
extern bool fake_bus_violation;               // старт передачи при активном приёме / DE

void fake_reset(void);
// Прошла миллисекунда: события шины и таймера TIM5 внутри неё по порядку
// времени (TC, байты ответа, IDLE, сравнение таймера), затем SysTick:
// fake_tick + 1 и bwm427_tick_1ms(). Суперцикл тесты вызывают после.
void fake_ms(void);

// --- АЦП ---
extern uint32_t fake_adc_bat;     // отсчёт канала батареи (VREFINT = калибровке)
uint32_t fake_adc_for_volts(float v); // отсчёт для напряжения АКБ v (делитель 4,03, 3,3 В)

// --- SD-карта ---
extern bool fake_card_present;
extern uint32_t fake_fwrite_calls;
extern uint32_t fake_fwrite_bad_size;  // вызовы f_write не кратные 512 (кроме дописи перед f_sync)
extern uint32_t fake_fsync_calls;
void fake_card_pull(void);             // вынуть: несинхронизированное пропадает
void fake_card_insert(void);
void fake_fs_format(void);
void fake_fs_add(const char *name);    // создать пустой файл
const char* fake_fs_get(const char *name, uint32_t *len); // NULL — нет файла
int fake_fs_count(void);

// --- Шина SPI и SD-карта для настоящего драйвера (fake_sd.c) ---
typedef enum {
	FAKE_MISO_CARD = 0,   // вставлена карта
	FAKE_MISO_00,         // карты нет, MISO читается как 0x00
	FAKE_MISO_FF,         // карты нет, MISO читается как 0xFF (подтяжка)
	FAKE_MISO_NOISE,      // карты нет, на MISO шум
} fake_miso_t;
#define FAKE_SD_SECTORS 16
extern fake_miso_t fake_miso;            // можно менять на ходу: «вынули карту»
extern uint16_t fake_sd_acmd41_polls;    // столько ACMD41 карта ещё в idle
extern uint32_t fake_sd_busy_us;         // занятость после записи блока
extern uint8_t fake_sd_cmd0_junk;        // столько первых CMD0 — ответ «не idle»
extern uint32_t fake_spi_bytes;
extern uint8_t fake_sd_mem[FAKE_SD_SECTORS][512];
void fake_sd_reset(fake_miso_t miso, uint32_t seed); // карта без питания, время = fake_tick
void fake_sd_cs(bool low);
uint64_t fake_time_us(void);             // время шины SPI с точностью до байта

// Настоящий драйвер (sd_drv.c)
DSTATUS drv_disk_initialize(BYTE pdrv);
DSTATUS drv_disk_status(BYTE pdrv);
DRESULT drv_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
DRESULT drv_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
DRESULT drv_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);
DRESULT drv_disk_check(BYTE pdrv, BYTE *buff);

// --- Флеш: сектор настроек (settings.c собран с -DSETTINGS_FLASH_FAKE) ---
extern uint32_t fake_flash[SETTINGS_FLASH_SIZE / 4];
extern uint32_t fake_flash_erases;
extern uint32_t fake_flash_words;        // записано слов
extern int32_t fake_flash_cut;           // >= 0: столько слов запишется, дальше «пропало питание»
void fake_flash_reset(void);             // стёртый сектор, счётчики в 0

#endif /* FAKE_H_ */
