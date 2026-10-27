"""Theme buttons/encoder queue backend state; never nest a full UI update."""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / 'tab5/main'
HARNESS = r'''
#include "theme_ui.c"
#include <assert.h>
static uint16_t fb[1280*720];
static unsigned actions;
static tab5_ui_action_t queued;
static void enqueue(const tab5_ui_action_t *a,void *ctx){
 (void)ctx;assert(a->kind==TAB5_UI_SETTINGS);queued=*a;actions++;
}
static void check_request(bool old,bool target,unsigned before){
 if(actions!=before+1||queued.light_theme!=target)fprintf(stderr,"actions=%u before=%u old=%d target=%d queued=%d page=%u offset=%d\n",actions,before,old,target,queued.light_theme,u.page,e.settings_offset);
 assert(actions==before+1&&queued.light_theme==target);
 assert(u.light==old&&u.state.light_theme==old); /* backend has not applied it */
 assert(cfg_equal(&queued.config,&u.state.measurement.config));
 assert(queued.brightness_pct==0&&!queued.timezone_set);
}
int main(void){
 assert(nui_native_init(1280,720,fb));
 for(unsigned initial=0;initial<2;initial++){
  tab5_ui_init(enqueue,NULL);tab5_ui_state_t state={.light_theme=initial,.brightness_pct=80};
  tab5_measure_config_default(&state.measurement.config);tab5_ui_update(&state);
  show_page(SETTINGS);nui_native_render();
  for(unsigned n=0;n<16;n++){
   bool old=state.light_theme,target=!old;unsigned before=actions;
   tab5_ui_scroll_benchmark_position(950);
   nui_area_t a;nui_obj_get_coords(u.theme_btn[target?0:1],&a);
   int x=(a.x1+a.x2)/2,y=(a.y1+a.y2)/2;
   tab5_ui_touch(x,y,true,100+n*300);tab5_ui_touch(x,y,false,110+n*300);
   check_request(old,target,before);
   /* A rejected/delayed backend snapshot must not leave an optimistic palette. */
   tab5_ui_update(&state);assert(u.light==old);
   state.light_theme=target;tab5_ui_update(&state);assert(u.light==target);
   nui_native_render();
   before=actions;old=target;target=!old;
   e.ring=true;e.level=ENC_EDIT;e.edit=8;e.last_ms=200+n*300;
   tab5_ui_input(TAB5_UI_INPUT_ROT,target?-1:1,201+n*300);
   check_request(old,target,before);
   state.light_theme=target;tab5_ui_update(&state);assert(u.light==target);
   nui_native_render();
  }
  /* Pending settings must still be sent, without applying the local draft. */
  u.pending=true;u.sent=state.measurement.config;u.sent.frequency_hz=20;
  enc_action(state.light_theme?A_DARK:A_LIGHT);
  assert(queued.config.frequency_hz==20&&u.state.measurement.config.frequency_hz!=20);
  tab5_ui_destroy();
 }
 nui_native_deinit();puts("Theme action PASS: touch+encoder both directions, 64 backend-confirmed transitions, delayed/rejected snapshot, pending config, native cached render");
}
'''
with tempfile.TemporaryDirectory(prefix='tab5-theme-') as directory:
    tmp=Path(directory)
    ui=(subprocess.check_output(['git','show','91997e9:tab5/main/tab5_ui.c'],cwd=ROOT)
        if '--previous' in sys.argv else (MAIN/'tab5_ui.c').read_bytes())
    (tmp/'theme_ui.c').write_bytes(ui)
    (tmp/'test.c').write_text(HARNESS,encoding='utf-8')
    sources=[MAIN/'native/native_ui.c',MAIN/'native/font_native_fallback14.c']
    sources += [MAIN/name for name in ('tab5_measure.c','tab5_ui_graph.c','tab5_time.c','tab5_plot.c','tab5_level.c')]
    sources += sorted(MAIN.glob('font_tab5_*.c'))
    exe=tmp/'test.exe'
    subprocess.run(['gcc','-std=c11','-O2','-I'+str(MAIN),str(tmp/'test.c'),*map(str,sources),'-lm','-o',str(exe)],check=True)
    result=subprocess.run([str(exe)])
    if '--previous' in sys.argv:
        assert result.returncode!=0,'Regression did not fail on previous code'
        print('Previous 1.7.17 rejected as expected: optimistic nested theme update')
    else:
        result.check_returncode()
