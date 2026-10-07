/*
 * settings.h — настройки прибора во внутренней флеш-памяти (сектор 7).
 *
 * Хранятся частота опроса/записи, α фильтра EMA, пауза шины RS485, тема
 * интерфейса, порог тревоги АКБ и параметры качки (окно, отсчёты/с, порог
 * покоя, гистерезис). Нули датчиков (SET ZERO) НЕ хранятся: их задают на каждом замере.
 *
 * Журнал. Сектор 7 (0x08060000, 128 КБ) поделён на записи по 32 байта
 * (settings_rec_t). Каждое сохранение дописывает новую запись в первое чистое
 * место (словами по 32 бита, HAL_FLASH_Program); при включении берётся
 * правильная запись (magic + CRC-32) с наибольшим номером seq, иначе — значения
 * по умолчанию. Запись, оборванная выключением питания, не проходит CRC и
 * пропускается. Сектор стирается, только когда заполнен: 4096 сохранений на
 * одно стирание (ресурс флеша — 10 000 стираний).
 *
 * Сектор 7 исключён из области FLASH в STM32F411CEUX_FLASH.ld и _RAM.ld
 * (384 КБ вместо 512), поэтому код туда попасть не может. dfu-util (DfuSe)
 * стирает только сектора, которые занимает образ, так что настройки
 * переживают обновление прошивки (кроме mass-erase / полного стирания чипа
 * программатором).
 *
 * Запись во флеш останавливает процессор: слово ~16 мкс, стирание сектора
 * 1-2 с. Поэтому app.c сохраняет лениво — через SETTINGS_SAVE_DELAY_MS после
 * последнего изменения и никогда во время записи замера на SD.
 */
#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <stdint.h>
#include <stdbool.h>

#define SETTINGS_FLASH_ADDR     0x08060000u  // сектор 7
#define SETTINGS_FLASH_SIZE     0x20000u     // 128 КБ
#define SETTINGS_REC_SIZE       32u          // байт на запись
#define SETTINGS_REC_WORDS      (SETTINGS_REC_SIZE / 4u)
#define SETTINGS_CAPACITY       (SETTINGS_FLASH_SIZE / SETTINGS_REC_SIZE) // 4096 записей
#define SETTINGS_MAGIC          0x31544553u  // "SET1": формат записи версии 1
#define SETTINGS_SAVE_DELAY_MS  3000u        // сохранять через 3 с после последнего изменения

// Хранимые значения
typedef struct {
	uint16_t log_freq_hz;
	float ema_alpha;
	uint8_t bus_gap_ms;
	uint8_t theme;
	float bat_alarm_v;       // NaN — в записи поля нет (старая прошивка): порог по умолчанию
	// Качка; 0xFF / 0xFFFF — в записи поля нет (старая прошивка): по умолчанию
	uint8_t roll_window_s;
	uint8_t roll_rate_hz;
	uint16_t roll_calm_cdeg; // порог покоя, сотые градуса
	uint16_t roll_hyst_cdeg; // гистерезис, сотые градуса
} settings_t;

// Запись журнала во флеше (32 байта, слова по 4 байта)
typedef struct {
	uint32_t magic;          // SETTINGS_MAGIC
	uint32_t seq;            // номер сохранения, растёт (в т. ч. через стирание)
	uint16_t log_freq_hz;
	uint8_t bus_gap_ms;
	uint8_t theme;
	float ema_alpha;
	float bat_alarm_v;       // порог тревоги АКБ; в записях старых прошивок здесь
	                         // 0xFFFFFFFF (резерв) — читается как NaN
	uint8_t roll_window_s;   // качка: окно, с      } в записях старых прошивок
	uint8_t roll_rate_hz;    // качка: отсчётов/с   } здесь был резерв 0xFF...:
	uint16_t roll_calm_cdeg; // качка: порог, 0,01° } читается как «нет поля»
	uint16_t roll_hyst_cdeg; // качка: гистерезис, 0,01°
	uint16_t reserved;       // 0xFFFF: место под будущие поля
	uint32_t crc;            // CRC-32 предыдущих 28 байт
} settings_rec_t;

_Static_assert(sizeof(settings_rec_t) == SETTINGS_REC_SIZE, "settings_rec_t: 32 байта");

typedef enum {
	SETTINGS_ERR_NONE = 0,
	SETTINGS_ERR_ERASE,      // не стёрся сектор
	SETTINGS_ERR_WRITE,      // запись не прочиталась обратно (несколько попыток)
} settings_err_t;

// Состояние хранилища (для диагностики, usb_cli diag)
typedef struct {
	bool loaded;             // при включении найдена запись (иначе — значения по умолчанию)
	bool pending;            // есть изменения, ещё не записанные во флеш
	uint16_t used;           // занято мест в секторе (с битыми записями)
	uint32_t seq;            // номер последней записи (0 — записей нет)
	uint32_t saves;          // записей во флеш с момента включения
	uint32_t erases;         // стираний сектора с момента включения
	uint8_t last_err;        // settings_err_t последней неудачи
} settings_info_t;

// Найти последнюю правильную запись. false — записей нет (out не тронут).
bool settings_load(settings_t *out);

// Записать значения, если они отличаются от последней записи (стирает сектор,
// когда он заполнен). Блокирует: ~0,2 мс, при стирании 1-2 с. Снимает pending.
bool settings_store(const settings_t *s);

// Значения изменились: сохранить через SETTINGS_SAVE_DELAY_MS
void settings_touch(uint32_t now);

// Пора сохранять: есть изменения и после последнего прошло SETTINGS_SAVE_DELAY_MS
bool settings_due(uint32_t now);

const settings_info_t* settings_get_info(void);

// --- Для host-тестов ---

// CRC-32 (IEEE 802.3, полином 0xEDB88320, как у zip)
uint32_t settings_crc32(const void *data, uint32_t len);

// Доступ к сектору (на МК — HAL_FLASH; в host-тестах — имитация, -DSETTINGS_FLASH_FAKE)
const uint32_t* settings_flash_words(void);                     // начало сектора
bool settings_flash_erase(void);
bool settings_flash_write(uint32_t offset, const uint32_t *words, uint32_t n); // offset в байтах

#endif /* SETTINGS_H_ */
