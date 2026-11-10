# Project FatFS override (ESP-IDF v5.5.2)

The pinned SDK's `ffconf.h` hardcodes `FF_FS_EXFAT=0`; it has no upstream
`CONFIG_FATFS_EXFAT` option. This component adds that Kconfig switch and
generates the five core/header files in the build directory. Only the
exFAT switch in `ffconf.h` is changed. Upstream licenses and implementations
are retained; no installed SDK file is modified.

`prepare.py` validates normalized-LF hashes from `upstream.json` before
writing anything. Unexpected upstream source fails configuration. The rest
of the component uses the original pinned diskio/VFS/OS port sources and
dependencies. `diskio.h` is also generated because its quoted `ff.h` must
resolve to the same ABI. Do not add the original SDK `src` include directory.

All consumers receive the generated include directory. `config_guard.c`
checks exFAT, LFN, UTF-8 and 64-bit `FSIZE_t` in the actual target build.
Defaults use heap LFN, length 255, UTF-8 and volume-label support. The latter
keeps the pinned exFAT runtime `FF_USE_LABEL` expression defined; no label
or formatting operation is performed by storage startup.

Clean builds use `sdkconfig.defaults`. Developers retaining an older ignored
sdkconfig must select `FATFS_EXFAT`, `FATFS_LFN_HEAP`, `FATFS_MAX_LFN=255`,
`FATFS_API_ENCODING_UTF_8` and `FATFS_USE_LABEL`, then reconfigure. Both
canonical target profiles are checked with these effective values.

Upstream: https://github.com/espressif/esp-idf/tree/v5.5.2/components/fatfs
