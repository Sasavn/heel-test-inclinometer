#!/usr/bin/env python3
"""Generate Tab5 LVGL9 fonts; requires Node/npm and Python fonttools.
Run: python tab5/tools/gen_fonts.py [--check]
Sources and licenses: pc/assets/fonts/DejaVuSans*.ttf and LICENSE_DEJAVU.txt.
No STM32 font is modified. The converter is pinned to lv_font_conv 1.5.3.
"""
import argparse
import re
import shutil
import subprocess
from pathlib import Path
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'tab5' / 'main'
ASSETS = ROOT / 'pc' / 'assets' / 'fonts'
TEXT = set(range(0x20, 0x7f)) | set(range(0x400, 0x460)) | {0xb0, 0x2013, 0x2014, 0x2212}
NUM = set(map(ord, ' +-.0123456789:°—−'))
FONTS = [(20, 'DejaVuSans.ttf', TEXT), (28, 'DejaVuSans.ttf', TEXT), (64, 'DejaVuSans-Bold.ttf', NUM)]

def ranges(codes):
    return ','.join(hex(c) for c in sorted(codes))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for size, filename, codes in FONTS:
        source = ASSETS / filename
        cmap = TTFont(source).getBestCmap()
        missing = codes - cmap.keys()
        if missing:
            raise SystemExit(f'{source.name}: missing {sorted(missing)}')
        name = f'font_tab5_{size}'
        target = OUT / f'{name}.c'
        if not args.check:
            npx = shutil.which('npx.cmd') or shutil.which('npx')
            if not npx:
                raise SystemExit('Node/npm npx is required')
            subprocess.run([npx, '--yes', 'lv_font_conv@1.5.3', '--font', str(source),
                            '--size', str(size), '--bpp', '4', '--format', 'lvgl',
                            '--no-compress', '--no-prefilter', '--lv-font-name', name,
                            '--range', ranges(codes), '-o', str(target)], check=True)
        data = target.read_text(encoding='utf-8')
        if not args.check:
            data = re.sub(r' \* Opts:.*', ' * Generated with tab5/tools/gen_fonts.py; DejaVu license: pc/assets/fonts/LICENSE_DEJAVU.txt', data)
            data = data.replace('#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#else\n#include "lvgl/lvgl.h"\n#endif', '#include "lvgl.h"')
            target.write_text(data, encoding='utf-8')
        # Check the actual generated cmap range tables, not only command comments.
        blocks = re.findall(r'\.range_start\s*=\s*(\d+).*?\.range_length\s*=\s*(\d+).*?\.glyph_id_start.*?\.unicode_list\s*=\s*(NULL|unicode_list_\d+).*?\.list_length\s*=\s*(\d+)', data, re.S)
        actual = set()
        for start, length, sparse, count in blocks:
            start, length = int(start), int(length)
            if sparse == 'NULL':
                actual.update(range(start, start + length))
            else:
                m = re.search(rf'{sparse}\[\]\s*=\s*\{{(.*?)\}}', data, re.S)
                offsets = [int(v, 0) for v in re.findall(r'0x[0-9a-fA-F]+|\d+', m[1])]
                actual.update(start + v for v in offsets)
        if codes - actual:
            raise SystemExit(f'{target.name}: generated cmap missing {sorted(codes-actual)}')
        if size != 64:
            strings = re.findall(r'"((?:[^"\\]|\\.)*)"', (OUT / 'tab5_ui.c').read_text(encoding='utf-8'))
            used = {ord(c) for text in strings for c in text if ord(c) > 127}
            if used - actual:
                raise SystemExit(f'{target.name}: UI glyphs missing {sorted(used-actual)}')
        print(f'{target.name}: {len(actual)} glyphs verified')

if __name__ == '__main__':
    main()
