#!/bin/sh
# Симулятор интерфейса на ПК: собрать (LVGL + Core/Src/ui + заглушки),
# прогнать сценарий и сохранить снимки экрана в tools/ui_sim/out/*.png.
#
#   tools/ui_sim/build.sh            сборка + запуск
#   tools/ui_sim/build.sh clean      удалить build/ и out/
#
# Нужны: MinGW-w64 gcc с поддержкой -m32 (multilib) + make — из PATH или из
# каталога GCC_DIR (например, GCC_DIR=/c/mingw64/bin), Python с Pillow для
# перевода BMP -> PNG (без Pillow останутся BMP).
set -e
cd "$(dirname "$0")"

if [ -n "$GCC_DIR" ]; then
	PATH="$GCC_DIR:$PATH"
	export PATH
fi

if [ "$1" = "clean" ]; then
	make clean
	rm -rf out
	exit 0
fi

make -j8 CC=gcc

mkdir -p out
rm -f out/*.bmp out/*.png
./build/ui_sim.exe out

PY=${PYTHON:-python}
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
