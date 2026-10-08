/*
 * usb_cli_ext.h — команды USB CDC для программы на ПК (pc/, krenomer.exe):
 * снимок состояния одной строкой JSON, часы, смена адреса датчика, ноль,
 * список файлов на карте и передача файла. Сама командная строка (приём,
 * кольцо передачи, разбор) — usb_cli.c; отсюда в неё три точки входа.
 *
 * Команды (регистр не важен, ответ кончается строкой OK… или ERR…):
 *   status                  одна строка JSON (поля — README, раздел «Командная
 *                           строка по USB»), затем OK
 *   set time YYYY-MM-DD HH:MM:SS   установить часы прибора (и DS3231)
 *   addr OLD NEW            сменить Modbus-адрес датчика OLD -> NEW (1..247);
 *                           асинхронно: итог — поле "svc" в status
 *   zero | zero reset       ноль по текущим углам отвечающих датчиков / сброс
 *   files                   *.CSV в корне карты: строки F,<имя>,<байт>,<ГГГГ-ММ-ДД ЧЧ:ММ>,
 *                           затем OK <число файлов>
 *   get <имя> [смещение]    передача файла: G,<имя>,<размер>,<смещение>, строки
 *                           D,<base64 до 48 байт>, E,<передано байт>,<crc32 hex>, OK
 *   get abort               прервать передачу (или список)
 *   samples on | off        каждый свежий отсчёт датчика — строкой
 *                           R,<адрес>,<n>,<t_ms>,<RawX>,<RawY>,<OffsetX>,<OffsetY>,<BatV>:
 *                           n — номер ответа датчика (ok_count: пропуски видны),
 *                           t_ms — HAL_GetTick() ответа, RawX/RawY — ответ
 *                           датчика в сотых градуса (код регистра - 10000),
 *                           OffsetX/OffsetY — ноль в тысячных, BatV — в десятых
 *                           вольта; округление как в CSV на карте. Для записи
 *                           на ПК (программа пишет тот же CSV). Не влезла строка
 *                           в кольцо — выброшена (счётчик — ответ «samples»).
 *                           Выключается и при открытии/закрытии порта.
 *   samples                 состояние: OK samples on|off, dropped N
 *
 * files и get не блокируют суперцикл: за проход usb_cli_task() — не больше
 * сектора файла (512 байт) или нескольких записей каталога, и только когда в
 * кольце передачи есть место. Во время записи замера (SD_RECORDING) обе
 * команды отказывают; начавшаяся запись или вынутая карта прерывают передачу.
 */
#ifndef USB_CLI_EXT_H_
#define USB_CLI_EXT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "app.h"

// --- Точки входа из usb_cli.c ---

// Выполнить команду, если она отсюда. cmd — первое слово (нижний регистр),
// arg — остаток строки без ведущих и хвостовых пробелов. true — команда
// выполнена (ответ выдан), false — не наша (разбирает usb_cli.c).
bool usb_cli_ext_exec(const char *cmd, const char *arg);

// Строки справки (help) о командах отсюда
void usb_cli_ext_help(void);

// Каждый проход usb_cli_task(): очередная порция files / get
void usb_cli_ext_task(uint32_t now);

// Хост открыл или закрыл порт (DTR): прервать files / get молча — иначе новый
// хост получил бы хвост чужой передачи вперемешку с ответами на свои команды;
// выключить samples
void usb_cli_ext_abort(void);

// После каждого цикла опроса (app.c, как sd_logger_write_cycle): при включённой
// команде samples — строка R на каждый датчик со свежим отсчётом (fresh[i])
void usb_cli_ext_samples(const bool fresh[APP_SENSOR_COUNT]);

// --- Вывод: реализован в usb_cli.c ---

// Дописать в кольцо передачи n байт: всё или ничего (не влезло — выброшено)
void usb_cli_out(const char *s, uint32_t n);
// Свободно байт в кольце передачи; 0 — хост не слушает (вывод выбрасывается)
uint32_t usb_cli_out_free(void);

// --- Чистые функции (host-тесты) ---

// Base64 (RFC 4648, с '='): n байт -> 4 * ceil(n / 3) знаков + '\0'. Длина без '\0'.
size_t usb_cli_ext_base64(char *out, const uint8_t *in, size_t n);
// CRC-32 (IEEE 802.3, как zlib crc32()): crc = usb_cli_ext_crc32(crc, p, n), начало — 0
uint32_t usb_cli_ext_crc32(uint32_t crc, const uint8_t *p, size_t n);
// "YYYY-MM-DD HH:MM:SS" (вместо пробела можно 't'), годы 2000..2099, дата
// проверяется app_time_valid(). false — не разобрано или такой даты нет.
bool usb_cli_ext_parse_time(const char *s, app_time_t *t);
// Снимок состояния (g_app) одной строкой JSON с "\r\n" в out (size байт).
// Возвращает длину; 0 — не влезло. now — HAL_GetTick().
size_t usb_cli_ext_status_json(char *out, size_t size, uint32_t now);

#endif /* USB_CLI_EXT_H_ */
