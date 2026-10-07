/*
 * sd_drv.c — настоящий драйвер SD-карты (Core/Src/fatfs_sd.c) поверх имитации
 * шины SPI и карты (fake_sd.c). В этой же программе SD_disk_* для логики
 * прибора имитирует fake_hal.c (карта на уровне FatFs), поэтому драйвер
 * собран под именами drv_disk_*.
 */
#define SD_SPI_FAKE
#define SD_disk_initialize drv_disk_initialize
#define SD_disk_status     drv_disk_status
#define SD_disk_read       drv_disk_read
#define SD_disk_write      drv_disk_write
#define SD_disk_ioctl      drv_disk_ioctl
#define SD_disk_check      drv_disk_check
#include "../../Core/Src/fatfs_sd.c"
