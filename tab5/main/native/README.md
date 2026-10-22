# Native Tab5 renderer

Independent C11 retained RGB565 toolkit. `native_ui.c` includes no LVGL headers
and links no LVGL runtime. Converted font/layout data is borrowed, not executed
by an external widget engine. `THIRD_PARTY_FONTS.md` documents font provenance.

## Platform contract

1. Call `nui_native_init(width, height, framebuffer)` before building UI.
   The caller owns a supplied native-endian RGB565 framebuffer; passing NULL
   requests an internally owned buffer. Stride is exactly `width` pixels.
2. Serialize UI updates, `nui_native_tick(elapsed_ms)`, touch and render calls.
3. `nui_native_touch(x, y, pressed)` uses framebuffer coordinates. Retained
   capture sends press/release/click, focus and switch events. Keyboard controls
   preserve press versus CLICK_TRIG release, NO_REPEAT, 400/100ms repeat.
4. `nui_native_render()` returns the number of rendered dirty rectangles. Zero
   means idle. An optional synchronous flush callback receives each rectangle,
   a pointer to its top-left framebuffer pixel, and full-framebuffer stride.
   `nui_native_get_dirty()` copies the last rendered rectangles.
5. Delete the UI before `nui_native_deinit()`. External fonts/styles/images and
   chart arrays must remain valid while retained objects borrow them.

## Rendering and profiling

Objects retain geometry, exact local/shared style selectors, text and events.
Children start at their parent's border plus padding. Text uses original 4bpp
bitmap metrics/cmaps/class kerning, primary-font fallback baselines, letter
spacing, word wrapping and last-row DOT ellipsis. Textareas expose their actual
content bounds with `nui_native_textarea_get_text_bounds()`; no fake child nodes
are created for tests. Circular marquee has an object-local epoch and initial
1000ms pause. Cursor blink restarts on edits/focus and uses 400ms phases.

Dirty rectangles are clipped/merged. An opaque descendant may cull earlier
paint underneath its entire dirty rectangle; later siblings/overlays are still
painted. Custom primitives use integer RGB565 blending and subpixel boundary
coverage. The renderer does not allocate a supersampled frame or mask layers.

`nui_native_set_clock_cb()` supplies monotonic microseconds on target. Host
Windows uses QueryPerformanceCounter; portable C11 uses timespec_get when
available, otherwise the C clock. `nui_native_profile_snapshot()` reports real
submitted primitive calls/pixels, timing, dirty pixels and frames. render_us
excludes flush callback/transport time. Primitive timings are exclusive of
other primitive types. `NUI_PROFILE_PRIMITIVE_TIMING=1` is the default. Set it
to 0 for fair outer benchmarks: begin/end make no clock calls, primitive calls
and pixels still count, and primitive us fields are intentionally disabled.
Allocation counters track toolkit heap only; borrowed framebuffer, UI-owned
plot/level buffers and static font data are excluded. Reset preserves live
allocation accounting and its peak.

## Checks

`checks/selftest.c` verifies press/release, disabled controls, timer deletion,
400/100ms keyboard repeat, UTF-8 editing, deletion from events, idle frames and
64 allocation-stable widget lifetimes. Production-wide host tests are owned
by the separate `tab5/tests/run_native_ui_test.py` harness.

The final renderer integration run produced all 98 fixture captures with no
layout guardrail failures and exact semantic geometry (10934 records). Its
functional suite passed. Own AA/shadow rasterization is not claimed to be
bit-identical to the frozen reference. `host-check/` is temporary generated
host verification output, not firmware source or runtime data.
