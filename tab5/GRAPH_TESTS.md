# Pure graph regression tests

Freeze: PASS, 2026-10-22. GCC host-only, actual `tab5_ui_graph.c`, actual state/measurement headers and actual LVGL headers. No LVGL runtime, renderer, GUI, COM, IDF build or hardware. `-Wall -Wextra -Werror`; executable timeout 20 seconds. Temporary executable is removed after the run.

```
python tab5/tests/test_ui_graph_run.py
python tab5/tests/test_ui_graph_run.py --source-root C:/Projects/ClaudeExp/diplom_project_tab5
```

Result: 14 groups, 44,661 checks, zero failures. Each group runs even if a preceding group fails; runner exit status is nonzero on any failure. LVGL's actual `LV_CHART_POINT_NONE` is compiled and statically checked as INT32_MAX; no duplicate mock constant.

## Production changes and reproduced failures

Ownership of `tab5_ui_graph.c/.h` was explicitly transferred to this task. Public API, ring capacities and sample rates remain unchanged. `now_us` advances the fixed grid; data is inserted into its own `timestamp_us / step` bucket, if retained. Older/repeated timestamps cannot replace newer measurements. Both sensor timestamps remain independent. Delayed first samples extend visible history only after all ring slots are initialized as gaps. Backward current time resets history; unknown current time leaves the entire state untouched.

Original repro 1: now=1,050,000 us, sample timestamp=990,000 us. Both fast and slow rings wrote into the current slot. Correct output places values in the previous slot, with a current-slot gap.

Original repro 2: after timestamp=1,000,040 us, a snapshot now=1,000,050 us supplied older sensor-0 timestamp=1,000,020 us. It overwrote the newer value in the same bucket. Correct output retains the newer value while independently accepting sensor 1's fresh timestamp.

The initial 2Hz fast-window test expectation `1140+(i+1)/5` had an off-by-one: retained sample indices are 1140 through 1199, occupying positions 4,9,...,299, so the correct expression is `1140+i/5`. This mathematical test correction preceded the production change; delayed-sample expectations were retained unchanged.

## Coverage

- Unknown time initially and after sampling is byte-for-byte inert; empty 30/120/600-second windows contain gaps for all four series.
- Duplicate timestamps do not update changed snapshot values or manufacture measurements in new buckets.
- Per-axis NaN, positive/negative infinity, values beyond +/-327.66 degrees, exact accepted range edges and rounding; invalid, no-good, zero-timestamp and future-timestamp samples.
- Offline/current gaps remain distinct from historical samples; unchanged/stale snapshots do not interpolate across a gap; reconnect uses new timestamps.
- Fast/slow bucket boundaries at 999,999 -> 1,000,000 us; multiple skipped fast buckets; newest sample wins within a shared bucket.
- First samples delayed by 20 seconds have precisely located points in all windows and every other slot remains a gap.
- Oldest retained buckets accepted; samples one microsecond into the preceding expired bucket rejected, separately for 30-second and 600-second rings. Exact 100ms/500ms sample ages belong to preceding buckets.
- Delayed new timestamps arriving after existing history fill their historical slots without shifting arrival-time slots or repeating on subsequent snapshots.
- Jumps at cap-1, cap and cap+1 for both rings, plus a large 3,999-second jump; old data expires, gaps remain and current measurements occupy the last slot. Backward time restarts both rings without residual data.
- Ten minutes at 2Hz, four distinct series: all 1,200 slow slots, the trailing 240 slow slots and the correctly sparse trailing 300 fast slots checked independently. Then 30 seconds at 10Hz: all fast slots and all 120/600-second mixed-history slots verified.
- 1,800 seconds at 10Hz repeatedly rotate both buffers. All four series in all three windows are checked in chronological order using absolute input sample indices, including value wrap-around.

The tests invoke production graph functions directly with fake snapshots and mathematical expectations; there is no test copy of graph selection/rotation code. Snapshot fixtures do not validate physical sensors, LVGL rendering or main-core synchronization.

## Source SHA-256 at freeze

- `tab5/main/tab5_ui_graph.c`: `daf0501d5fba10fab7c3e9884c48163578993317102b803760f88014241dfbe7`
- `tab5/main/tab5_ui_graph.h`: `855f6bbd1008f9a25ab1a87b70b699e50aa8719c06101784abe399e9df7cb79b`
- `tab5/tests/test_ui_graph.c`: `429b82e11f0618f0d8b73c0bf86a14ffb0da8111aaea26f3ab96fa4ee1796053`
- `tab5/tests/test_ui_graph_run.py`: `4878bbb6423ab0e6d3f175896768eeb06d3be79cdb7e0519d9ca2951b929b1f4`
- `tab5/main/tab5_ui.h`: `30245cfc299330c0cd4c3205854e391299e9863b4cc036a0ca064be520007410`
- `tab5/main/tab5_measure.h`: `50851dd1e10a146202834283a09b2f77561ee4475a1a71de626f77bf470f989e`
- `tab5/managed_components/lvgl__lvgl/src/widgets/chart/lv_chart.h`: `f207c6bcb934864b11cebf20eb11b733a10251da821683b44e445db8d7858886`

Main повторно запустил14/44661 на итоговом append-only UI header с typed action result; PASS.
