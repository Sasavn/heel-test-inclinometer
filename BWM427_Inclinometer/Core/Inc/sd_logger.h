/*
 * sd_logger.h — запись замеров на SD-карту: файл на каждый датчик,
 * "YYYY-MM-DD_MNNN_Dk.CSV" (дата начала замера, сквозной номер замера,
 * Modbus-адрес датчика), например 2026-10-06_M007_D2.CSV.
 *
 * Состояние (g_app.sd_state, sd_err, file_number, rec_*) ведёт сам модуль,
 * старт/стоп — по g_app.rec_switch_on (тумблер записи).
 */
#ifndef SD_LOGGER_H_
#define SD_LOGGER_H_

#include <stdint.h>
#include <stdbool.h>
#include "app.h"

#define SD_ROW_MAX      128  // максимальная длина строки CSV с запасом
#define SD_NAME_LEN     32   // "2026-10-06_M9999_D247.CSV" + '\0' с запасом (длинные имена FatFs, _USE_LFN)
#define SD_NUM_MAX      9999 // номера замеров 001..9999 (с 1000 — четыре цифры)

// Смонтировать карту при старте и найти номер следующего замера.
void sd_logger_init(void);

// Каждый проход суперцикла: старт/стоп записи по тумблеру, f_sync раз в
// секунду, проверка наличия карты и повторное монтирование.
void sd_logger_task(void);

// После каждого завершённого цикла опроса: открыть файлы датчиков, которые
// появились во время записи, и дописать по строке для каждого датчика,
// давшего в этом цикле свежий отсчёт (fresh[i]).
void sd_logger_write_cycle(const bool fresh[APP_SENSOR_COUNT]);

// Имя файла датчика i (индекс в g_app.sensor) в текущем замере (идёт запись —
// дата начала записи) или в следующем (дата — сегодняшняя). false — номера
// нет (карты нет или номера кончились).
bool sd_logger_file_name(uint8_t i, char out[SD_NAME_LEN]);

// --- Чистые функции (host-тесты) ---

// Строка CSV: Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms + '\n',
// дробная часть через запятую (столбцы — см. sd_logger.c).
// Возвращает длину без '\0' (не больше SD_ROW_MAX - 1).
uint16_t sd_format_row(char *out, const app_time_t *t, const app_sensor_t *s,
		float bat_v, uint32_t ms);

// "YYYY-MM-DD_MNNN_Dk.CSV": дата, номер замера (не меньше трёх цифр),
// Modbus-адрес датчика
void sd_make_name(char *out, const app_time_t *date, uint16_t num, uint8_t addr);

// Разобрать имя файла замера: новое "YYYY-MM-DD_MNNN_Dk.CSV" или старое
// "M_NNN_k.CSV" (регистр букв не важен). false — чужой файл.
bool sd_parse_name(const char *name, uint16_t *num, uint8_t *addr);

// Номер после num: num + 1, а после SD_NUM_MAX — 0 (номера кончились)
uint16_t sd_number_after(uint16_t num);

#endif /* SD_LOGGER_H_ */
