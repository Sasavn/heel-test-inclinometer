/*
 * settings.c — журнал настроек в секторе 7 внутренней флеш-памяти.
 * Формат записи, правила выбора и сохранения — в settings.h.
 *
 * Доступ к флешу сведён к трём функциям (settings_flash_*): на МК — HAL_FLASH,
 * в host-тестах (-DSETTINGS_FLASH_FAKE) — имитация в tests/host/fake_hal.c.
 */
#include "settings.h"
#include <string.h>

#ifndef SETTINGS_FLASH_FAKE
#include "main.h"
#endif

#define REC_CRC_LEN   (SETTINGS_REC_SIZE - 4u) // CRC — по всей записи, кроме самой CRC
#define WRITE_TRIES   3u                       // мест подряд для попытки записи

static settings_info_t s_info;
static uint16_t s_next;          // первое место после последней непустой записи
static settings_t s_last;        // значения последней правильной записи
static bool s_have_last;
static uint32_t s_touch_ms;      // момент последнего изменения

/* ------------------------------------------------------------------------ */
/* Флеш (только на МК)                                                      */
/* ------------------------------------------------------------------------ */

#ifndef SETTINGS_FLASH_FAKE

static void flash_begin(void) {
	HAL_FLASH_Unlock();
	// Флаги ошибок прошлых операций (в т. ч. отладчика) сорвали бы новую
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR
			| FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR
			| FLASH_FLAG_RDERR);
}

static void flash_end(void) {
	HAL_FLASH_Lock();
	// Кэш данных флеша (ART) может помнить старое содержимое сектора, а запись
	// сразу читается обратно для проверки
	if (READ_BIT(FLASH->ACR, FLASH_ACR_DCEN)) {
		__HAL_FLASH_DATA_CACHE_DISABLE();
		__HAL_FLASH_DATA_CACHE_RESET();
		__HAL_FLASH_DATA_CACHE_ENABLE();
	}
}

const uint32_t* settings_flash_words(void) {
	return (const uint32_t*) SETTINGS_FLASH_ADDR;
}

// Стереть сектор 7: процессор стоит 1-2 с (код выполняется из того же флеша)
bool settings_flash_erase(void) {
	FLASH_EraseInitTypeDef e = { 0 };
	uint32_t bad = 0;
	e.TypeErase = FLASH_TYPEERASE_SECTORS;
	e.Sector = FLASH_SECTOR_7;
	e.NbSectors = 1;
	e.VoltageRange = FLASH_VOLTAGE_RANGE_3; // питание 2,7-3,6 В: операции по 32 бита
	flash_begin();
	HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e, &bad);
	flash_end();
	return st == HAL_OK && bad == 0xFFFFFFFFu;
}

// Записать n слов, ~16 мкс на слово
bool settings_flash_write(uint32_t offset, const uint32_t *words, uint32_t n) {
	bool ok = true;
	flash_begin();
	for (uint32_t k = 0; k < n && ok; k++) {
		ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
				SETTINGS_FLASH_ADDR + offset + 4u * k, words[k]) == HAL_OK;
	}
	flash_end();
	return ok;
}

#endif

/* ------------------------------------------------------------------------ */
/* Журнал                                                                   */
/* ------------------------------------------------------------------------ */

uint32_t settings_crc32(const void *data, uint32_t len) {
	const uint8_t *p = (const uint8_t*) data;
	uint32_t crc = 0xFFFFFFFFu;
	while (len--) {
		crc ^= *p++;
		for (uint8_t i = 0; i < 8; i++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

static const uint32_t* slot(uint16_t i) {
	return settings_flash_words() + (uint32_t) i * SETTINGS_REC_WORDS;
}

// Место не тронуто после стирания
static bool slot_blank(const uint32_t *w) {
	for (uint32_t k = 0; k < SETTINGS_REC_WORDS; k++) {
		if (w[k] != 0xFFFFFFFFu) {
			return false;
		}
	}
	return true;
}

static bool rec_valid(const settings_rec_t *r) {
	return r->magic == SETTINGS_MAGIC && r->crc == settings_crc32(r, REC_CRC_LEN);
}

// Совпадают побитно (float — тоже: так NaN не пишется заново при каждом сохранении)
static bool same(const settings_t *a, const settings_t *b) {
	return a->log_freq_hz == b->log_freq_hz && a->bus_gap_ms == b->bus_gap_ms
			&& a->theme == b->theme
			&& memcmp(&a->ema_alpha, &b->ema_alpha, sizeof(a->ema_alpha)) == 0
			&& memcmp(&a->bat_alarm_v, &b->bat_alarm_v, sizeof(a->bat_alarm_v)) == 0
			&& a->roll_window_s == b->roll_window_s && a->roll_rate_hz == b->roll_rate_hz
			&& a->roll_calm_cdeg == b->roll_calm_cdeg
			&& a->roll_hyst_cdeg == b->roll_hyst_cdeg;
}

bool settings_load(settings_t *out) {
	settings_rec_t best;
	bool found = false;
	memset(&s_info, 0, sizeof(s_info));
	memset(&best, 0, sizeof(best));
	s_have_last = false;
	s_next = 0;
	for (uint16_t i = 0; i < SETTINGS_CAPACITY; i++) {
		const uint32_t *w = slot(i);
		if (slot_blank(w)) {
			continue;
		}
		s_next = (uint16_t) (i + 1u); // писать только после последнего непустого места
		settings_rec_t r;
		memcpy(&r, w, sizeof(r));
		if (rec_valid(&r) && (!found || r.seq >= best.seq)) {
			best = r;
			found = true;
		}
	}
	s_info.used = s_next;
	if (!found) {
		return false;
	}
	s_last.log_freq_hz = best.log_freq_hz;
	s_last.ema_alpha = best.ema_alpha;
	s_last.bus_gap_ms = best.bus_gap_ms;
	s_last.theme = best.theme;
	s_last.bat_alarm_v = best.bat_alarm_v; // старая запись: 0xFFFFFFFF = NaN
	s_last.roll_window_s = best.roll_window_s;   // старая запись: 0xFF...
	s_last.roll_rate_hz = best.roll_rate_hz;
	s_last.roll_calm_cdeg = best.roll_calm_cdeg;
	s_last.roll_hyst_cdeg = best.roll_hyst_cdeg;
	s_have_last = true;
	s_info.loaded = true;
	s_info.seq = best.seq;
	*out = s_last;
	return true;
}

bool settings_store(const settings_t *s) {
	s_info.pending = false;
	if (s_have_last && same(s, &s_last)) {
		return true; // вернули прежние значения — писать нечего
	}
	settings_rec_t r;
	memset(&r, 0xFF, sizeof(r));
	r.magic = SETTINGS_MAGIC;
	r.seq = s_info.seq + 1u;
	r.log_freq_hz = s->log_freq_hz;
	r.bus_gap_ms = s->bus_gap_ms;
	r.theme = s->theme;
	r.ema_alpha = s->ema_alpha;
	r.bat_alarm_v = s->bat_alarm_v;
	r.roll_window_s = s->roll_window_s;
	r.roll_rate_hz = s->roll_rate_hz;
	r.roll_calm_cdeg = s->roll_calm_cdeg;
	r.roll_hyst_cdeg = s->roll_hyst_cdeg;
	r.crc = settings_crc32(&r, REC_CRC_LEN);
	uint32_t words[SETTINGS_REC_WORDS];
	memcpy(words, &r, sizeof(words));

	for (uint8_t t = 0; t < WRITE_TRIES; t++) {
		// Первое чистое место (после сбоя записи следующее может быть занято)
		while (s_next < SETTINGS_CAPACITY && !slot_blank(slot(s_next))) {
			s_next++;
		}
		if (s_next >= SETTINGS_CAPACITY) {
			// Сектор заполнен: стереть и писать с начала. Выключение питания
			// именно сейчас (1-2 с раз в 4096 сохранений) вернёт значения по умолчанию.
			if (!settings_flash_erase()) {
				s_info.last_err = SETTINGS_ERR_ERASE;
				return false;
			}
			s_info.erases++;
			s_next = 0;
		}
		uint16_t i = s_next++;
		s_info.used = s_next;
		if (settings_flash_write((uint32_t) i * SETTINGS_REC_SIZE, words, SETTINGS_REC_WORDS)
				&& memcmp(slot(i), words, sizeof(words)) == 0) {
			s_last = *s;
			s_have_last = true;
			s_info.seq = r.seq;
			s_info.saves++;
			s_info.last_err = SETTINGS_ERR_NONE;
			return true;
		}
	}
	s_info.last_err = SETTINGS_ERR_WRITE;
	return false;
}

void settings_touch(uint32_t now) {
	s_info.pending = true;
	s_touch_ms = now;
}

bool settings_due(uint32_t now) {
	return s_info.pending && now - s_touch_ms >= SETTINGS_SAVE_DELAY_MS;
}

const settings_info_t* settings_get_info(void) {
	return &s_info;
}
