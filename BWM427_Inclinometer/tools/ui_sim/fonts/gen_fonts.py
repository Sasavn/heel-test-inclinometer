#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Генерация шрифтов интерфейса (LVGL v9) в Core/Src/ui/fonts/ и проверка, что
в них есть все символы строк интерфейса.

Нужен Node.js: lv_font_conv запускается через `npx --yes lv_font_conv@1.5.3`,
и fontTools (pip install fonttools) — какой исходный шрифт содержит символ.
Исходники шрифтов лежат рядом, в src/:
  Montserrat-Medium.ttf, Montserrat-Bold.ttf — github.com/JulietaUla/Montserrat
      (копии из репозитория LVGL, OFL), есть кириллица;
  DejaVuSans-subset.ttf — только греческий и геометрические фигуры
      (α и ● — в Montserrat их нет), вырезано fontTools из DejaVuSans.ttf LVGL;
  FontAwesome5-lvsymbols.ttf — символы LV_SYMBOL_* из
      scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff LVGL;
  ShipIcon.ttf — свой значок «Качка» (кораблик с креном, U+E000), рисует
      make_ship_font.py (fontTools) — правят его там, затем перегенерируют.
Лицензии — в licenses/ (ShipIcon — своя работа проекта).

Шрифты:
  ui_font_14  — Montserrat Medium 14: текст + значки строки состояния;
  ui_font_20  — Montserrat Medium 20: текст + значки меню и кнопок;
  ui_font_num — Montserrat Bold 33: только цифры и знаки (углы, поля ввода).

Набор символов текстовых шрифтов — не весь алфавит, а только то, что
встречается в строках интерфейса (Core/Src/ui/*.c, ui_internal.h,
Core/Inc/version.h) + цифры и символы имён файлов (выводятся не из строк
интерфейса). Строки между метками FONT_SMALL_ONLY_BEGIN / _END (текст
справки) рисуются только шрифтом 14 — в шрифт 20 их символы не берутся.
Новая строка с новой буквой -> перегенерировать шрифты; проверка
(--check, её запускают tools/ui_sim/build.sh и tests/host/run.sh) найдёт
символ, которого нет, — иначе на экране был бы пустой прямоугольник.

Значки FontAwesome — списки SYM_* ниже (какой значок каким шрифтом
рисуется, по строкам не определить); проверка требует, чтобы каждый
LV_SYMBOL_* из интерфейса был хотя бы в одном шрифте. Свои значки — коды
U+E000–U+E0FF (OWN_ICONS, в строках — макросы UI_SYMBOL_* из
ui_internal.h) — так же: списки ICON_* ниже, проверка — что каждый код из
строк интерфейса есть хотя бы в одном шрифте; в текстовые наборы символов
они не попадают. Свои значки растеризуются без автохинтинга (рисунок уже
выровнен по пикселям шрифта 20).

После lv_font_conv цифры 0-9 делаются моноширинными (ширина самой широкой
цифры, глиф по центру ячейки), чтобы меняющиеся числа не «прыгали».

Экономия (флеш шрифтов, arm-none-eabi-gcc -Os, 2026-10-07):
  весь алфавит А-я + ASCII, 4 бит/пиксель (было)      37 044 Б
  только символы строк интерфейса, 4 бит/пиксель    25 764 Б
  ... 2 бит/пиксель (выбрано)                        16 856 Б
  ... 2 бит/пиксель + сжатие RLE                     15 758 Б
2 бит/пиксель (4 уровня сглаживания) на снимках симулятора не отличить от
4 бит/пиксель (tools/ui_sim/out/closeup/*_fonts_4bpp_vs_2bpp.png), а время
отрисовки то же: LVGL в обоих случаях распаковывает глиф в A8. Сжатие RLE
дало бы ещё ~1 КБ, но требует LV_USE_FONT_COMPRESSED (код распаковщика) и
распаковки при каждом выводе глифа — нагрузка на процессор, а она теперь на
экране; поэтому без сжатия.

Запуск (из корня проекта или откуда угодно):
  python tools/ui_sim/fonts/gen_fonts.py            сгенерировать
  python tools/ui_sim/fonts/gen_fonts.py --check    только проверить
  ... --bpp 2 | --compress | --out DIR              для сравнения вариантов
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
SRC = os.path.join(HERE, "src")
OUT = os.path.join(ROOT, "Core", "Src", "ui", "fonts")
UI_DIR = os.path.join(ROOT, "Core", "Src", "ui")
UI_SOURCES = [os.path.join(UI_DIR, "ui_internal.h"),
              os.path.join(ROOT, "Core", "Inc", "version.h")]
SYMBOL_DEF = os.path.join(ROOT, "Middlewares", "Third_Party", "lvgl", "include", "lvgl", "font",
                          "lv_symbol_def.h")

MONT_MED = os.path.join(SRC, "Montserrat-Medium.ttf")
MONT_BOLD = os.path.join(SRC, "Montserrat-Bold.ttf")
DEJAVU = os.path.join(SRC, "DejaVuSans-subset.ttf")
FA = os.path.join(SRC, "FontAwesome5-lvsymbols.ttf")
SHIP = os.path.join(SRC, "ShipIcon.ttf")

# Символы, которые выводятся не из строк интерфейса: цифры (числа) и знаки
# имён файлов замера 2026-10-07_M007_D2.CSV (sd_logger_file_name)
DYNAMIC = "0123456789" + "-_.MDCSV"

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
SYM_BATTERY_1 = "0xF243"        # LV_SYMBOL_BATTERY_1 (меню: порог АКБ, питание)
SYM_EYE = "0xF06E"              # LV_SYMBOL_EYE_OPEN (плитки: датчики)
SYM_LOOP = "0xF079"             # LV_SYMBOL_LOOP     (плитки: частота)

# Свои значки (ShipIcon.ttf): коды U+E000–U+E0FF
OWN_ICONS = range(0xE000, 0xE100)
ICON_SHIP = "0xE000"            # UI_SYMBOL_SHIP     (меню: качка)

# Размер крупных цифр углов. Ширина "−88.88°" = 4 цифры + знак + точка + °;
# в строке карточки датчика рядом с ней — «качка 12.34°» (ui_main.c)
NUM_SIZE = 33

TEXT_FONTS = {
	# имя: (размер, значки FontAwesome, свои значки)
	"ui_font_14": (14, [SYM_SD, SYM_WARNING, SYM_OK, SYM_CLOSE], []),
	"ui_font_20": (20, [SYM_SETTINGS, SYM_OK, SYM_CLOSE, SYM_LEFT, SYM_WARNING, SYM_SAVE,
	                    SYM_REFRESH, SYM_BELL, SYM_EDIT, SYM_PAUSE, SYM_TINT, SYM_BATTERY_1,
	                    SYM_SD, SYM_EYE, SYM_LOOP], [ICON_SHIP]),
}
# Крупные цифры углов и полей ввода: только то, что реально выводится:
# пробел + . 0-9 : ° — и U+2212 «минус» (той же ширины, что и '+'; в тексте
# интерфейса минус пишется как UI_MINUS). Только диапазоны, без
# --symbols и "=>": на Windows npx.cmd портит не-ASCII и '>' в аргументах.
NUM_RANGE = "0x20,0x2B,0x2E,0x30-0x3A,0xB0,0x2014,0x2212"

LV_FONT_CONV = ["lv_font_conv@1.5.3"]

# ---------------------------------------------------------------------------
# Символы строк интерфейса
# ---------------------------------------------------------------------------

FMT_RE = re.compile(r"%[-+ #0]*(\d+|\*)?(\.(\d+|\*))?(hh|h|ll|l|z|j|t)?[diouxXeEfgGcspn]")


def c_unescape(body):
	"""Тело строкового литерала C -> байты."""
	out = bytearray()
	i = 0
	simple = {"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, "'": 39, '"': 34, "a": 7,
	          "b": 8, "f": 12, "v": 11, "?": 63}
	while i < len(body):
		c = body[i]
		if c != "\\":
			out += c.encode("utf-8")
			i += 1
			continue
		n = body[i + 1]
		if n == "x":
			m = re.match(r"[0-9A-Fa-f]{1,2}", body[i + 2:])
			out.append(int(m.group(0), 16))
			i += 2 + len(m.group(0))
		elif n in "01234567" and n != "0" or (n == "0" and i + 2 < len(body) and body[i + 2] in "01234567"):
			m = re.match(r"[0-7]{1,3}", body[i + 1:])
			out.append(int(m.group(0), 8) & 0xFF)
			i += 1 + len(m.group(0))
		else:
			out.append(simple[n])
			i += 2
	return bytes(out)


def string_groups(path):
	"""Склеенные соседние строковые литералы файла: [(текст, только_шрифт_14)].
	Комментарии и #include пропускаются; макросы-строки (LV_SYMBOL_*, UI_*)
	не раскрываются — их символы берутся из файлов, где они определены."""
	text = open(path, encoding="utf-8").read()
	small_only = []
	for m in re.finditer(r"FONT_SMALL_ONLY_BEGIN(.*?)FONT_SMALL_ONLY_END", text, re.S):
		small_only.append((m.start(), m.end()))
	groups = []
	cur, cur_pos = None, 0
	i, n = 0, len(text)

	def flush():
		nonlocal cur
		if cur is not None:
			s = cur.decode("utf-8")
			s = FMT_RE.sub("", s.replace("%%", "\x00")).replace("\x00", "%")
			small = any(a <= cur_pos < b for a, b in small_only)
			groups.append((s, small))
			cur = None

	while i < n:
		c = text[i]
		if text.startswith("//", i):
			i = text.find("\n", i)
			i = n if i < 0 else i
		elif text.startswith("/*", i):
			i = text.index("*/", i) + 2
		elif c == "#" and text[:i].rstrip(" \t").endswith(("\n", "")) and \
				text.startswith("include", i + 1):
			i = text.find("\n", i)
			i = n if i < 0 else i
		elif c == "'":
			j = i + 1
			while text[j] != "'":
				j += 2 if text[j] == "\\" else 1
			flush()
			i = j + 1
		elif c == '"':
			j = i + 1
			while text[j] != '"':
				j += 2 if text[j] == "\\" else 1
			if cur is None:
				cur, cur_pos = b"", i
			cur += c_unescape(text[i + 1:j])
			i = j + 1
		elif c.isspace():
			i += 1
		elif c.isalnum() or c == "_":
			j = i
			while j < n and (text[j].isalnum() or text[j] == "_"):
				j += 1
			# Макрос-строка рядом с литералом не разрывает склейку
			if not re.fullmatch(r"(LV_SYMBOL_\w+|UI_[A-Z_]+|FW_\w+)", text[i:j]):
				flush()
			i = j
		else:
			flush()
			i += 1
	flush()
	return groups


def symbol_codes():
	"""LV_SYMBOL_* -> код символа (из lv_symbol_def.h)."""
	codes = {}
	for m in re.finditer(r'#define\s+(LV_SYMBOL_\w+)\s+"((?:\\x[0-9A-Fa-f]{2})+)"',
	                     open(SYMBOL_DEF, encoding="utf-8").read()):
		codes[m.group(1)] = ord(c_unescape(m.group(2)).decode("utf-8"))
	return codes


def ui_files():
	files = [os.path.join(UI_DIR, f) for f in sorted(os.listdir(UI_DIR))
	         if f.endswith(".c") and not f.startswith("lv_port")]
	return files + UI_SOURCES


def used_chars():
	"""(символы для шрифта 14, для шрифта 20, коды значков) по строкам интерфейса.
	Значки — LV_SYMBOL_* (FontAwesome) и свои (OWN_ICONS, из строк)."""
	small, mid = set(DYNAMIC), set(DYNAMIC)
	syms = set()
	codes = symbol_codes()
	for f in ui_files():
		for s, small_only in string_groups(f):
			small.update(s)
			if not small_only:
				mid.update(s)
		for name in re.findall(r"\bLV_SYMBOL_\w+", open(f, encoding="utf-8").read()):
			if name in codes:
				syms.add(codes[name])
	syms.update(ord(c) for c in small if ord(c) in OWN_ICONS)
	clean = lambda s: {ord(c) for c in s if ord(c) >= 0x20 and not 0xF000 <= ord(c) <= 0xF8FF
	                   and ord(c) not in OWN_ICONS}
	return clean(small), clean(mid), syms


# ---------------------------------------------------------------------------
# Генерация
# ---------------------------------------------------------------------------

def cmap(path):
	from fontTools.ttLib import TTFont
	return set(TTFont(path).getBestCmap())


def ranges(codes):
	"""Коды -> "0x20-0x7E,0xB0" для lv_font_conv -r."""
	codes = sorted(codes)
	parts, i = [], 0
	while i < len(codes):
		j = i
		while j + 1 < len(codes) and codes[j + 1] == codes[j] + 1:
			j += 1
		parts.append("0x%X" % codes[i] if i == j else "0x%X-0x%X" % (codes[i], codes[j]))
		i = j + 1
	return ",".join(parts)


def text_font_args(chars, syms, own):
	"""Символы по исходным шрифтам: Montserrat, нет — DejaVu; значки
	FontAwesome (syms) и свои (own, без автохинтинга)."""
	mont, dejavu = cmap(MONT_MED), cmap(DEJAVU)
	in_mont = {c for c in chars if c in mont}
	in_dejavu = {c for c in chars - in_mont if c in dejavu}
	missing = chars - in_mont - in_dejavu
	if missing:
		sys.exit("нет в исходных шрифтах: " + " ".join("U+%04X %s" % (c, chr(c))
		                                               for c in sorted(missing)))
	args = ["--font", MONT_MED, "-r", ranges(in_mont)]
	if in_dejavu:
		args += ["--font", DEJAVU, "-r", ranges(in_dejavu)]
	args += ["--font", FA, "-r", ",".join(syms)]
	if own:
		args += ["--font", SHIP, "--autohint-off", "-r", ",".join(own)]
	return args


def run_conv(name, size, args, out_dir, bpp, compress):
	npx = shutil.which("npx") or shutil.which("npx.cmd")
	if not npx:
		sys.exit("npx не найден: установите Node.js")
	out = os.path.join(out_dir, name + ".c")
	cmd = [npx, "--yes"] + LV_FONT_CONV + [
		"--bpp", str(bpp), "--size", str(size), "--format", "lvgl", "--no-prefilter",
		"--lv-font-name", name, "-o", out] + ([] if compress else ["--no-compress"]) + args
	print(" ".join(os.path.basename(c) if os.path.isabs(c) else c for c in cmd))
	subprocess.run(cmd, check=True)
	return out


GLYPH_RE = re.compile(
	r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), "
	r"\.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}")
CODE_RE = re.compile(r"/\* U\+([0-9A-F]+) ")


def font_codes(path):
	"""Коды символов в сгенерированном шрифте (по комментариям glyph_bitmap)."""
	text = open(path, encoding="utf-8").read()
	bm_start = text.index("glyph_bitmap[] = {")
	return [int(c, 16) for c in CODE_RE.findall(text[bm_start:text.index("};", bm_start)])]


def make_tabular_digits(path, out_dir):
	"""Сделать цифры 0-9 одинаковой ширины (в единицах 1/16 px, кратно 16)."""
	with open(path, encoding="utf-8") as f:
		text = f.read()
	codes = font_codes(path)
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
	text = text.replace(SRC + os.sep, "").replace(out_dir + os.sep, "")
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		f.write(text)
	print("  tabular digits: adv %s -> %d (1/16 px)" % (advs, target))


# ---------------------------------------------------------------------------
# Проверка
# ---------------------------------------------------------------------------

def check(out_dir):
	small, mid, syms = used_chars()
	need = {"ui_font_14": small, "ui_font_20": mid}
	have = {name: set(font_codes(os.path.join(out_dir, name + ".c"))) for name in need}
	ok = True
	for name, chars in need.items():
		missing = sorted(chars - have[name])
		if missing:
			ok = False
			print("%s: нет символов %s" % (name, " ".join("U+%04X «%s»" % (c, chr(c))
			                                              for c in missing)))
	for s in sorted(syms):
		if not any(s in h for h in have.values()):
			ok = False
			print("значка U+%04X нет ни в одном шрифте" % s)
	extra = {name: len(have[name] - need[name] - set(range(0xF000, 0xF900)) - set(OWN_ICONS))
	         for name in need}
	print("шрифты: %s — %s" % (", ".join("%s %d симв. (лишних %d)" % (n, len(have[n]), extra[n])
	                                    for n in need), "OK" if ok else "ОШИБКА"))
	if any(extra.values()):
		print("  (лишние символы не мешают, но тратят флеш: перегенерируйте шрифты)")
	return ok


def main():
	# Вывод по-русски и в консоли Windows (Git Bash, cmd)
	if hasattr(sys.stdout, "reconfigure"):
		sys.stdout.reconfigure(encoding="utf-8")
	ap = argparse.ArgumentParser()
	ap.add_argument("--check", action="store_true", help="только проверить шрифты")
	ap.add_argument("--out", default=OUT, help="куда писать .c (по умолчанию Core/Src/ui/fonts)")
	ap.add_argument("--bpp", type=int, default=2, help="бит на пиксель (по умолчанию 2)")
	ap.add_argument("--compress", action="store_true", help="сжатие RLE (нужно LV_USE_FONT_COMPRESSED)")
	a = ap.parse_args()
	out_dir = os.path.abspath(a.out)
	if a.check:
		sys.exit(0 if check(out_dir) else 1)

	os.makedirs(out_dir, exist_ok=True)
	small, mid, _ = used_chars()
	chars = {"ui_font_14": small, "ui_font_20": mid}
	for name, (size, syms, own) in TEXT_FONTS.items():
		path = run_conv(name, size, text_font_args(chars[name], syms, own), out_dir, a.bpp,
		                a.compress)
		make_tabular_digits(path, out_dir)
	path = run_conv("ui_font_num", NUM_SIZE, ["--font", MONT_BOLD, "-r", NUM_RANGE, "--no-kerning"],
	                out_dir, a.bpp, a.compress)
	make_tabular_digits(path, out_dir)
	if not check(out_dir):
		sys.exit(1)


if __name__ == "__main__":
	main()
