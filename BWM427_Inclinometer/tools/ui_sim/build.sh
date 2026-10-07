#!/bin/sh
# Симулятор интерфейса на ПК: собрать (LVGL + Core/Src/ui + заглушки),
# прогнать сценарий и сохранить снимки экрана в tools/ui_sim/out/*.png.
#
#   tools/ui_sim/build.sh            сборка + запуск (оба вида меню)
#   tools/ui_sim/build.sh clean      удалить build/ и out/
#
# Вид меню 1 (список, как в прошивке по умолчанию) — полный сценарий: главный
# экран, меню, общие экраны (снимки <тема>_NN_*, <тема>_menuA_*); вид 2
# (плитки) — только меню (<тема>_menuB_*).
#
# Сначала — проверка шрифтов: каждый символ строк интерфейса есть в шрифтах
# (tools/ui_sim/fonts/gen_fonts.py --check), иначе на экране был бы пустой
# прямоугольник.
#
# Нужны: MinGW gcc + make (из PATH или каталога GCC_DIR), Python с Pillow для
# перевода BMP -> PNG (без Pillow останутся BMP).
set -e
cd "$(dirname "$0")"

if [ -n "$GCC_DIR" ]; then
	PATH="$GCC_DIR:$PATH"
	export PATH
fi
PY=${PYTHON:-python}

if [ "$1" = "clean" ]; then
	make clean
	rm -rf out
	exit 0
fi

"$PY" fonts/gen_fonts.py --check

make -j8 CC=gcc MENU=1
make -j8 CC=gcc MENU=2

mkdir -p out
rm -f out/*.bmp out/*.png
./build/menu1/ui_sim.exe out
./build/menu2/ui_sim.exe out

if "$PY" -c "import PIL" 2>/dev/null; then
	"$PY" - <<'PYEOF'
import glob, os
from PIL import Image
for bmp in sorted(glob.glob("out/*.bmp")):
    png = bmp[:-4] + ".png"
    Image.open(bmp).save(png)
    os.remove(bmp)
print("PNG: %d files in tools/ui_sim/out" % len(glob.glob("out/*.png")))
PYEOF
else
	echo "Pillow не найден: снимки оставлены в BMP (tools/ui_sim/out)"
fi
