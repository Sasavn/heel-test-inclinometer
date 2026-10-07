#include "ili9341.h"

// Подключаем внешний SPI, который настроил CubeMX
extern SPI_HandleTypeDef hspi1;

/*
 * Скорость SPI1: APB2 96 МГц / прескалер 2 = 48 МГц SCK.
 * По даташиту ILI9341 минимальный цикл записи 4-wire SPI — 100 нс (10 МГц),
 * т.е. шина работает примерно в 5 раз быстрее нормы. На имеющемся модуле это
 * работает стабильно, поэтому оставлено как есть. Если появятся «мусор»/сдвиги
 * на экране (длинный шлейф, другой модуль) — поднять BaudRatePrescaler SPI1
 * в CubeMX до 4 (24 МГц) или 8 (12 МГц, в пределах даташита).
 */
#define ILI9341_SPI_TIMEOUT_MS  100U    // на один блок; 32 КБ при 48 МГц ≈ 6 мс
#define ILI9341_SPI_CHUNK       32768U  // HAL_SPI_Transmit принимает uint16_t длину

// Макросы для управления пином CS (Выбор чипа)
#define ILI9341_Select()   HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET)
#define ILI9341_Unselect() HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET)

// Линия DC: 0 — команда, 1 — данные
#define ILI9341_DC_Cmd()   HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET)
#define ILI9341_DC_Data()  HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET)

// Передать буфер по SPI кусками (CS и DC уже выставлены)
static void ILI9341_Transmit(const uint8_t *data, uint32_t len) {
	while (len > 0) {
		uint16_t n = (len > ILI9341_SPI_CHUNK) ? ILI9341_SPI_CHUNK : (uint16_t) len;
		HAL_SPI_Transmit(&hspi1, (uint8_t*) data, n, ILI9341_SPI_TIMEOUT_MS);
		data += n;
		len -= n;
	}
}

// Отправить команду с параметрами одной транзакцией (CS активен всё время)
static void ILI9341_Command(uint8_t cmd, const uint8_t *params, uint8_t n) {
	ILI9341_Select();
	ILI9341_DC_Cmd();
	HAL_SPI_Transmit(&hspi1, &cmd, 1, ILI9341_SPI_TIMEOUT_MS);
	if (n > 0) {
		ILI9341_DC_Data();
		HAL_SPI_Transmit(&hspi1, (uint8_t*) params, n, ILI9341_SPI_TIMEOUT_MS);
	}
	ILI9341_Unselect();
}

// Отправить команду с одним байтом параметра
static void ILI9341_Command1(uint8_t cmd, uint8_t param) {
	ILI9341_Command(cmd, &param, 1);
}

// Инициализировать дисплей (полная рабочая последовательность)
void ILI9341_Init(void) {
	ILI9341_Unselect();

	// Аппаратный сброс
	HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
	HAL_Delay(50);
	HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
	HAL_Delay(50);

	ILI9341_Command(0x01, NULL, 0); // Software Reset
	HAL_Delay(150);                 // по даташиту 5 мс (120 мс, если был Sleep Out)

	// --- ОБЯЗАТЕЛЬНЫЕ КОМАНДЫ НАСТРОЙКИ МАТРИЦЫ ---
	ILI9341_Command1(0xC0, 0x23);   // Power control 1
	ILI9341_Command1(0xC1, 0x10);   // Power control 2
	static const uint8_t vcom1[2] = { 0x3E, 0x28 };
	ILI9341_Command(0xC5, vcom1, 2); // VCOM control 1
	ILI9341_Command1(0xC7, 0x86);   // VCOM control 2
	ILI9341_Command1(0x36, 0x28);   // Memory Access Control: альбомная 320x240 (MV + BGR)
	// ----------------------------------------------

	ILI9341_Command1(0x3A, 0x55);   // Pixel Format Set: 16-bit color

	ILI9341_Command(0x11, NULL, 0); // Sleep Out
	HAL_Delay(120);

	ILI9341_Command(0x29, NULL, 0); // Display ON
	HAL_Delay(20);
}

// Установить ориентацию экрана
void ILI9341_SetRotation(uint8_t rotation) {
	if (rotation == 1) {
		ILI9341_Command1(0x36, 0x28); // Landscape (320x240)
	} else {
		ILI9341_Command1(0x36, 0x48); // Portrait (240x320)
	}
}

// Открыть окно для записи пикселей; CS остаётся активным до ILI9341_EndWrite()
void ILI9341_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
	uint8_t col[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
	uint8_t row[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
	uint8_t cmd;

	ILI9341_Select();

	cmd = 0x2A; // Column Address Set
	ILI9341_DC_Cmd();
	HAL_SPI_Transmit(&hspi1, &cmd, 1, ILI9341_SPI_TIMEOUT_MS);
	ILI9341_DC_Data();
	HAL_SPI_Transmit(&hspi1, col, 4, ILI9341_SPI_TIMEOUT_MS);

	cmd = 0x2B; // Page Address Set
	ILI9341_DC_Cmd();
	HAL_SPI_Transmit(&hspi1, &cmd, 1, ILI9341_SPI_TIMEOUT_MS);
	ILI9341_DC_Data();
	HAL_SPI_Transmit(&hspi1, row, 4, ILI9341_SPI_TIMEOUT_MS);

	cmd = 0x2C; // Memory Write
	ILI9341_DC_Cmd();
	HAL_SPI_Transmit(&hspi1, &cmd, 1, ILI9341_SPI_TIMEOUT_MS);

	ILI9341_DC_Data(); // дальше идут только пиксели
}

// Передать пиксели в открытое окно (RGB565, старший байт первым)
void ILI9341_WritePixels(const uint8_t *data, uint32_t len) {
	ILI9341_Transmit(data, len);
}

// Закончить запись в окно
void ILI9341_EndWrite(void) {
	ILI9341_Unselect();
}

// Вывести прямоугольник пикселей одной транзакцией
void ILI9341_DrawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
		const uint8_t *data) {
	if (w == 0 || h == 0)
		return;
	ILI9341_SetWindow(x, y, x + w - 1, y + h - 1);
	ILI9341_Transmit(data, (uint32_t) w * h * 2U);
	ILI9341_EndWrite();
}

// Нарисовать один пиксель
void ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
	uint8_t data[2] = { color >> 8, color & 0xFF };
	ILI9341_SetWindow(x, y, x, y);
	HAL_SPI_Transmit(&hspi1, data, 2, ILI9341_SPI_TIMEOUT_MS);
	ILI9341_EndWrite();
}

// Залить весь экран одним цветом
void ILI9341_FillScreen(uint16_t color) {
	// Буфер на одну строку (320 пикселей * 2 байта); статический — не на стеке
	static uint8_t line_buf[ILI9341_WIDTH * 2];
	uint8_t hi = color >> 8;
	uint8_t lo = color & 0xFF;

	for (uint32_t i = 0; i < sizeof(line_buf); i += 2) {
		line_buf[i] = hi;
		line_buf[i + 1] = lo;
	}

	ILI9341_SetWindow(0, 0, ILI9341_WIDTH - 1, ILI9341_HEIGHT - 1);
	for (uint32_t i = 0; i < ILI9341_HEIGHT; i++) {
		ILI9341_Transmit(line_buf, sizeof(line_buf));
	}
	ILI9341_EndWrite();
}

// Нарисовать символ (Оптимизированная версия блочной записи)
void ILI9341_WriteChar(uint16_t x, uint16_t y, char ch, FontDef font,
		uint16_t color, uint16_t bgcolor) {
	uint32_t i, b, j;
	if (ch < 32 || ch > 126)
		return;

	// Задаем адресное окно размером ровно с одну букву ОДИН РАЗ
	ILI9341_SetWindow(x, y, x + font.width - 1, y + font.height - 1);

	uint8_t color_hi = color >> 8, color_lo = color & 0xFF;
	uint8_t bg_hi = bgcolor >> 8, bg_lo = bgcolor & 0xFF;

	// Буфер для одной горизонтальной линии символа (макс 32 пикселя)
	uint8_t row_buf[64];

	for (i = 0; i < font.height; i++) {
		b = font.data[(ch - 32) * font.height + i];
		uint32_t buf_idx = 0;

		// Собираем цвета пикселей всей строки в один массив
		for (j = 0; j < font.width; j++) {
			if ((b << j) & 0x8000) {
				row_buf[buf_idx++] = color_hi;
				row_buf[buf_idx++] = color_lo;
			} else {
				row_buf[buf_idx++] = bg_hi;
				row_buf[buf_idx++] = bg_lo;
			}
		}
		// Выплевываем всю строку в шину SPI за одну команду
		ILI9341_Transmit(row_buf, buf_idx);
	}

	ILI9341_EndWrite();
}

// Нарисовать строку текста
void ILI9341_WriteString(uint16_t x, uint16_t y, const char *str, FontDef font,
		uint16_t color, uint16_t bgcolor) {
	while (*str) {
		ILI9341_WriteChar(x, y, *str, font, color, bgcolor);
		x += font.width;
		str++;
	}
}
