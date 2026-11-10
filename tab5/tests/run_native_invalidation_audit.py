"""Read-only host invalidation audit; generated binaries live in a temporary directory.
Run from repository root: python tab5/tests/run_native_invalidation_audit.py
No display, device, network or system-clock writes. Firmware sources are not changed.
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / 'tab5/main'
OUT = ROOT / '.tools/native-large'
BASE = '6660d0e'
COMMAND = 'python tab5/tests/run_native_invalidation_audit.py'
PROBE = r"""
#include <assert.h>
#include <stdio.h>
#include "UI_SOURCE"
#if CURRENT
#include "PORT_SOURCE"
#endif
static uint16_t fb[1280*720];
static void result(const char *name){
    nui_native_stats_t st;nui_native_profile_snapshot(&st);
    printf("%s frames=%llu dirty_pixels=%llu rects=%llu layout_evict=%llu style_evict=%llu\n",name,
        (unsigned long long)st.frames,(unsigned long long)st.dirty_pixels,
        (unsigned long long)st.dirty_rects,(unsigned long long)st.layout_cache_evictions,
        (unsigned long long)st.style_cache_evictions);
}
static void transition(const char *name,unsigned page){
    nui_native_render();nui_native_profile_reset();show_page(page);nui_native_render();result(name);
}
int main(void){
    assert(nui_native_init(1280,720,fb));tab5_ui_init(NULL,NULL);
    tab5_ui_state_t s={.light_theme=true,.now_known=true,.now_us=1000000,
        .datetime_known=true,.datetime={2026,10,26,12,30,0},
        .clock={.known=true,.time_valid=true,.battery_present=true,.battery_v=4.0}};
    snprintf(s.clock.datetime,sizeof(s.clock.datetime),"26.10.2026 12:30:00");
    tab5_measure_config_default(&s.measurement.config);
    for(unsigned i=0;i<2;i++)s.measurement.sensor[i]=(tab5_measure_sensor_t){
        .valid=true,.good=20,.timestamp_us=1000000,.x=1.25,.y=2.5,
        .decoded_x=1.25,.decoded_y=2.5,.roll_x=.1,.roll_y=.2,.calm_x=true,.calm_y=true};
    tab5_ui_update(&s);nui_native_render();nui_native_profile_reset();
    for(unsigned i=0;i<100;i++){s.now_us+=100000;tab5_ui_update(&s);nui_native_render();}
    result("main_static_100updates");
    transition("main_to_settings",SETTINGS);nui_native_profile_reset();
    for(unsigned i=0;i<100;i++){
        s.now_us+=100000;s.measurement.sensor[0].x+=.01;
        s.measurement.sensor[0].roll_x+=.001;s.capacity[0].free_bytes++;
        tab5_ui_update(&s);nui_native_render();
    }
    result("settings_hidden_measure_changes_100updates");
    transition("settings_to_network",NETWORK);
    transition("network_to_graph",GRAPH);
    transition("graph_to_main",MEASURE);
#if CURRENT
    tab5_native_port_input_reset();tab5_ui_input(TAB5_UI_INPUT_ROT,1,100);
    nui_native_render();nui_native_profile_reset();
    for(unsigned i=1;i<=100;i++){
        nui_native_tick(10);tab5_native_port_input_dispatch(100+i*10);nui_native_render();
    }
    result("focus_idle_100_real_tick_queue_polls");
#endif
    /* Use the action callback path, including immediate encoder paint, so an
       artificial delayed first paint is not misreported as a periodic redraw. */
    nui_event_t ev={.user_data=(void*)(intptr_t)A_TIME,.code=NUI_EVENT_CLICKED};
#if CURRENT
    ev.target=enc_button(A_TIME);
#endif
    event(&ev);nui_native_render();
    nui_area_t area;nui_obj_get_coords(u.keyboard,&area);nui_native_profile_reset();
    for(unsigned i=0;i<100;i++){
        keyboard_style(u.keyboard);nui_obj_get_coords(u.keyboard,&area);nui_native_render();
    }
    result("keyboard_style_100_same_theme");
    nui_native_profile_reset();
    for(unsigned i=0;i<100;i++){s.now_us+=100000;tab5_ui_update(&s);nui_native_render();}
    result("time_modal_real_action_static_100updates");
    nui_native_profile_reset();
    for(unsigned i=0;i<20;i++){
        s.now_us+=100000;
        for(unsigned j=0;j<2;j++){s.measurement.sensor[j].x+=.01;s.measurement.sensor[j].y+=.01;}
        tab5_ui_update(&s);nui_native_render();
    }
    result("time_modal_live_angles_20updates");
    tab5_ui_destroy();nui_native_deinit();return 0;
}
"""

def git(*args):
    return subprocess.check_output(['git',*args],cwd=ROOT)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run():
    cc = shutil.which('gcc')
    if not cc:
        raise SystemExit('gcc is required')
    sources = [MAIN / name for name in ('tab5_ui_graph.c','tab5_measure.c','tab5_time.c',
        'tab5_plot.c','tab5_level.c','native/native_ui.c','native/font_native_fallback14.c')]
    sources += sorted(MAIN.glob('font_tab5_*.c'))
    tracked = [MAIN/'tab5_ui.c',MAIN/'tab5_ui.h',MAIN/'tab5_native_port.c',
        MAIN/'tab5_native_port.h',*sources]
    before = {str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in tracked}
    results, logs = {}, []
    OUT.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='tab5-readonly-invalidation-',dir=OUT) as tmp:
        td = Path(tmp)
        baseline_ui = td/'baseline_ui.c'
        baseline_ui.write_bytes(git('show',f'{BASE}:tab5/main/tab5_ui.c'))
        for current,ui in [(0,baseline_ui),(1,MAIN/'tab5_ui.c')]:
            name = 'current' if current else 'baseline'
            code = PROBE.replace('UI_SOURCE',ui.as_posix()).replace('PORT_SOURCE',(MAIN/'tab5_native_port.c').as_posix())
            exe = td/f'{name}.exe'
            subprocess.run([cc,'-std=c11','-O2','-DNUI_PROFILE_FINE_COUNTERS=1',
                '-DTAB5_NATIVE_INPUT_ONLY=1',f'-DCURRENT={current}','-I'+str(MAIN),
                '-x','c','-',*[str(p) for p in sources],'-lm','-o',str(exe)],
                input=code.encode('utf-8'),check=True,cwd=ROOT)
            output = subprocess.check_output([str(exe)],cwd=ROOT,timeout=60).decode('utf-8').replace('\r\n','\n')
            logs.append(name.upper()+'\n'+output)
            results[name] = {}
            for line in output.splitlines():
                scenario,*values = line.split()
                results[name][scenario] = {key:int(value) for key,value in (v.split('=') for v in values)}
    after = {str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in tracked}
    assert before == after, 'Production inputs changed concurrently; repeat this audit after source freeze'
    zero = ['main_static_100updates','settings_hidden_measure_changes_100updates',
            'keyboard_style_100_same_theme','time_modal_real_action_static_100updates']
    for version in results:
        for scenario in zero:
            assert results[version][scenario]['frames'] == 0, (version,scenario)
            assert results[version][scenario]['dirty_pixels'] == 0, (version,scenario)
    assert results['current']['focus_idle_100_real_tick_queue_polls']['frames'] == 0
    assert results['current']['keyboard_style_100_same_theme']['layout_evict'] == 0
    assert results['current']['keyboard_style_100_same_theme']['style_evict'] == 0
    transitions = ['main_to_settings','settings_to_network','network_to_graph','graph_to_main']
    comparisons = {}
    for name in transitions:
        old,new = (results[version][name]['dirty_pixels'] for version in ('baseline','current'))
        comparisons[name] = {'baseline_dirty_pixels':old,'current_dirty_pixels':new,
            'reduction_pct':round(100*(old-new)/old,3)}
        assert results['current'][name]['frames'] == 1 and new < 1280*720
    native = 'tab5/main/native/native_ui.c'
    native_same = git('show',f'{BASE}:{native}').replace(b'\r\n',b'\n') == (ROOT/native).read_bytes().replace(b'\r\n',b'\n')
    report = {
        'status':'PASS','command':COMMAND,'generated_at_utc':datetime.now(timezone.utc).isoformat(),
        'head':git('rev-parse','HEAD').decode().strip(),'baseline_ref':BASE,
        'scope':'Host CPU framebuffer invalidation only; production sources unchanged; no device/network/GUI',
        'comparison_method':'Baseline UI source from git; both use the current superset API header and current linked support/font sources. Renderer baseline equality is checked separately.',
        'renderer_matches_baseline_ignoring_crlf':native_same,'fine_counters':1,'theme':'light',
        'results':results,'dirty_comparisons':comparisons,'source_sha256':before,
        'reproducer_sha256':sha(Path(__file__)),
        'limits':['Host does not measure PPA rotation, LCD transfer, input-driver latency or ESP32 CPU time.',
            'Static loops hold visible text and clock seconds fixed; expected carets/marquees are not disabled.',
            'Current focus-idle case advances real native ticks; other static loops exercise update without advancing native ticks.',
            'Live-angle changes behind a translucent modal are measured, not classified as a new regression.',
            'This is not the full encoder or partial-vs-full pixel equivalence suite; run tab5/tests/run_native_encoder.py for that.']}
    OUT.mkdir(parents=True,exist_ok=True)
    text = '\n'.join(logs)+'\nPASS: zero-frame scenarios and bounded transitions; firmware unchanged.\n'
    (OUT/'invalidation-audit.log').write_text(text,encoding='utf-8')
    (OUT/'invalidation-audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(text,end='')
    print('REPORT .tools/native-large/invalidation-audit.json')
    print('LOG .tools/native-large/invalidation-audit.log')

if __name__ == '__main__':
    run()
