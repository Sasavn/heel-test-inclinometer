#include "ff.h"

#ifdef CONFIG_FATFS_EXFAT
_Static_assert(FF_FS_EXFAT == 1, "exFAT configuration must reach the actual FatFS core");
_Static_assert(FF_USE_LFN > 0, "exFAT requires long filenames");
_Static_assert(FF_LFN_UNICODE == 2, "Tab5 storage API uses UTF-8 filenames");
_Static_assert(sizeof(FSIZE_t) == 8, "exFAT FatFS ABI requires 64-bit FSIZE_t");
#else
_Static_assert(FF_FS_EXFAT == 0, "FatFS ABI must match project configuration");
#endif
