"""Compile the production renderer's ESP allocation branch with host caps.

No IDF build or device access. Exercise the real UI lifetime fixture and prove
that retained allocations never consume/fall back to internal calloc memory.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "tab5/main"

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_heap_caps.h"
static unsigned caps_calls, internal_calls;
static bool fail_psram;
void *heap_caps_calloc(size_t count, size_t bytes, unsigned caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    caps_calls++;
    return fail_psram ? NULL : calloc(count, bytes);
}
static void *forbidden_internal_calloc(size_t count, size_t bytes) {
    (void)count; (void)bytes; internal_calls++; return NULL;
}
#define calloc forbidden_internal_calloc
#define ESP_PLATFORM 1
#include "native/native_ui.c"
#undef ESP_PLATFORM
#undef calloc
#include "tab5_ui.h"
static uint16_t frame[1280*720];
int main(void) {
    nui_native_stats_t stats;
    assert(nui_native_init(1280,720,frame));
    for(unsigned cycle=0;cycle<16;cycle++) {
        tab5_ui_init(NULL,NULL);
        tab5_ui_state_t state={.light_theme=(cycle&1)!=0,
                              .now_known=true,.now_us=100000000};
        tab5_measure_config_default(&state.measurement.config);
        tab5_ui_update(&state);
        nui_native_render();
        nui_native_profile_snapshot(&stats);
        assert(!stats.allocation_failures && stats.allocation_count>100);
        if(!cycle)printf("Real UI retained peak: %zu bytes / %zu live blocks\n",
                         stats.allocation_peak,stats.allocation_count);
        tab5_ui_destroy();
        nui_native_deinit();
        nui_native_profile_snapshot(&stats);
        assert(!stats.allocation_count && !stats.allocation_bytes);
        if(cycle<15)assert(nui_native_init(1280,720,frame));
    }
    assert(caps_calls>1000 && !internal_calls);
    /* Exercise an owned framebuffer too; same allocator and cleanup. */
    assert(nui_native_init(32,32,NULL));
    nui_native_deinit();
    nui_native_profile_snapshot(&stats);
    assert(!stats.allocation_count && !stats.allocation_bytes);
    /* Failed PSRAM must be reported, never silently use internal RAM. */
    unsigned before=caps_calls;
    fail_psram=true;
    assert(!nui_native_init(1280,720,frame));
    nui_native_profile_snapshot(&stats);
    assert(stats.allocation_failures==1 && !stats.allocation_count);
    assert(!internal_calls && caps_calls==before+1);
    fail_psram=false;
    assert(nui_native_init(32,32,NULL));
    nui_native_deinit();
    nui_native_profile_snapshot(&stats);
    assert(!stats.allocation_count && !stats.allocation_bytes);
    printf("ESP renderer PSRAM: %u capability allocations; zero internal calls; "
           "PSRAM failure/retry, 16 real UI lifecycles and owned framebuffer PASS\n", caps_calls);
    (void)forbidden_internal_calloc;
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="tab5-renderer-psram-") as directory:
        tmp = Path(directory)
        (tmp / "esp_heap_caps.h").write_text(
            "#pragma once\n#include <stddef.h>\n"
            "#define MALLOC_CAP_SPIRAM (1u << 10)\n"
            "#define MALLOC_CAP_8BIT (1u << 2)\n"
            "void *heap_caps_calloc(size_t, size_t, unsigned);\n",
            encoding="utf-8")
        (tmp / "test.c").write_text(HARNESS, encoding="utf-8")
        sources = [MAIN / "native/font_native_fallback14.c"]
        sources += [MAIN / name for name in (
            "tab5_ui.c", "tab5_measure.c", "tab5_ui_graph.c", "tab5_time.c",
            "tab5_plot.c", "tab5_level.c")]
        sources += sorted(MAIN.glob("font_tab5_*.c"))
        exe = tmp / "test.exe"
        subprocess.run([
            "gcc", "-std=c11", "-O2", "-Wall", "-Wextra",
            "-I" + str(tmp), "-I" + str(MAIN),
            "-I" + str(ROOT / "tab5/tests"), str(tmp / "test.c"),
            *map(str, sources), "-lm", "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
