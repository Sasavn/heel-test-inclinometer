/*
 * usb_cli.h — командная строка по USB CDC (виртуальный COM-порт, 0483:5740).
 *
 * Диагностика прибора и переход в системный загрузчик STM32 (USB DFU 0483:DF11)
 * без кнопок BOOT0/NRST — для удалённой перепрошивки.
 *
 * Команды — строки ASCII (регистр не важен, конец строки CR и/или LF),
 * ответ — строки с "\r\n", последняя строка ответа "OK" или "ERR ...":
 *   help            список команд
 *   ver             версия (дата сборки), UID кристалла
 *   diag            разовый снимок состояния (суперцикл, загрузка, датчики,
 *                   тайминги шины RS485, SD, батарея, ...)
 *   diag reset      обнулить статистику шины (задержки ответа, таймауты, CRC, цикл)
 *   stream N        каждые N мс строка "S,..." (0 = выкл, 50..60000)
 *   set ...         freq N | alpha X | gap N | theme dark|light | batalarm X
 *   boot | dfu      перезагрузка в системный загрузчик (USB DFU);
 *                   во время записи на SD только "boot force"
 *   reset           перезагрузка прошивки (во время записи — "reset force")
 *   status, set time, addr, zero, files, get — для программы на ПК, см.
 *                   usb_cli_ext.h (usb_cli_ext.c)
 *
 * Ничего не блокирует: без хоста или с закрытым портом вывод выбрасывается.
 * printf/_write в CDC не перенаправляются.
 */
#ifndef USB_CLI_H_
#define USB_CLI_H_

#include <stdint.h>
#include <stdbool.h>
#include "usb_cli_ext.h"   // команды для программы на ПК и вывод для них

// В самом начале main() (USER CODE 1, до HAL_Init): если перед программным
// сбросом была команда boot — уйти в системный загрузчик. Если прошивку
// запустил сам загрузчик (dfu-util ... :leave), а не сброс, — NVIC_SystemReset()
// для чистого старта. Иначе — возврат.
void usb_cli_boot_check(void);

// Каждый проход суперцикла: разбор принятых строк, поток "stream", передача.
void usb_cli_task(void);

// --- Из прерывания OTG_FS (usbd_cdc_if.c) ---

// Принятые по CDC байты (CDC_Receive_FS)
void usb_cli_rx_isr(const uint8_t *buf, uint32_t len);
// SET_CONTROL_LINE_STATE: хост открыл (DTR = 1) или закрыл (DTR = 0) порт
void usb_cli_line_state_isr(bool dtr);

#endif /* USB_CLI_H_ */
