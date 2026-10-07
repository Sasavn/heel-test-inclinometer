"""narfu_logo.svg -> narfu_logo_small.svg: упрощённая книга для 36 px.

Слово «сафу» (bjl-top), эллипс (bjl-border) и дуга (bjl-under) — как в
оригинале, дуга слегка утолщена обводкой. Книга (bjl-txt): тот же контур и
нижний изгиб раскрытых страниц, но вместо 11 волосяных прорезей — 3 широкие,
то есть 4 толстых луча (в 36 px высоты исходные прорези тоньше 0,3 px и
сливаются в сплошной прямоугольник).
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
s = open(src, encoding='utf-8').read()

GAP = 2.4      # ширина прорези вверху, единицы SVG (36 px: масштаб 0.6 -> ~1.4 px)
TIP = 0.5      # ширина прорези внизу
# (центр по x, глубина) — слева лучи длиннее, как в оригинале
gaps = [(33.25, 8.0), (28.65, 12.5), (24.05, 15.5)]

top = 'V0 '
for cx, depth in gaps:  # справа налево
    top += (f'H{cx + GAP / 2:.2f} L{cx + TIP / 2:.2f} {depth:.2f} '
            f'H{cx - TIP / 2:.2f} L{cx - GAP / 2:.2f} 0 ')
book = ('M20.12 22.55 c2.914-.341 3.64-.341 5.601 0 c1.735.341 3.2 1.095 4.01 1.763 '
        'c1.706-2.389 4.479-2.503 7.877-2.488 l1.592.014 ' + top + 'H20.18 Z')
s, n = re.subn(r'(<path class="bjl-txt" d=")[^"]*(")', lambda m: m.group(1) + book + m.group(2), s)
assert n == 1
# Дуга: тонкая — утолщить обводкой того же цвета
s, n = re.subn(r'(<path class="bjl-under"[^>]*?fill="#00AEEF")', r'\1 stroke="#00AEEF" stroke-width="0.9"', s)
assert n == 1, n
open(dst, 'w', encoding='utf-8').write(s)
print('ok')
