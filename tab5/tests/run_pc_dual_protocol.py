"""Actual USB loop / authenticated TCP dispatch -> command -> queue -> owner.

Uses the production services core and SDK cJSON. Replaces OS, NVS and drivers;
does not access a port, socket, device, network or another workspace's cache.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from run_services_startup_guard import mask_c

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, default=ROOT/'tab5/build-host/pc-dual')
p.add_argument('--wire-out', type=Path, help='Optional actual JSON/REL CSV dumps; never changes firmware')
p.add_argument('--idf', type=Path, default=Path(os.environ.get(
    'IDF_PATH', 'C:/Projects/ClaudeExp/diplom_project_tab5/.tools/esp-idf')))
a = p.parse_args()
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=True)
source = (ROOT/'tab5/main/main.c').read_text(encoding='utf-8')
services = (ROOT/'tab5/main/tab5_services.c').read_text(encoding='utf-8')


def function(text, marker):
    masked = mask_c(text)
    start = text.index(marker)
    brace = masked.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    return text[start:end]


# Rename only the application flag: services has its independent real flag.
def app(text):
    return re.sub(r'(?<![.\w])recording\b', 'app_recording', text)


types = source[source.index('typedef struct {\n    tab5_ui_action_t action;'):
               source.index('static QueueHandle_t samples,')]
(out/'actions_generated.h').write_text(types, encoding='utf-8')
markers = ['static void load_workspace_config(', 'static bool save_workspace_config(',
           'static void load_relative_config(', 'static bool save_relative_config(',
           'static bool apply_dual_action_body(', 'static bool apply_dual_action(',
           'static void add_num(', 'static void status(',
           'static void relative_status_json(', 'static void process_dual_request(',
           'static bool dual_command(']
(out/'control_generated.h').write_text('\n'.join(app(function(source, m)) for m in markers), encoding='utf-8')
(out/'command_generated.h').write_text(function(source, 'static void command(void *unused,'), encoding='utf-8')
(out/'usb_generated.h').write_text('\n'.join(function(source, m) for m in
    ['static bool physical_record(', 'static void usb_reply(', 'static void ui_callback(', 'static void usb_task(']), encoding='utf-8')

# Execute the exact production main queue branch with a fake scheduler.
start = source.index('        while(xQueueReceive(actions,&event,0)==pdTRUE){')
end = source.index('            tab5_ui_action_t a=event.ui;', start)
(out/'owner_generated.h').write_text('static void run_owner(void){\n action_event_t event;\n'+source[start:end]+\
    'assert(!event.protocol); ui_events++;\n }}\n', encoding='utf-8')
assert 'tab5_services_config_t services={.command=command,.network=network};' in source
usb = function(source, 'static void usb_task(')
assert 'if(!tab5_services_line(1,line,true,usb_reply,NULL))command(NULL,line,usb_reply,NULL);' in usb
tcp = function(services, 'static void network_task(')
dispatch = re.search(r'else if\(!tab5_services_line\(c->owner,c->rx,true,tcp_reply,c\) && config.command\)config.command\(config.command_ctx,c->rx,tcp_reply,c\);', tcp)
assert dispatch and tcp.index('else if(!c->auth)') < dispatch.start()
(out/'tcp_generated.h').write_text('static void dispatch_tcp(test_client_t *c){\n'+
    'if(!c->auth){tcp_reply(c,"ERR auth");}\n'+dispatch[0]+'\n}\n', encoding='utf-8')
cjson = a.idf/'components/json/cJSON'
exe = out/'pc-dual.exe'
cmd = [shutil.which('gcc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
       '-Wno-unused-function', '-I'+str(ROOT/'tab5/main'), '-I'+str(ROOT/'tab5/tests'),
       '-I'+str(out), '-I'+str(cjson), str(ROOT/'tab5/tests/test_pc_dual_protocol.c'),
       str(ROOT/'tab5/main/tab5_relative.c'), str(ROOT/'tab5/main/tab5_workspace.c'),
       str(ROOT/'tab5/main/tab5_time.c'), str(cjson/'cJSON.c'), '-lbcrypt', '-lm', '-o', str(exe)]
build = subprocess.run(cmd, capture_output=True)
(out/'build.log').write_bytes(build.stdout+build.stderr)
if build.returncode:
    print((build.stdout+build.stderr).decode(errors='replace'))
    raise SystemExit(build.returncode)
env = os.environ.copy()
if a.wire_out:
    a.wire_out.resolve().mkdir(parents=True, exist_ok=True)
    env['TAB5_WIRE_DUMP_DIR'] = str(a.wire_out.resolve())
run = subprocess.run([str(exe)], cwd=out, capture_output=True, timeout=60, env=env)
(out/'test.log').write_bytes(run.stdout+run.stderr)
print((run.stdout+run.stderr).decode(errors='replace'))
report = {'pass': run.returncode == 0, 'compile_command': cmd,
          'sources': {str(x.relative_to(ROOT)): hashlib.sha256(x.read_bytes()).hexdigest()
                      for x in [ROOT/'tab5/main/main.c', ROOT/'tab5/main/tab5_services.c',
                                ROOT/'tab5/main/tab5_ui.h', Path(__file__),
                                ROOT/'tab5/tests/test_pc_dual_protocol.c']}}
(out/'result.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
raise SystemExit(run.returncode)
