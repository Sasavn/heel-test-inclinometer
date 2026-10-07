#ifndef ILI9341_H_
#define ILI9341_H_

#include "main.h"
#include "fonts.h"

// --- Размер экрана (альбомная ориентация, MADCTL = 0x28) ---
#define ILI9341_WIDTH       320
#define ILI9341_HEIGHT      240

// --- Базовые цвета для дисплея ---
#define ILI9341_BLACK       0x0000
#define ILI9341_WHITE       0xFFFF
#define ILI9341_RED         0xF800
#define ILI9341_GREEN       0x07E0
#define ILI9341_BLUE        0x001F
#define ILI9341_YELLOW      0xFFE0
#define ILI9341_CYAN        0x07FF

// --- Прототипы функций ---
void ILI9341_Init(void);
void ILI9341_SetRotation(uint8_t rotation);
void ILI9341_FillScreen(uint16_t color);
void ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void ILI9341_WriteChar(uint16_t x, uint16_t y, char ch, FontDef font, uint16_t color, uint16_t bgcolor);
void ILI9341_WriteString(uint16_t x, uint16_t y, const char* str, FontDef font, uint16_t color, uint16_t bgcolor);

// --- Блочная запись пикселей (для LVGL и заливок) ---
// Открыть окно x0..x1, y0..y1 (включительно) и начать запись в память (RAMWR).
// CS остаётся активным: дальше один или несколько ILI9341_WritePixels(),
// в конце обязательно ILI9341_EndWrite().
void ILI9341_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
// Передать len байт пикселей RGB565, старший байт первым (любая длина,
// внутри режется на куски для HAL_SPI_Transmit)
void ILI9341_WritePixels(const uint8_t *data, uint32_t len);
// Закончить запись (отпустить CS)
void ILI9341_EndWrite(void);
// Окно + пиксели одной транзакцией: w*h пикселей RGB565, старший байт первым
void ILI9341_DrawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data);

#endif /* ILI9341_H_ */
