#!/bin/sh
# Host-тесты логики прибора (Git Bash). Компилятор: HOST_CC или gcc из PATH.
#   tests/host/run.sh
set -e
cd "$(dirname "$0")/../.."
CC="${HOST_CC:-gcc}"
OUT=build_host
mkdir -p "$OUT"
"$CC" -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter \
	-D__USE_MINGW_ANSI_STDIO=1 -DAPP_VREFINT_CAL=1500 -DSETTINGS_FLASH_FAKE -DBWM427_TIMER_FAKE \
	-include tests/host/stub/ff_integer_host.h \
	-Itests/host/stub -Itests/host -ICore/Inc -IFATFS/Target \
	-IMiddlewares/Third_Party/FatFs/src \
	tests/host/test_main.c tests/host/fake_hal.c tests/host/fake_sd.c tests/host/sd_drv.c \
	Core/Src/bwm427.c Core/Src/sd_logger.c Core/Src/app.c Core/Src/settings.c \
	-lm -o "$OUT/host_tests.exe"
"$OUT/host_tests.exe"
# Команды USB для программы на ПК (usb_cli_ext.c): своя имитация FatFs с чтением
"$CC" -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter \
	-D__USE_MINGW_ANSI_STDIO=1 \
	-include tests/host/stub/ff_integer_host.h \
	-Itests/host/stub -Itests/host -ICore/Inc -IFATFS/Target \
	-IMiddlewares/Third_Party/FatFs/src \
	tests/host/test_cli_ext.c Core/Src/usb_cli_ext.c \
	-o "$OUT/cli_ext_tests.exe"
"$OUT/cli_ext_tests.exe"

# Все символы строк интерфейса есть в шрифтах (иначе — пустые прямоугольники)
"${PYTHON:-python}" tools/ui_sim/fonts/gen_fonts.py --check
