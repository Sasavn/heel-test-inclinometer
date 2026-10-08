#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Значок «Качка» — кораблик с креном — как шрифт TrueType из одного глифа:
src/ShipIcon.ttf, код U+E000 (область частного использования, вне
U+F000–U+F8FF, где значки FontAwesome / LV_SYMBOL_*). gen_fonts.py берёт его
в ui_font_20 так же, как значки FontAwesome; в C — UI_SYMBOL_SHIP
(ui_internal.h).

Рисунок: корабль с носа (поперечное сечение) — корпус, широкий у палубы и
сужающийся к килю, отдельно от него (зазор) надстройка и мачта с реем; весь
корабль накренён на HEEL_DEG. Под ним с зазором — горизонтальная (не
накренённая) ватерлиния во всю ширину значка: крен виден относительно неё.
Контуры — многоугольники, внешние по часовой стрелке (как требует TrueType).

Глиф рисуется только шрифтом 20 px, поэтому координаты ниже — в пикселях при
20 px (1 px = 100 единиц, unitsPerEm = 2000), ось y вверх, 0 — базовая
линия. Строка шрифта 20: от −4 до +18 px — глиф должен в неё помещаться,
иначе вырастет высота строки ui_font_20. Растеризует lv_font_conv без
автохинтинга (--autohint-off), т.е. как нарисовано: горизонтальные края
ватерлинии стоят на границах пикселей.

Запуск (fontTools):
  python tools/ui_sim/fonts/make_ship_font.py      -> src/ShipIcon.ttf
затем python tools/ui_sim/fonts/gen_fonts.py (перегенерировать шрифты).
"""
import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "src", "ShipIcon.ttf")

CODE = 0xE000
UPM = 2000                  # 20 px -> 1 px = 100 единиц
PX = UPM // 20

# --- Рисунок, пиксели при 20 px -------------------------------------------
ADVANCE = 22                # ширина значка (как у значков FontAwesome)
HEEL_DEG = 14.0             # крен; > 0 — мачта влево (против часовой)

# Корабль без крена, начало координат — киль посередине.
# Корпус: правый борт от киля до угла палубы (левый — зеркально)
HULL = [(3.2, 0.0), (8.5, 5.5)]
CABIN_GAP = 1.0             # зазор палуба — надстройка
CABIN_W = 3.8               # полуширина надстройки
CABIN_H = 3.6               # высота надстройки
MAST_W = 0.75               # полуширина мачты
MAST_H = 6.4                # высота мачты над надстройкой
YARD = (3.0, 3.3, 1.5)      # рей: полуширина, низ над надстройкой, толщина

# Ватерлиния: полоса во всю ширину значка, корабль над ней с зазором
WATER_Y = (-3.0, -1.0)      # низ и верх полосы (2 px, по границам пикселей)
WATER_GAP = 1.0             # от верха полосы до нижней точки корпуса
# ---------------------------------------------------------------------------


def mirror(right):
	"""Правая половина (снизу вверх) -> замкнутый контур против часовой."""
	return list(right) + [(-x, y) for x, y in reversed(right)]


def ship_parts():
	"""Контуры корабля без крена: корпус; надстройка с мачтой."""
	hull = mirror(HULL)
	deck = HULL[-1][1]
	y0 = deck + CABIN_GAP
	y1 = y0 + CABIN_H
	top = [(CABIN_W, y0), (CABIN_W, y1), (MAST_W, y1)]
	if YARD:
		w, h, t = YARD
		top += [(MAST_W, y1 + h), (w, y1 + h), (w, y1 + h + t), (MAST_W, y1 + h + t)]
	top += [(MAST_W, y1 + MAST_H)]
	return [hull, mirror(top)]


def contours():
	a = math.radians(HEEL_DEG)
	c, s = math.cos(a), math.sin(a)
	parts = [[(x * c - y * s, x * s + y * c) for x, y in p] for p in ship_parts()]
	pts = [p for part in parts for p in part]
	# По ширине — посередине значка, по высоте — над ватерлинией с зазором
	dx = (ADVANCE - (max(x for x, _ in pts) + min(x for x, _ in pts))) / 2
	dy = WATER_Y[1] + WATER_GAP - min(y for _, y in pts)
	out = [[(x + dx, y + dy) for x, y in p] for p in parts]
	yb, yt = WATER_Y
	out.append([(0, yb), (ADVANCE, yb), (ADVANCE, yt), (0, yt)])
	return out


def area(poly):
	return sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1])) / 2


def build(path):
	from fontTools.fontBuilder import FontBuilder
	from fontTools.pens.ttGlyphPen import TTGlyphPen

	pen = TTGlyphPen(None)
	for poly in contours():
		pts = [(int(round(x * PX)), int(round(y * PX))) for x, y in poly]
		if area(pts) > 0:          # TrueType: внешний контур — по часовой
			pts.reverse()
		pen.moveTo(pts[0])
		for p in pts[1:]:
			pen.lineTo(p)
		pen.closePath()
	ship = pen.glyph()
	empty = TTGlyphPen(None).glyph()

	fb = FontBuilder(UPM, isTTF=True)
	fb.font.recalcTimestamp = False     # одинаковый файл при каждом запуске
	fb.setupGlyphOrder([".notdef", "ship"])
	fb.setupCharacterMap({CODE: "ship"})
	fb.setupGlyf({".notdef": empty, "ship": ship})
	glyf = fb.font["glyf"]
	adv = ADVANCE * PX
	fb.setupHorizontalMetrics({".notdef": (adv, 0), "ship": (adv, glyf["ship"].xMin)})
	asc, desc = 18 * PX, -4 * PX
	fb.setupHorizontalHeader(ascent=asc, descent=desc)
	fb.setupNameTable({"familyName": "ShipIcon", "styleName": "Regular",
	                   "uniqueFontIdentifier": "ShipIcon-Regular",
	                   "fullName": "ShipIcon Regular", "psName": "ShipIcon-Regular",
	                   "version": "Version 1.0"})
	fb.setupOS2(sTypoAscender=asc, sTypoDescender=desc, sTypoLineGap=0,
	            usWinAscent=asc, usWinDescent=-desc)
	fb.setupPost()
	head = fb.font["head"]
	head.created = head.modified = 3786912000   # 2024-01-01: файл не меняется от запуска к запуску
	g = glyf["ship"]
	if not (desc <= g.yMin and g.yMax <= asc and 0 <= g.xMin and g.xMax <= adv):
		sys.exit("значок вышел за строку шрифта 20 (x 0..%d, y -4..18 px)" % ADVANCE)
	tmp = path + ".tmp"
	fb.save(tmp)
	os.replace(tmp, path)
	print("%s: U+%04X, %d контура, рамка x %.2f..%.2f, y %.2f..%.2f px, ширина %d px" % (
		os.path.basename(path), CODE, g.numberOfContours, g.xMin / PX, g.xMax / PX,
		g.yMin / PX, g.yMax / PX, ADVANCE))


def main():
	if hasattr(sys.stdout, "reconfigure"):
		sys.stdout.reconfigure(encoding="utf-8")
	ap = argparse.ArgumentParser()
	ap.add_argument("--out", default=OUT, help="куда писать TTF (по умолчанию src/ShipIcon.ttf)")
	build(os.path.abspath(ap.parse_args().out))


if __name__ == "__main__":
	main()
