"""Verify that native font glyph data and page geometry retain the baseline.

This is a source/data check, not a replacement for rendered pixel comparison.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline', default='369c7fc')
p.add_argument('--output', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]

def original(path):
    return subprocess.check_output(['git', 'show', f'{a.baseline}:{path}'], cwd=root).decode('utf-8')

def arrays(text):
    # Compare every data array (bitmaps, glyph descriptors, cmaps and kerning),
    # normalizing toolkit names while retaining all numeric values and offsets.
    result = {}
    pattern = r'static\s+(?:\w+\s+)*?(\w+)\s*\[\s*\]\s*=\s*\{(.*?)\n\};'
    for name, body in re.findall(pattern, text, re.S):
        body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
        body = re.sub(r'\bnui_', 'lv_', body)
        body = re.sub(r'\bNUI_', 'LV_', body)
        result[name] = re.sub(r'\s+', '', body)
    return result

fonts = []
for path in sorted((root / 'tab5/main').glob('font_tab5_*.c')):
    rel = path.relative_to(root).as_posix()
    old, new = arrays(original(rel)), arrays(path.read_text(encoding='utf-8'))
    if not old or 'glyph_bitmap' not in old or 'glyph_dsc' not in old:
        raise SystemExit(f'Cannot recognize font arrays: {rel}')
    same = old == new
    fonts.append({'font': path.name, 'arrays': len(old), 'identical': same,
                  'normalized_sha256': hashlib.sha256(json.dumps(new, sort_keys=True).encode()).hexdigest()})

def layout_calls(text):
    # Public page constructors use literal coordinates. Do not compare all
    # numerical values: live state and callbacks need independent behavior tests.
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = text.replace('nui_', 'lv_').replace('NUI_', 'LV_')
    names = r'(?:box|label|chip|button|icon|lv_obj_set_pos|lv_obj_set_size|lv_obj_set_width|lv_obj_set_height)'
    return [re.sub(r'\s+', '', x) for x in re.findall(r'\b' + names + r'\([^;\n]*', text)]

ui_path = 'tab5/main/tab5_ui.c'
old_layout = layout_calls(original(ui_path))
new_layout = layout_calls((root / ui_path).read_text(encoding='utf-8'))
r = {'baseline': a.baseline, 'fonts': fonts,
     'layout_call_count': len(old_layout), 'layout_calls_identical': old_layout == new_layout,
     'passed': all(f['identical'] for f in fonts) and old_layout == new_layout}
s = json.dumps(r, indent=2)
if a.output:
    a.output.write_text(s + '\n', encoding='utf-8')
print(s)
raise SystemExit(0 if r['passed'] else 1)
