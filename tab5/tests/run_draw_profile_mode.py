"""Validate production diagnostic JSON in coarse/counting/timed build modes."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
main = root / 'tab5/main'
cc = shutil.which('gcc')
if not cc:
    raise SystemExit('gcc is required')
with tempfile.TemporaryDirectory(prefix='tab5-profile-mode-') as td:
    for fine, timed in [(0,0),(1,0),(1,1),(0,1)]:
        exe = Path(td) / f'mode-{fine}-{timed}.exe'
        subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                        f'-DNUI_PROFILE_FINE_COUNTERS={fine}',f'-DNUI_PROFILE_PRIMITIVE_TIMING={timed}',
                        '-I'+str(main),str(root/'tab5/tests/test_draw_profile_mode.c'),
                        str(main/'tab5_draw_profile.c'),'-o',str(exe)],check=True)
        row = json.loads(subprocess.check_output([str(exe)],text=True))
        assert row == {'draw':'fill','us':1234 if timed else None,'calls':99 if fine else None,
                       'timing_enabled':bool(timed),'counters_enabled':bool(fine)}, row
        print(f'PASS fine={fine} timing={timed}: actual adapter modes/null versus measured values')
