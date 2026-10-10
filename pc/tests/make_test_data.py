# -*- coding: utf-8 -*-
"""Тестовые файлы «Обработки» (pc/tests/data): синтетическая непрерывная запись опыта и файлы старых форматов.

Запуск: python -X utf8 make_test_data.py  (пишет в data/ рядом; результат детерминирован, без numpy)

1. data/continuous/2026-10-09_M020_D2.CSV — СИНТЕТИЧЕСКАЯ запись всего опыта одним файлом (как настоящий файл
   прошивки 1.5 у руководителя, но данные сгенерированы): 10 положений груза, крен по X ≈ 0, 3, 6, 8, 10,5, −2,5, −6,
   −9,5, −12 и возврат ≈ 0,3°, по 40–60 с; после каждого переноса — всплеск на 1,5–3 с и затухающая качка (период
   6–10 с, 0,5–1,5°), постоянная лёгкая качка, шум 0,02°; 10 Гц, Ms от начала записи; OffsetX 2,701, OffsetY −0,819,
   BatV 11,6. Истинные уровни — в data/continuous/truth.txt (секунды начала/конца положения и уровень).
2. data/old/ — файлы прежних прошивок и старого анализатора (форматы — по исходникам в истории git):
   M_001.CSV          прошивка Romero2207 (май 2026): один датчик, «Time,RawX,…,BatV», Time «ДД.ММ.ГГ ЧЧ:ММ:СС»;
   M_002_1/2.CSV      прошивка Romero2207 (июнь 2026, программные часы с 01.06.26 12:00:00): два датчика;
   2026-10-07_M004_D2.CSV  наша прошивка 1.0–1.3: тот же формат + Ms;
   M_006_1.CSV        без строки заголовка (только данные);
   M_007_1.CSV        карту выдернули: оборванная последняя строка и нули в хвосте;
   analyzer/M_001.CSV тестовый файл BWM427_Analyzer (generate_test_data.py): «Time,CalcX,CalcY,BatV», ЧЧ:ММ:СС;
   excel/опыт 3 (Excel).csv  пересохранён в русском Excel: «;», запятая, BOM, CRLF, «sep=;», Time без секунд;
   excel/опыт 4.txt   Excel «Текст Юникод»: UTF-16LE с BOM, табуляция, запятая, CRLF;
   readme.txt         не замер (программа должна сказать «не понял формат»).
"""
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")


def write_bytes(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def comma(v, dec):
    s = "%.*f" % (dec, v)
    if s.startswith("-") and float(s) == 0.0:
        s = s  # прошивка пишет «-0,000» как есть
    return s.replace(".", ",")


# ---------------------------------------------------------------------------------------------------------------
# 1. Непрерывная запись опыта
# ---------------------------------------------------------------------------------------------------------------

LEVELS = [0.0, 3.0, 6.0, 8.0, 10.5, -2.5, -6.0, -9.5, -12.0, 0.3]


def continuous():
    rng = random.Random(20261009)
    off_x, off_y = 2.701, -0.819
    # Положения: длительность 40–60 с, переносы — 2–3 с (вес едет), всплеск 1,5–3 с от начала переноса
    segs = []  # (t0, t1, level) — интервалы, где груз стоит
    t = 0.0
    for i, lv in enumerate(LEVELS):
        dur = rng.uniform(40.0, 60.0)
        segs.append((t, t + dur, lv))
        t += dur
        if i + 1 < len(LEVELS):
            t += rng.uniform(2.0, 3.0)  # перенос
    total = t
    moves = []  # (начало переноса, конец, от, к, всплеск: длит, амплитуда; качка после: A0, период, затухание)
    for i in range(1, len(segs)):
        a, b = segs[i - 1][1], segs[i][0]
        moves.append(dict(t0=a, t1=b, frm=segs[i - 1][2], to=segs[i][2], sd=rng.uniform(1.5, 3.0),
                          sa=rng.uniform(2.0, 4.0) * rng.choice([-1, 1]), A=rng.uniform(0.5, 1.5),
                          T=rng.uniform(6.0, 10.0), tau=rng.uniform(12.0, 20.0), ph=rng.uniform(0, 2 * math.pi)))

    def clean(tt):
        """Крен без шума: уровень, перенос, всплеск, затухающая качка, постоянная лёгкая качка."""
        lv = segs[0][2]
        for s in segs:
            if tt >= s[0]:
                lv = s[2]
        v = None
        for m in moves:
            if m["t0"] <= tt < m["t1"]:  # груз едет: плавно от прежнего уровня к новому
                u = (tt - m["t0"]) / (m["t1"] - m["t0"])
                v = m["frm"] + (m["to"] - m["frm"]) * (0.5 - 0.5 * math.cos(math.pi * u))
        if v is None:
            v = lv
        for m in moves:
            if tt >= m["t0"]:
                d = tt - m["t0"]
                if d < m["sd"]:  # всплеск: удар, рывок датчика
                    v += m["sa"] * math.sin(math.pi * d / m["sd"]) * (1.0 + 0.3 * math.sin(2 * math.pi * d * 1.7))
                if tt >= m["t1"]:  # затухающая качка после переноса
                    e = tt - m["t1"]
                    v += m["A"] * math.exp(-e / m["tau"]) * math.sin(2 * math.pi * e / m["T"] + m["ph"])
        v += 0.15 * math.sin(2 * math.pi * tt / 9.3 + 0.4) + 0.05 * math.sin(2 * math.pi * tt / 4.1)
        return v

    lines = ["Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms"]
    start_clock = 14 * 3600 + 5 * 60 + 12
    ms = 63
    n = 0
    while ms / 1000.0 <= total:
        tt = ms / 1000.0
        x = clean(tt) + rng.gauss(0.0, 0.02)
        y = 0.32 + 0.004 * clean(tt) + 0.04 * math.sin(2 * math.pi * tt / 7.7) + rng.gauss(0.0, 0.02)
        raw_x = round(x + off_x, 2)
        raw_y = round(y + off_y, 2)
        calc_x = raw_x - off_x
        calc_y = raw_y - off_y
        sec = start_clock + int(tt)
        bat = 11.6 if rng.random() > 0.03 else 11.5
        lines.append("09.10.2026;%02d:%02d:%02d;%s;%s;%s;%s;%s;%s;%s;%d" % (
            sec // 3600, sec // 60 % 60, sec % 60, comma(raw_x, 2), comma(raw_y, 2), comma(off_x, 3), comma(off_y, 3),
            comma(calc_x, 3), comma(calc_y, 3), comma(bat, 1), ms))
        n += 1
        ms += 100 + rng.choice([-2, -1, 0, 0, 0, 1, 2, 3])
    write_bytes(os.path.join(DATA, "continuous", "2026-10-09_M020_D2.CSV"), ("\n".join(lines) + "\n").encode("ascii"))
    truth = ["# положение; начало, с; конец, с; уровень крена X, ° (синтетика make_test_data.py)"]
    for i, s in enumerate(segs):
        truth.append("%d;%.2f;%.2f;%.3f" % (i + 1, s[0], s[1], s[2]))
    write_bytes(os.path.join(DATA, "continuous", "truth.txt"), ("\n".join(truth) + "\n").encode("utf-8"))
    print("continuous: %d строк, %.1f с, положений %d" % (n, total, len(segs)))


# ---------------------------------------------------------------------------------------------------------------
# 2. Старые форматы
# ---------------------------------------------------------------------------------------------------------------

def old_rows(rng, n, hz, level_y, level_x, clock, date, off=(0.0, 0.0), ms=False, sensor_shift=0.0):
    """Строки прошивки до 1.3: «%02d.%02d.%02d %02d:%02d:%02d,%.3f×6,%.1f[,Ms]»."""
    d, mo, y = date
    out = []
    for i in range(n):
        t = i / hz
        s = clock + int(t)
        ax = level_x + 0.02 * math.sin(t * 0.7) + rng.uniform(-0.01, 0.01) + off[0]
        ay = level_y + sensor_shift + 0.03 * math.sin(t * 0.9) + rng.uniform(-0.02, 0.02) + off[1]
        row = "%02d.%02d.%02d %02d:%02d:%02d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f" % (
            d, mo, y, s // 3600, s // 60 % 60, s % 60, ax, ay, off[0], off[1], ax - off[0], ay - off[1], 12.3)
        if ms:
            row += ",%d" % int(round(t * 1000 + 37))
        out.append(row)
    return out


OLD_HEAD = "Time,RawX,RawY,OffsetX,OffsetY,CalcX,CalcY,BatV"


def old_formats():
    rng = random.Random(7)
    base = os.path.join(DATA, "old")
    # Romero2207, май 2026: один датчик, часы DS3231
    rows = old_rows(rng, 300, 10, 2.451, 0.12, 10 * 3600 + 15 * 60, (28, 5, 26))
    write_bytes(os.path.join(base, "M_001.CSV"), ("\n".join([OLD_HEAD] + rows) + "\n").encode("ascii"))
    # Romero2207, июнь 2026: два датчика, программные часы с 01.06.26 12:00:00, смещения нуля
    for k, shift in ((1, 0.0), (2, 0.031)):
        rows = old_rows(rng, 400, 10, -2.315, 0.08, 12 * 3600 + 3 * 60, (1, 6, 26), off=(0.105, -0.212),
                        sensor_shift=shift)
        write_bytes(os.path.join(base, "M_002_%d.CSV" % k), ("\n".join([OLD_HEAD] + rows) + "\n").encode("ascii"))
    # Наша прошивка 1.0–1.3: + Ms, крен по X
    rows = old_rows(rng, 350, 10, 0.21, 1.732, 15 * 3600 + 20 * 60, (7, 10, 26), ms=True)
    write_bytes(os.path.join(base, "2026-10-07_M004_D2.CSV"),
                ("\n".join([OLD_HEAD + ",Ms"] + rows) + "\n").encode("ascii"))
    # Без заголовка
    rows = old_rows(rng, 200, 10, 1.105, 0.05, 9 * 3600, (2, 6, 26))
    write_bytes(os.path.join(base, "M_006_1.CSV"), ("\n".join(rows) + "\n").encode("ascii"))
    # Карту выдернули: строки до последнего записанного сектора, оборванная строка, нули в хвосте кластера
    rows = old_rows(rng, 150, 10, -0.874, 0.02, 9 * 3600 + 600, (2, 6, 26))
    body = ("\n".join([OLD_HEAD] + rows) + "\n").encode("ascii") + b"02.06.26 09:10:15,0.0" + b"\x00" * 300
    write_bytes(os.path.join(base, "M_007_1.CSV"), body)
    # Тестовый файл BWM427_Analyzer: «Time,CalcX,CalcY,BatV», шаг 1 с, round(…, 3)
    lines = ["Time,CalcX,CalcY,BatV"]
    t0 = 10 * 3600 + 15 * 60
    for i in range(100):
        s = t0 + i
        cx = round(2.450 + rng.uniform(-0.04, 0.04), 3)
        cy = round(rng.uniform(-0.1, 0.1), 3)
        bv = round(rng.uniform(12.35, 12.45), 2)
        lines.append("%02d:%02d:%02d,%s,%s,%s" % (s // 3600, s // 60 % 60, s % 60, repr(cx), repr(cy), repr(bv)))
    write_bytes(os.path.join(base, "analyzer", "M_001.CSV"), ("\n".join(lines) + "\n").encode("ascii"))
    # Пересохранён в русском Excel (CSV UTF-8): «sep=;», BOM, «;», запятая, CRLF, Time «ДД.ММ.ГГГГ ЧЧ:ММ»
    rows = old_rows(rng, 1500, 10, 3.217, 0.33, 11 * 3600, (3, 6, 26))
    out = ["sep=;", OLD_HEAD.replace(",", ";")]
    for r in rows:
        f = r.split(",")
        dt = f[0]
        f[0] = dt[:6] + "20" + dt[6:14]  # «03.06.2026 11:00» — Excel показывает и сохраняет без секунд
        out.append(";".join([f[0]] + [x.replace(".", ",") for x in f[1:]]))
    write_bytes(os.path.join(base, "excel", "опыт 3 (Excel).csv"),
                b"\xef\xbb\xbf" + ("\r\n".join(out) + "\r\n").encode("utf-8"))
    # Excel «Текст Юникод»: UTF-16LE с BOM, табуляция
    rows = old_rows(rng, 180, 10, -1.648, 0.21, 11 * 3600 + 900, (3, 6, 26))
    out = [OLD_HEAD.replace(",", "\t")]
    for r in rows:
        f = r.split(",")
        out.append("\t".join([f[0]] + [x.replace(".", ",") for x in f[1:]]))
    write_bytes(os.path.join(base, "excel", "опыт 4.txt"), "﻿".encode("utf-16-le") +
                ("\r\n".join(out) + "\r\n").encode("utf-16-le"))
    write_bytes(os.path.join(base, "readme.txt"), "Здесь файлы старой прошивки.\r\n".encode("utf-8"))
    print("old: готово")


if __name__ == "__main__":
    continuous()
    old_formats()
