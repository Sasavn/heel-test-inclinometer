# Frozen LVGL versus native host RGB565 benchmark

Baseline: 369c7fcd435ffd09631e49c0118e102c66ea4d75. Both -O2. Native coarse flag=0; primitive diagnostic flag=1 is separate.
Three alternating rounds, 120 warmup + 600 measured logical steps per scene/theme/run; 36000 raw measured rows.
QPC wall time includes update + logical input + layout/render completion + exact RGB565-to-RGB888 synchronous mock flush. Input generation, CSV/PPM/PNG/file IO excluded.
All percentiles are ms/logical step. A touch step includes press/release and may produce multiple display refreshes. No individual-display-frame p95 is inferred. Layout is not independently separable; pump combines input/layout/render work. Flush time overlaps total/pump. Hardware rotation/DSI/target speed: N/A.

| Scene | Theme | Logical steps / backend | LVGL display refreshes | Native display refreshes | LVGL median / p95 ms/step | Native median / p95 ms/step | Native / LVGL median |
|---|---|---:|---:|---:|---:|---:|---:|
| idle | dark | 1800 | 0 | 0 | 0.0062 / 0.0110 | 0.0098 / 0.0157 | 1.581x |
| idle | light | 1800 | 0 | 0 | 0.0080 / 0.0114 | 0.0101 / 0.0182 | 1.262x |
| angles | dark | 1800 | 1800 | 1800 | 0.7250 / 1.1073 | 1.2732 / 1.7993 | 1.756x |
| angles | light | 1800 | 1800 | 1800 | 0.9723 / 1.1752 | 1.2472 / 1.9218 | 1.283x |
| graph | dark | 1800 | 1800 | 1800 | 0.5083 / 1.7744 | 0.6512 / 1.7643 | 1.281x |
| graph | light | 1800 | 1800 | 1800 | 0.5931 / 1.9599 | 0.7384 / 2.0139 | 1.245x |
| modal | dark | 1800 | 1800 | 3600 | 0.2554 / 0.4457 | 0.7505 / 1.1245 | 2.939x |
| modal | light | 1800 | 1800 | 3600 | 0.3782 / 0.4777 | 0.7210 / 1.1618 | 1.906x |
| transition | dark | 1800 | 2520 | 3600 | 3.9416 / 5.6079 | 7.6480 / 11.0348 | 1.940x |
| transition | light | 1800 | 2520 | 3600 | 4.4100 / 5.7151 | 6.6544 / 10.3947 | 1.509x |

## Timing-mode verification

Coarse/instrumented: **equal**. See timing-mode-equivalence.json for per-case hashes and deterministic flush/render/primitive work counts.
This checks final case framebuffers and work counts; it does not compare every intermediate framebuffer.

## Memory accounting

LVGL reserved arena: 8,388,608 bytes, separate from its usable used-block current/peak metrics.
Native requested tracked payload excludes allocator headers/CRT overhead. allocation_live_blocks is CURRENT LIVE BLOCKS, not cumulative malloc calls. No per-frame allocation-call claim is possible.
External native display framebuffer: 1,843,200 bytes; plot: 757,120; two gauges: 160,000. Do not double-count as tracked engine payload.
Host mock sink: 3,686,400 bytes. Baseline static draw buffer: 512,000 bytes, 256,000 active for RGB565. Harness arrays/code/fonts/static UI state are outside allocation metrics.
Peaks are lifetime-scoped since init, not per-scene reset. The following columns deliberately have different allocation semantics.

| Scene | Theme | LVGL current usable blocks bytes | Native current tracked payload bytes | Native lifetime tracked peak bytes | Native live blocks |
|---|---|---:|---:|---:|---:|
| idle | dark | 148256 | 154761 | 154775 | 1926 |
| idle | light | 148264 | 154761 | 170836 | 1926 |
| angles | dark | 148248 | 154765 | 154781 | 1926 |
| angles | light | 148256 | 154765 | 170836 | 1926 |
| graph | dark | 144504 | 155006 | 155020 | 1942 |
| graph | light | 144512 | 155006 | 170836 | 1942 |
| modal | dark | 162864 | 170755 | 170836 | 2126 |
| modal | light | 162904 | 170755 | 170836 | 2126 |
| transition | dark | 144824 | 155357 | 170836 | 1942 |
| transition | light | 144808 | 155357 | 170836 | 1942 |

## Evidence

The original generated output is under `.tools/native-baseline/` in the local
workspace. The archive copies beside this report use the following filenames:

- [benchmark-raw.csv](benchmark-raw.csv): ordered per-logical-step measurements; the legacy frame column is the logical-step ordinal.
- [benchmark-results.json](benchmark-results.json) and [benchmark-aggregate.csv](benchmark-aggregate.csv): update/pump/flush/total ms/logical-step metrics, display refresh counts and explicitly amortized mean ms/refresh (not an individual-frame percentile).
- [benchmark-memory.json](benchmark-memory.json): reserved/current/peak/external accounting.
- [primitive-profile.json](primitive-profile.json): separate instrumented primitive calls/us/pixels.
- [timing-mode-equivalence.json](timing-mode-equivalence.json): checksums and draw-work equivalence.
- [benchmark-toolchain.json](benchmark-toolchain.json): compiler flags and source provenance; local paths are normalized.
- [frames.json](frames.json): hashes of the 98 native captures; [selected native screenshots](../img/native/). The untouched frozen reference captures remain in `.tools/native-baseline/frames/`, separate from benchmark scene captures.

Original names retained locally: `paired/raw_frames.csv`, `paired/results.json`,
`paired/aggregate.csv`, `paired/memory.json`, `profile/profiles.json`,
`build.json`, `build-native-coarse.json`, `build-native-instrumented.json`.

## Separate primitive diagnostic

Flag=1 diagnostic across 6,000 logical steps; excluded from speed comparison. Primitive timing scopes are not assumed exclusive.

| Primitive | Calls | Total us | Pixels |
|---|---:|---:|---:|
| shadow | 22076 | 3787702 | 12339120 |
| fill | 97064 | 1057645 | 2998811440 |
| label | 107438 | 639806 | 65845752 |
| border | 48476 | 248274 | 18601440 |
| line | 44862 | 216563 | 3432376 |
| arc | 13662 | 66104 | 808320 |
| image | 4158 | 31248 | 450710400 |
| triangle | 6720 | 7647 | 113280 |
