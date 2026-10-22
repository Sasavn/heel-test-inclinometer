"""Compatibility entry point for the production native RGB565 UI suite.

All old actions, both themes and 98 screenshot names live in test_native_ui.c.
--rgb565 remains accepted; RGB565 is now mandatory. No managed LVGL is used.
Historical LVGL reproduction (immutable 369c7fc):
    python tab5/tests/native_baseline.py capture
Exact diagnostic geometry/font reference (frozen sources remain unchanged):
    python tab5/tests/run_native_ui_test.py --prepare-reference
"""
from run_native_ui_test import main

if __name__ == "__main__":
    raise SystemExit(main())
