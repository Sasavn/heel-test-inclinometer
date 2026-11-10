"""Current exact app_main prefix with the existing production service harness.

The old 1.6.15 gate rejects even 1.7.19's error-only console message and its
entry stubs predate clock/backlight/dual config. This adds boundary stubs;
it does not rewrite the production prefix or the frozen historical gate.
"""
import argparse,importlib.util,pathlib,shutil,subprocess
root=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--out',type=pathlib.Path,default=root/'.tools/dual-startup');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('old_guard',root/'tab5/tests/run_services_startup_guard.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
text=(root/'tab5/main/main.c').read_text(encoding='utf-8');prefix=guard.function(text,'app_main').split('bool trial_confirmed')[0]
(a.out/'startup_entry_generated.h').write_text('static void run_actual_app_startup(void){\n'+prefix+'\n}\n',encoding='utf-8')
shim=(root/'tab5/tests/test_services_entry_chain.c').read_text(encoding='utf-8')
shim=shim.replace('#include "startup_entry_generated.h"',r'''
static uint32_t backlight_timeout_s;
static uint8_t brightness_pct=60;
static const char *esp_err_to_name(int error){(void)error;return "injected display error";}
static void load_relative_config(void){} /* Real NVS function tested by run_dual_runtime.py. */
static void tab5_time_set_offset(int offset){CHECK(offset==180);}
static bool tab5_native_port_backlight_configure(uint32_t timeout,uint8_t brightness){
 CHECK(timeout==0&&brightness==60);return true;
}
#include "startup_entry_generated.h"
''')
c=a.out/'entry.c';c.write_text(shim,encoding='utf-8');exe=(a.out/'entry.exe').resolve()
cmd=[shutil.which('gcc') or 'gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'tab5/tests'),'-I'+str(root/'tab5/main'),'-I'+str(a.out.resolve()),str(c),'-lbcrypt','-lm','-o',str(exe)]
subprocess.run(cmd,check=True)
r=subprocess.run([str(exe)],cwd=a.out,capture_output=True);(a.out/'test.log').write_bytes(r.stdout+r.stderr);print(r.stdout.decode(errors='replace'),end='');raise SystemExit(r.returncode)
