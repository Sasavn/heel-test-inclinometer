"""Fail if a production IDF build retains LVGL components or linked symbols.

Usage: python tab5/tests/audit_native_link.py tab5/build [--nm path/to/nm]
Inspects actual resolved components, Ninja link inputs and ELF symbol table;
an unused download in managed_components is not a production dependency.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('build', type=Path)
p.add_argument('--nm')
p.add_argument('--output', type=Path)
a = p.parse_args()
b = a.build.resolve()
d = json.loads((b / 'project_description.json').read_text(encoding='utf-8'))
forbidden = re.compile(r'lvgl|esp_lvgl_port', re.I)
components = d.get('build_components', [])
component_hits = [s for s in components if forbidden.search(s)]
ninja = (b / 'build.ninja').read_text(encoding='utf-8')
link_lines = [s for s in ninja.splitlines() if s.startswith('build ') and '.elf:' in s]
link_hits = [s for s in link_lines if forbidden.search(s)]
nm = a.nm or shutil.which('riscv32-esp-elf-nm')
if not nm:
    root = Path(__file__).resolve().parents[2]
    found = sorted((root / '.tools/idf-tools/tools/riscv32-esp-elf').glob('*/riscv32-esp-elf/bin/riscv32-esp-elf-nm.exe'))
    if found:
        nm = str(found[-1])
if not nm:
    raise SystemExit('nm is required; pass --nm')
elf = b / d['app_elf']
symbols = subprocess.check_output([nm, '-a', str(elf)], text=True, encoding='utf-8')
symbol_hits = [s for s in symbols.splitlines() if re.search(r'\b(?:lv_|lvgl_|__wrap_lv_|__real_lv_)\w*$', s)]
result = {'build': b.name, 'project_version': d.get('project_version'),
          'component_count': len(components), 'forbidden_components': component_hits,
          'forbidden_link_inputs': link_hits, 'forbidden_symbols': symbol_hits,
          'passed': not (component_hits or link_hits or symbol_hits)}
text = json.dumps(result, indent=2, ensure_ascii=False)
if a.output:
    a.output.write_text(text + '\n', encoding='utf-8')
print(text)
raise SystemExit(0 if result['passed'] else 1)
