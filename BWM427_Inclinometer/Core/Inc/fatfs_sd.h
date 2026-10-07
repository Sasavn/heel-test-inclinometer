#ifndef FATFS_SD_H
#define FATFS_SD_H

#include "stm32f4xx_hal.h"
#include "main.h"
#include "diskio.h"

// Используем пины, которые настроены в CubeMX
#define SD_CS_PORT SD_CS_GPIO_Port
#define SD_CS_PIN  SD_CS_Pin

// Инициализировать карту (375 кГц), затем перейти на рабочую скорость SPI2.
// Без карты возвращается за ~1-3 мс (MISO 0xFF / 0x00), при шуме на MISO —
// до ~10 мс. Вставленная карта: обычно 10-300 мс (ACMD41), не дольше ~1 с.
DSTATUS SD_disk_initialize(BYTE pdrv);
DSTATUS SD_disk_status(BYTE pdrv);
// Чтение/запись. Ошибка (в т. ч. карту вынули) снимает инициализацию: дальше
// RES_NOTRDY сразу, до следующего SD_disk_initialize().
DRESULT SD_disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count);
DRESULT SD_disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count);
DRESULT SD_disk_ioctl(BYTE pdrv, BYTE cmd, void* buff);
// Карта на месте? Чтение сектора 0 (в buff, 512 байт) с короткими ожиданиями
// (без записи карта не занята): без карты — не дольше ~5 мс.
DRESULT SD_disk_check(BYTE pdrv, BYTE* buff);

#endif
