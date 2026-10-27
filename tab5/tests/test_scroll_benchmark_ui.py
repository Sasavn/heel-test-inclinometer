"""Real Settings viewport and matte-cache wrappers; no buttons or devices."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "tab5/main"
HARNESS = r'''
#include "tab5_ui.c"
#include <assert.h>
static uint16_t fb[1280*720];
static unsigned actions;
static void action(const tab5_ui_action_t *a,void *ctx){(void)a;(void)ctx;actions++;}
int main(void){
 assert(nui_native_init(1280,720,fb));
 for(unsigned theme=0;theme<2;theme++){
  tab5_ui_init(action,NULL);tab5_ui_state_t state={.light_theme=theme};
  tab5_measure_config_default(&state.measurement.config);tab5_ui_update(&state);
  assert(tab5_ui_scroll_cache_enabled());
  tab5_ui_scroll_benchmark_position(137);show_page(NETWORK);e.index=2;
  e.network_offset=55;nui_obj_set_y(u.network_content,-55);
  unsigned char draft[sizeof(u.draft)];memcpy(draft,u.draft,sizeof(draft));
  tab5_ui_scroll_benchmark_state_t saved={0};int maximum;
  assert(tab5_ui_scroll_benchmark_begin(&saved,&maximum));
  assert(maximum==950&&saved.page==NETWORK&&saved.settings_offset==137);
  assert(u.page==SETTINGS&&!e.ring&&!actions);
  nui_area_t initial,position;nui_obj_get_coords(u.settings_content,&initial);
  for(int offset=0;offset<=maximum;offset+=16){
   tab5_ui_scroll_benchmark_position(offset);nui_obj_get_coords(u.settings_content,&position);
   assert(position.y1==initial.y1-offset&&e.settings_offset==offset);
  }
  tab5_ui_scroll_benchmark_position(99999);assert(e.settings_offset==950);
  tab5_ui_scroll_benchmark_position(-1);assert(e.settings_offset==0);
  assert(!memcmp(draft,u.draft,sizeof(draft))&&!actions);
  tab5_ui_set_scroll_cache_enabled(true);assert(tab5_ui_scroll_cache_enabled());
  tab5_ui_clear_scroll_cache();tab5_ui_scroll_benchmark_position(320);nui_native_render();
  tab5_ui_set_scroll_cache_enabled(false);assert(!tab5_ui_scroll_cache_enabled());
  nui_native_render();
  tab5_ui_scroll_benchmark_end(&saved);
  assert(!saved.valid&&u.page==NETWORK&&e.settings_offset==137&&e.network_offset==55&&e.index==2);
  assert(!memcmp(draft,u.draft,sizeof(draft))&&!actions);
  u.state.recording=true;assert(!tab5_ui_scroll_benchmark_begin(&saved,&maximum));u.state.recording=false;
  e.touch_down=true;assert(!tab5_ui_scroll_benchmark_begin(&saved,&maximum));e.touch_down=false;
  u.modal_kind=1;assert(!tab5_ui_scroll_benchmark_begin(&saved,&maximum));u.modal_kind=0;
  tab5_ui_destroy();
 }
 nui_native_deinit();assert(!actions);
 puts("Real UI benchmark PASS: both themes, viewport geometry/clamp, matte cache toggle/clear/render, restore page/scroll/focus, no semantic callbacks, recording/modal/touch refusal");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="tab5-scroll-ui-") as directory:
    tmp = Path(directory)
    (tmp / "test.c").write_text(HARNESS, encoding="utf-8")
    sources = [MAIN / "native/native_ui.c", MAIN / "native/font_native_fallback14.c"]
    sources += [MAIN / name for name in (
        "tab5_measure.c", "tab5_ui_graph.c", "tab5_time.c", "tab5_plot.c", "tab5_level.c")]
    sources += sorted(MAIN.glob("font_tab5_*.c"))
    exe = tmp / "test.exe"
    subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra",
                    "-I" + str(MAIN), str(tmp / "test.c"),
                    *map(str, sources), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
