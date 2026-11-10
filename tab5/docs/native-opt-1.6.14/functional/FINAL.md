Final native1.6.14 gate PASS.

Renderer 0dee13b5fd02652656d6315830813445fe339676145f2bf34c4994308e642992
Header 1dd8d7d3b216033490db7db9ac872404c53003490c444ea2a89b82c04134a590
UI b9d1434572c66b7e25de6c326224f62f8af2490b6f84402811aaa01d3d821c25

Both modes98 exact RGB vs immutable parent v1 and each other, geometry/semantics0, all inputs PASS. All targeted groups fine0/fine1 PASS. Atomic-role18 cases per mode: sole MAIN zero allocations, unknown styles/selectors conservative sequential fallback; inherited/color/font/pad/border and warm/cold geometry/full framebuffer exact. Lifetime64 and dirty-page40 both flags PASS. Gradient/profile/CLI helpers and custom callbacks/clipped4bpp/span regressions PASS.

Sanitizer runtime unavailable; no sanitizer or hardware validation claimed. CPU tests complete. Run read-only verifier: python verify_final_pack.py
