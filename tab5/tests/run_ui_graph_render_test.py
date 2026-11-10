"""Run the full production native RGB565 graph/UI regression replacement.

The former isolated LVGL fallback chart demo is retired. The full native suite
checks all four series, bounded 5Hz interpolation, missing/error/pending data,
30s/2min/10min selection, unfiltered numerics, range/legend/touch, idle renders,
opaque raster geometry and actual RGB565 flush samples, in both themes.
It also runs all action scenarios and retains their original screenshot names.
Usage: python tab5/tests/run_ui_graph_render_test.py [native runner options]
Historical graph-widget demo is available only in frozen commit 369c7fc.
"""
from run_native_ui_test import main

if __name__ == "__main__":
    raise SystemExit(main())
