/*
 * Подключается через -include до всего остального: типы FatFs как на МК.
 * Иначе integer.h на Windows (_WIN32) тянет windows.h.
 */
#ifndef _FF_INTEGER
#define _FF_INTEGER
#include <stdint.h>
typedef int INT;
typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef short SHORT;
typedef unsigned short WORD;
typedef unsigned short WCHAR;
typedef int32_t LONG;
typedef uint32_t DWORD;
typedef unsigned long long QWORD;
#endif
