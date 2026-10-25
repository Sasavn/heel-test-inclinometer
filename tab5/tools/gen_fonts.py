#!/usr/bin/env python3
"""Generate Tab5 native 4bpp fonts; requires Node/npm and Python fonttools.
Run: python tab5/tools/gen_fonts.py [--check]
Text: tab5/assets/fonts/Roboto-*.ttf (Apache 2.0, LICENSE_ROBOTO_APACHE2.txt).
Symbols Roboto lacks (✓ ⚠ ✗ ● ▲ ■ ○ ⌫ →): pc/assets/fonts/DejaVuSans*.ttf (LICENSE_DEJAVU.txt).
No STM32 font is modified. The converter is pinned to lv_font_conv 1.5.3.
"""
import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'tab5' / 'main'
ASSETS = ROOT / 'pc' / 'assets' / 'fonts'
ROBOTO = ROOT / 'tab5' / 'assets' / 'fonts'
TEXT = set(range(0x20, 0x7f)) | set(range(0x400, 0x460)) | {ord(c) for c in '°–—−·…✓⚠✗●▲⌫±■○«»→•'}
SYMBOLS = {ord(c) for c in '✓⚠✗●▲⌫■○→'}
NUM = set(map(ord, ' +-.0123456789:°—−'))
LARGE_NUM = NUM | {ord('±')}
# (suffix, size, Roboto face, DejaVu face for symbols, codes)
FONTS = [('16', 16, 'Roboto-Regular.ttf', 'DejaVuSans.ttf', TEXT), ('16m', 16, 'Roboto-Medium.ttf', 'DejaVuSans-Bold.ttf', TEXT),
         ('20', 20, 'Roboto-Regular.ttf', 'DejaVuSans.ttf', TEXT), ('20m', 20, 'Roboto-Medium.ttf', 'DejaVuSans-Bold.ttf', TEXT),
         ('24', 24, 'Roboto-Bold.ttf', 'DejaVuSans-Bold.ttf', TEXT), ('32', 32, 'Roboto-Bold.ttf', 'DejaVuSans-Bold.ttf', TEXT),
         ('40', 40, 'Roboto-Bold.ttf', 'DejaVuSans-Bold.ttf', TEXT), ('64', 64, 'Roboto-Bold.ttf', 'DejaVuSans-Bold.ttf', NUM)]

FONTS += [('28', 28, 'Roboto-Medium.ttf', 'DejaVuSans-Bold.ttf', TEXT),
          ('36', 36, 'Roboto-Bold.ttf', 'DejaVuSans-Bold.ttf', TEXT),
          ('48', 48, 'Roboto-Bold.ttf', 'DejaVuSans-Bold.ttf', TEXT),
          ('56', 56, 'Roboto-Bold.ttf', 'DejaVuSans-Bold.ttf', TEXT),
          ('108', 108, 'Roboto-Bold.ttf', 'DejaVuSans-Bold.ttf', LARGE_NUM)]
LARGE = {'28', '36', '48', '56', '108'}

def ranges(codes):
    return ','.join(hex(c) for c in sorted(codes))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--only', nargs='+', choices=[f[0] for f in FONTS],
                        help='Font suffixes; defaults to the five large fonts, leaving legacy files untouched')
    args = parser.parse_args()
    selected = set(args.only) if args.only else LARGE
    for suffix, size, filename, fallback, codes in FONTS:
        if suffix not in selected:
            continue
        source = ROBOTO / filename
        symbols = ASSETS / fallback
        own = codes - SYMBOLS
        missing = own - TTFont(source).getBestCmap().keys()
        missing |= (codes & SYMBOLS) - TTFont(symbols).getBestCmap().keys()
        if missing:
            raise SystemExit(f'{source.name}: missing {sorted(missing)}')
        name = f'font_tab5_{suffix}'
        target = OUT / f'{name}.c'
        with tempfile.TemporaryDirectory(prefix='tab5-font-') as tmp:
            generated = Path(tmp) / target.name
            npx = shutil.which('npx.cmd') or shutil.which('npx')
            if not npx:
                raise SystemExit('Node/npm npx is required')
            parts = ['--font', str(source), '--range', ranges(own)]
            if codes & SYMBOLS:
                parts += ['--font', str(symbols), '--range', ranges(codes & SYMBOLS)]
            subprocess.run([npx, '--yes', 'lv_font_conv@1.5.3', *parts,
                            '--size', str(size), '--bpp', '4', '--format', 'lvgl',
                            '--no-compress', '--no-prefilter', '--lv-font-name', name,
                            '-o', str(generated)], check=True)
            data = generated.read_text(encoding='utf-8')
            data = re.sub(r' \* Opts:.*', ' * Generated with tab5/tools/gen_fonts.py; Roboto (Apache 2.0) + DejaVu symbols, see tab5/assets/fonts', data)
            data = data.replace('#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#else\n#include "lvgl/lvgl.h"\n#endif', '#include "lvgl.h"')
            data = data.replace('#include "lvgl.h"', '#include "native/native_ui.h"')
            data = re.sub(r'\blv_', 'nui_', data)
            data = re.sub(r'\bLV_', 'NUI_', data).replace('LVGL_VERSION_', 'NUI_VERSION_')
        data = data.rstrip() + '\n'
        if args.check:
            if not target.exists() or target.read_bytes() != data.encode('utf-8'):
                raise SystemExit(f'{target.name}: reproducibility check failed')
        else:
            times = target.stat() if target.exists() else None
            target.write_bytes(data.encode('utf-8'))
            if times:
                import os
                os.utime(target, ns=(times.st_atime_ns, times.st_mtime_ns))
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
        if codes == TEXT:
            strings = re.findall(r'"((?:[^"\\]|\\.)*)"', (OUT / 'tab5_ui.c').read_text(encoding='utf-8'))
            used = {ord(c) for text in strings for c in text if ord(c) > 127}
            if used - actual:
                raise SystemExit(f'{target.name}: UI glyphs missing {sorted(used-actual)}')
        if '.bpp = 4' not in data or re.search(r'\blv_|\bLV_|#include.*lvgl', data):
            raise SystemExit(f'{target.name}: expected native uncompressed 4bpp font')
        print(f'{target.name}: {len(actual)} glyphs verified; {target.stat().st_size} source bytes')

if __name__ == '__main__':
    main()
