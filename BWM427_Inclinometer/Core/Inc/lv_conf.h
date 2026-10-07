/*
 * lv_conf.h — конфигурация LVGL v9 (проверено на 9.6.0) для BWM427_Inclinometer.
 *
 * STM32F411CE: 512 КБ flash, 128 КБ RAM, без ОС, без DMA для дисплея.
 * Дисплей ILI9341 320x240 RGB565 (SPI1), ввод — энкодер TIM3 + кнопка PA0.
 *
 * Здесь задано только то, что отличается от умолчаний LVGL
 * (Middlewares/Third_Party/lvgl/include/lvgl/config/lv_conf_internal.h).
 * Всё ненужное (виджеты, темы, ФС, декодеры картинок, логи, монитор
 * производительности) выключено, чтобы не тратить flash.
 *
 * Подключается через LV_CONF_INCLUDE_SIMPLE (Makefile и .cproject).
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
 * ПАМЯТЬ И СТАНДАРТНАЯ БИБЛИОТЕКА
 *====================*/

// Встроенный аллокатор LVGL (TLSF) на статическом массиве
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
// Свои memcpy/memset у LVGL копируют словами; newlib-nano — побайтно
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN
#define LV_SPRINTF_USE_FLOAT    0  // дробные числа интерфейс форматирует сам

// Куча LVGL (объекты, стили, тексты меток). Все 4 экрана создаются один раз,
// смена темы их только перекрашивает. Симулятор tools/ui_sim (32-битная сборка,
// как Cortex-M) показывает максимум ~27.8 КБ занятых из ~36.5 КБ доступных
// (остальное — служебные данные TLSF), т.е. ~25 % запаса. Буфер отрисовки
// сюда не входит.
#define LV_MEM_SIZE             (40U * 1024U)

/*====================
 * ОС, ТАЙМЕРЫ, ДИСПЛЕЙ
 *====================*/

#define LV_USE_OS               LV_OS_NONE

// Период перерисовки и опроса энкодера, мс
#define LV_DEF_REFR_PERIOD      30

// 2.8" 320x240 -> ~143 точки на дюйм
#define LV_DPI_DEF              143

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

/*====================
 * ОТРИСОВКА (программная)
 *====================*/

#define LV_USE_DRAW_SW          1
// Рисуем только в RGB565 (байты меняются местами в lv_port_disp.c перед SPI)
#define LV_DRAW_SW_SUPPORT_RGB565                  1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED          0
#define LV_DRAW_SW_SUPPORT_RGB565A8                0
#define LV_DRAW_SW_SUPPORT_RGB888                  0
#define LV_DRAW_SW_SUPPORT_XRGB8888                0
#define LV_DRAW_SW_SUPPORT_ARGB8888                0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED  0
#define LV_DRAW_SW_SUPPORT_L8                      0
#define LV_DRAW_SW_SUPPORT_AL88                    0
#define LV_DRAW_SW_SUPPORT_A8                      0
#define LV_DRAW_SW_SUPPORT_I1                      0
// Скругления и обводки рамок фокуса требуют «сложного» рисования
#define LV_DRAW_SW_COMPLEX      1
#define LV_DRAW_SW_SHADOW_CACHE_SIZE 0
#define LV_DRAW_SW_CIRCLE_CACHE_SIZE 4
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 0
#define LV_USE_DRAW_SW_ASM      LV_DRAW_SW_ASM_NONE
#define LV_USE_DRAW_ARM2D_SYNC  0
#define LV_USE_NATIVE_HELIUM_ASM 0

#define LV_USE_MATRIX           0
#define LV_USE_VECTOR_GRAPHIC   0
#define LV_USE_THORVG           0
#define LV_USE_DRAW_DMA2D       0  // у F411 нет DMA2D

/*====================
 * ЛОГИ, ПРОВЕРКИ, ОТЛАДКА
 *====================*/

#define LV_USE_LOG              0

// Оставлены только дешёвые проверки: нехватка кучи и NULL
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0
#define LV_USE_CHECK_ARG            0

#define LV_USE_SYSMON           0
#define LV_USE_PERF_MONITOR     0
#define LV_USE_MEM_MONITOR      0
#define LV_USE_PROFILER         0
#define LV_USE_REFR_DEBUG       0
#define LV_USE_LAYER_DEBUG      0
#define LV_USE_PARALLEL_DRAW_DEBUG 0
#define LV_USE_TEST             0
#define LV_USE_MONKEY           0
#define LV_USE_SNAPSHOT         0
#define LV_USE_OBJ_ID           0
#define LV_USE_OBJ_NAME         0
#define LV_USE_OBJ_PROPERTY     0
#define LV_USE_OBSERVER         0
#define LV_USE_FLOAT            0
#define LV_USE_TRANSLATION      0
#define LV_USE_PRIVATE_API      0

/*====================
 * ШРИФТЫ
 *====================*/

// Встроенные Montserrat не нужны: в них нет кириллицы.
// Свои шрифты (Core/Src/ui/fonts, генерация: tools/ui_sim/fonts/gen_fonts.py):
//   ui_font_14  — Montserrat Medium 14, ASCII + кириллица + символы
//   ui_font_20  — Montserrat Medium 20, то же
//   ui_font_num — Montserrat Bold 36, только цифры и знаки (углы, поля ввода)
#define LV_FONT_MONTSERRAT_14   0
#define LV_USE_CUSTOM_FONT_DEFAULT 1
#define LV_FONT_CUSTOM_DECLARE  LV_FONT_DECLARE(ui_font_14) \
                                LV_FONT_DECLARE(ui_font_20) \
                                LV_FONT_DECLARE(ui_font_num)
#define LV_FONT_DEFAULT         &ui_font_14
#define LV_USE_FONT_COMPRESSED  0
#define LV_USE_FONT_PLACEHOLDER 0
#define LV_USE_FONT_MANAGER     0
#define LV_USE_IMGFONT          0
#define LV_USE_FREETYPE         0
#define LV_USE_TINY_TTF         0

#define LV_TXT_ENC              LV_TXT_ENC_UTF8
#define LV_USE_BIDI             0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*====================
 * ТЕМЫ И РАЗМЕТКА
 *====================*/

// Темы LVGL не используются: все стили и обе темы интерфейса (тёмная/светлая)
// задаёт ui.c
#define LV_USE_THEME_DEFAULT    0
#define LV_USE_THEME_SIMPLE     0
#define LV_USE_THEME_MONO       0

// Раскладка абсолютная (координаты в ui_*.c), flex/grid не нужны
#define LV_USE_FLEX             0
#define LV_USE_GRID             0

/*====================
 * ВИДЖЕТЫ: только метка и кнопка
 *====================*/

#define LV_WIDGETS_HAS_DEFAULT_VALUE 0
#define LV_USE_LABEL            1
#define LV_LABEL_TEXT_SELECTION 0
#define LV_LABEL_LONG_TXT_HINT  0
#define LV_USE_BUTTON           1

#define LV_USE_3DTEXTURE        0
#define LV_USE_ANIMIMG          0
#define LV_USE_ARC              0
#define LV_USE_ARCLABEL         0
#define LV_USE_BAR              0
#define LV_USE_BARCODE          0
#define LV_USE_BUTTONMATRIX     0
#define LV_USE_CALENDAR         0
#define LV_USE_CANVAS           0
#define LV_USE_CHART            0
#define LV_USE_CHECKBOX         0
#define LV_USE_DROPDOWN         0
#define LV_USE_IMAGE            0
#define LV_USE_IMAGEBUTTON      0
#define LV_USE_KEYBOARD         0
#define LV_USE_LED              0
#define LV_USE_LINE             0
#define LV_USE_LIST             0
#define LV_USE_LOTTIE           0
#define LV_USE_MENU             0
#define LV_USE_MSGBOX           0
#define LV_USE_QRCODE           0
#define LV_USE_ROLLER           0
#define LV_USE_SCALE            0
#define LV_USE_SLIDER           0
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          0
#define LV_USE_SWITCH           0
#define LV_USE_TEXTAREA         0
#define LV_USE_TABLE            0
#define LV_USE_TABVIEW          0
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0
#define LV_USE_GLTF             0

/*====================
 * ФАЙЛЫ, КАРТИНКИ, ПРОЧЕЕ — выключено
 *====================*/

#define LV_USE_FS_STDIO         0
#define LV_USE_FS_POSIX         0
#define LV_USE_FS_WIN32         0
#define LV_USE_FS_FATFS         0  // SD-картой владеет логика, не LVGL
#define LV_USE_FS_MEMFS         0
#define LV_USE_FS_LITTLEFS      0
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 0
#define LV_USE_FS_ARDUINO_SD    0
#define LV_USE_FS_UEFI          0
#define LV_USE_FS_FROGFS        0

#define LV_CACHE_DEF_SIZE       0
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 0
#define LV_USE_RLE              0
#define LV_USE_LZ4              0
#define LV_USE_LODEPNG          0
#define LV_USE_LIBPNG           0
#define LV_USE_BMP              0
#define LV_USE_TJPGD            0
#define LV_USE_LIBJPEG_TURBO    0
#define LV_USE_LIBWEBP          0
#define LV_USE_GIF              0
#define LV_USE_SVG              0
#define LV_USE_RLOTTIE          0
#define LV_USE_FFMPEG           0
#define LV_USE_GSTREAMER        0
#define LV_USE_IME_PINYIN       0
#define LV_USE_FILE_EXPLORER    0
#define LV_USE_FRAGMENT         0
#define LV_USE_GRIDNAV          0
#define LV_USE_GESTURE_RECOGNITION 0

// Драйверы дисплеев LVGL не нужны: свой порт в Core/Src/ui/lv_port_disp.c
#define LV_USE_ILI9341          0
#define LV_USE_ST7735           0
#define LV_USE_ST7789           0
#define LV_USE_ST7796           0
#define LV_USE_GENERIC_MIPI     0
#define LV_USE_SDL              0
#define LV_USE_X11              0
#define LV_USE_WAYLAND          0
#define LV_USE_LINUX_FBDEV      0
#define LV_USE_LINUX_DRM        0
#define LV_USE_EVDEV            0
#define LV_USE_LIBINPUT         0
#define LV_USE_NUTTX            0
#define LV_USE_OPENGLES         0
#define LV_USE_GLFW             0
#define LV_USE_QNX              0
#define LV_USE_UEFI             0
#define LV_USE_ST_LTDC          0
#define LV_USE_TFT_ESPI         0
#define LV_USE_LOVYAN_GFX       0
#define LV_USE_FT81X            0

#endif /* LV_CONF_H */
