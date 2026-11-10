/*
 * version.h — версия прошивки: показывается в «Справке» и командой USB «ver».
 * Менять при каждом выпуске прошивки (дата сборки берётся из __DATE__).
 */
#ifndef VERSION_H_
#define VERSION_H_

#include "relative.h"
#define FW_VERSION "1.6.0"
#if APP_DEFAULT_INSTRUMENT == 1
#define FW_DEFAULT_INSTRUMENT "level"
#else
#define FW_DEFAULT_INSTRUMENT "heel"
#endif

#endif /* VERSION_H_ */
