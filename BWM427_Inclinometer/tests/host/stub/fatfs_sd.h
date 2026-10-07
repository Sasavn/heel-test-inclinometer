/*
 * host-тесты: для логики прибора (sd_logger.c) драйвер SD заменён имитацией
 * карты на уровне FatFs (fake_hal.c). Настоящий драйвер Core/Src/fatfs_sd.c
 * проверяется отдельно поверх имитации SPI (sd_drv.c, fake_sd.c).
 */
#ifndef FATFS_SD_H
#define FATFS_SD_H
#include "main.h"
#include "diskio.h"
#define SD_CS_PORT SD_CS_GPIO_Port
#define SD_CS_PIN  SD_CS_Pin
DSTATUS SD_disk_initialize(BYTE pdrv);
DSTATUS SD_disk_status(BYTE pdrv);
DRESULT SD_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
DRESULT SD_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
DRESULT SD_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);
DRESULT SD_disk_check(BYTE pdrv, BYTE *buff);
#endif
