"""Read native renderer type sizes from target ELF debug info, without a target.

Never runs an inferior, connects to a port, or loads user/ELF GDB scripts.
These are static type sizes, not measured target heap/RSS usage.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('elf', type=Path)
p.add_argument('--gdb', type=Path)
p.add_argument('--output', type=Path)
a = p.parse_args()
gdb = str(a.gdb) if a.gdb else shutil.which('riscv32-esp-elf-gdb')
if not gdb:
    candidates = sorted((root/'.tools/idf-tools/tools/riscv32-esp-elf-gdb').glob('*/riscv32-esp-elf-gdb/bin/riscv32-esp-elf-gdb.exe'))
    if candidates:
        gdb = str(candidates[-1])
if not gdb:
    raise SystemExit('Target GDB is required; pass --gdb')
elf = a.elf.resolve()
fields = {
    'object_bytes':'sizeof(struct nui_obj)',
    'style_entry_bytes':'sizeof(struct nui_style_entry)',
    'style_reference_bytes':'sizeof(style_ref)',
    'event_handler_bytes':'sizeof(handler)',
    'timer_bytes':'sizeof(struct nui_timer)',
    'profile_stats_bytes':'sizeof(nui_native_stats_t)',
    'renderer_context_static_bytes':"sizeof('native_ui.c'::n)",
    'object_style_cache_bytes':'sizeof(((struct nui_obj*)0)->main_cache)',
    'object_coordinate_cache_bytes':'sizeof(((struct nui_obj*)0)->coords_cache)',
    'shadow_cache_table_static_bytes':"sizeof('native_ui.c'::shadow_cache)",
    'gradient_cache_static_bytes':"sizeof('native_ui.c'::gradient_cache)",
}
required = set(fields)-{'object_style_cache_bytes','object_coordinate_cache_bytes','shadow_cache_table_static_bytes','gradient_cache_static_bytes'}
commands = ['set auto-load off','file "'+elf.as_posix()+'"']
for key, expression in fields.items():
    commands += [f'printf "NATIVE_BUDGET {key}=%llu\\n", (unsigned long long)({expression})']
# A command file avoids MinGW GDB's non-CRT parsing of nested argv quotes.
with tempfile.NamedTemporaryFile(mode='w',encoding='utf-8',suffix='.gdb',
                                 prefix='native-layout-',dir=root/'.tools',delete=False) as f:
    command_file = Path(f.name)
    f.write('\n'.join(commands)+'\n')
try:
    run = subprocess.run([gdb,'-nx','-nh','-batch','-iex','set auto-load off','-x',str(command_file)],
                         stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=90)
finally:
    command_file.unlink()
stdout = run.stdout.decode('utf-8',errors='replace')
sizes = {key:int(value) for key,value in re.findall(r'NATIVE_BUDGET (\w+)=(\d+)',stdout)}
missing = sorted(required-sizes.keys())
if missing:
    raise SystemExit('Missing required target types: '+str(missing)+'\n'+run.stderr.decode('utf-8',errors='replace'))
result = {'scope':'Static DWARF type sizes; no inferior or hardware connection',
          'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),
          'bytes':sizes,'optional_fields_unavailable':sorted(set(fields)-sizes.keys()),
          'accounting':'Cache fields are included in object_bytes; stats are included in renderer_context_static_bytes. External buffers, heap allocator overhead and allocated cache data are not counted here.'}
text = json.dumps(result,indent=2)
if a.output:
    a.output.write_text(text+'\n',encoding='utf-8')
print(text)
