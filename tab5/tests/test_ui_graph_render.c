/* The legacy LVGL-only chart-widget demo is retired. Its replacement is
 * the full production native RGB565 suite in test_native_ui.c, including:
 * graph_5hz_regression: bounded interpolation, pending tail, failures/silence;
 * graph_arrays: all four source/render series and 30s/2min selection;
 * raster_overlay_audit: actual RGB565 flush pixels, opacity and curve colors;
 * graph_idle_regression: missing data, unchanged frames, theme/range/legend;
 * graph_motion_benchmark and live_unfiltered_regression in both themes.
 * Keeping this translation unit delegates manual C builds to that complete
 * suite instead of silently testing a removed LVGL fallback chart renderer.
 * Host command: python tab5/tests/run_ui_graph_render_test.py
 * Historical graph_5hz_raw/render captures are reproducible only from frozen
 * commit 369c7fc; current captures retain the full native suite's exact names.
 */
#include "test_native_ui.c"
