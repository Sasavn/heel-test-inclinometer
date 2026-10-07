#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Генерация шрифтов интерфейса (LVGL v9) в Core/Src/ui/fonts/.

Нужен Node.js: lv_font_conv запускается через `npx --yes lv_font_conv@1.5.3`.
Исходники шрифтов лежат рядом, в src/:
  Montserrat-Medium.ttf, Montserrat-Bold.ttf — github.com/JulietaUla/Montserrat
      (копии из репозитория LVGL, OFL), есть кириллица;
  DejaVuSans-subset.ttf — только греческий и геометрические фигуры
      (α и ● — в Montserrat их нет), вырезано fontTools из DejaVuSans.ttf LVGL;
  FontAwesome5-lvsymbols.ttf — символы LV_SYMBOL_* из
      scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff LVGL.
Лицензии — в licenses/.

Шрифты:
  ui_font_14  — Montserrat Medium 14: текст + значки строки состояния;
  ui_font_20  — Montserrat Medium 20: текст + значки меню и кнопок;
  ui_font_num — Montserrat Bold 36: только цифры и знаки (углы, поля ввода).

После lv_font_conv цифры 0-9 делаются моноширинными (ширина самой широкой
цифры, глиф по центру ячейки), чтобы меняющиеся числа не «прыгали».

Запуск: python tools/ui_sim/fonts/gen_fonts.py   (из корня проекта или откуда угодно)
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "src")
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "..", "Core", "Src", "ui", "fonts"))

MONT_MED = os.path.join(SRC, "Montserrat-Medium.ttf")
MONT_BOLD = os.path.join(SRC, "Montserrat-Bold.ttf")
DEJAVU = os.path.join(SRC, "DejaVuSans-subset.ttf")
FA = os.path.join(SRC, "FontAwesome5-lvsymbols.ttf")

# Текст: ASCII, °, ·, кириллица (А-я, Ё, ё), —, …, →, − (U+2212)
TEXT_RANGE = "0x20-0x7E,0xB0,0xB7,0x401,0x410-0x44F,0x451,0x2014,0x2026,0x2192,0x2212"
# Из DejaVu: α (фильтр EMA), ● (индикатор записи)
DEJAVU_RANGE = "0x3B1,0x25CF"

# Символы FontAwesome (коды LV_SYMBOL_*)
SYM_SD = "0xF7C2"               # LV_SYMBOL_SD_CARD
SYM_WARNING = "0xF071"          # LV_SYMBOL_WARNING
SYM_SETTINGS = "0xF013"         # LV_SYMBOL_SETTINGS
SYM_OK = "0xF00C"               # LV_SYMBOL_OK
SYM_CLOSE = "0xF00D"            # LV_SYMBOL_CLOSE
SYM_LEFT = "0xF053"             # LV_SYMBOL_LEFT
SYM_SAVE = "0xF0C7"             # LV_SYMBOL_SAVE
SYM_REFRESH = "0xF021"          # LV_SYMBOL_REFRESH
SYM_BELL = "0xF0F3"             # LV_SYMBOL_BELL     (меню: дата и время)
SYM_EDIT = "0xF304"             # LV_SYMBOL_EDIT     (меню: адрес датчика)
SYM_PAUSE = "0xF04C"            # LV_SYMBOL_PAUSE    (меню: пауза шины)
SYM_TINT = "0xF043"             # LV_SYMBOL_TINT     (меню: тема)
SYM_BATTERY_1 = "0xF243"        # LV_SYMBOL_BATTERY_1 (меню: порог АКБ)

# Размер крупных цифр углов. Ширина "−88.88°" = 4 цифры + знак + точка + °;
# две колонки X/Y должны уместиться в карточке датчика (ui_main.c, VAL_W)
NUM_SIZE = 36

FONTS = [
	# имя, размер, аргументы lv_font_conv (после --size)
	("ui_font_14", 14, [
		"--font", MONT_MED, "-r", TEXT_RANGE,
		"--font", DEJAVU, "-r", DEJAVU_RANGE,
		"--font", FA, "-r", ",".join([SYM_SD, SYM_WARNING, SYM_OK, SYM_CLOSE]),
	]),
	("ui_font_20", 20, [
		"--font", MONT_MED, "-r", TEXT_RANGE,
		"--font", DEJAVU, "-r", DEJAVU_RANGE,
		"--font", FA, "-r", ",".join([SYM_SETTINGS, SYM_OK, SYM_CLOSE, SYM_LEFT,
		                              SYM_WARNING, SYM_SAVE, SYM_REFRESH, SYM_BELL,
		                              SYM_EDIT, SYM_PAUSE, SYM_TINT, SYM_BATTERY_1]),
	]),
	# Крупные цифры углов и полей ввода: только то, что реально выводится:
	# пробел + . 0-9 : ° — и U+2212 «минус» (той же ширины, что и '+'; в тексте
	# интерфейса минус пишется как UI_MINUS). Только диапазоны, без
	# --symbols и "=>": на Windows npx.cmd портит не-ASCII и '>' в аргументах.
	("ui_font_num", NUM_SIZE, [
		"--font", MONT_BOLD, "-r", "0x20,0x2B,0x2E,0x30-0x3A,0xB0,0x2014,0x2212",
		"--no-kerning",
	]),
]

LV_FONT_CONV = ["lv_font_conv@1.5.3"]


def run_conv(name, size, args):
	npx = shutil.which("npx") or shutil.which("npx.cmd")
	if not npx:
		sys.exit("npx не найден: установите Node.js")
	out = os.path.join(OUT, name + ".c")
	cmd = [npx, "--yes"] + LV_FONT_CONV + [
		"--bpp", "4", "--size", str(size), "--format", "lvgl",
		"--no-compress", "--no-prefilter",
		"--lv-font-name", name, "-o", out] + args
	print(" ".join(os.path.basename(c) if os.path.isabs(c) else c for c in cmd))
	subprocess.run(cmd, check=True)
	return out


GLYPH_RE = re.compile(
	r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), "
	r"\.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}")
CODE_RE = re.compile(r"/\* U\+([0-9A-F]+) ")


def make_tabular_digits(path):
	"""Сделать цифры 0-9 одинаковой ширины (в единицах 1/16 px, кратно 16)."""
	with open(path, encoding="utf-8") as f:
		text = f.read()
	bm_start = text.index("glyph_bitmap[] = {")
	bm_end = text.index("};", bm_start)
	codes = [int(c, 16) for c in CODE_RE.findall(text[bm_start:bm_end])]
	dsc_start = text.index("glyph_dsc[] = {")
	dsc_end = text.index("};", dsc_start)
	dsc = text[dsc_start:dsc_end]
	glyphs = list(GLYPH_RE.finditer(dsc))
	# glyphs[0] — зарезервированный id 0, дальше глифы в порядке кодов
	assert len(glyphs) == len(codes) + 1, (len(glyphs), len(codes))
	digit_ids = [i + 1 for i, c in enumerate(codes) if 0x30 <= c <= 0x39]
	if len(digit_ids) != 10:
		return
	advs = [int(glyphs[i].group(2)) for i in digit_ids]
	target = ((max(advs) + 8) // 16) * 16   # целые пиксели: LVGL округляет adv по глифу
	parts = []
	last = 0
	for i in digit_ids:
		m = glyphs[i]
		bi, adv, bw, bh, ox, oy = (int(g) for g in m.groups())
		ox += int(round((target - adv) / 32.0))   # половина разницы, в пикселях
		new = ("{.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, "
		       ".ofs_x = %d, .ofs_y = %d}" % (bi, target, bw, bh, ox, oy))
		parts.append(dsc[last:m.start()])
		parts.append(new)
		last = m.end()
	parts.append(dsc[last:])
	text = text[:dsc_start] + "".join(parts) + text[dsc_end:]
	text = text.replace(" * Opts: ", " * Цифры 0-9 сделаны моноширинными (tools/ui_sim/fonts/gen_fonts.py)\n * Opts: ", 1)
	# Абсолютные пути в строке Opts -> короткие имена (чтобы файл не зависел от машины)
	text = text.replace(SRC + os.sep, "").replace(OUT + os.sep, "")
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		f.write(text)
	print("  tabular digits: adv %s -> %d (1/16 px)" % (advs, target))


def main():
	os.makedirs(OUT, exist_ok=True)
	for name, size, args in FONTS:
		path = run_conv(name, size, args)
		make_tabular_digits(path)


if __name__ == "__main__":
	main()
