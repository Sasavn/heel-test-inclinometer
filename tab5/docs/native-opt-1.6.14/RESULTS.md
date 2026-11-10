# Final 1.6.14 three-way benchmark

LVGL 369c7fc / native c49eacf (1.6.13) / optimized 0dee13b5 (1.6.14). All -O2; native release timing0/fine0, separate diagnostic timing1/fine1.
Immutable c4 adapter/contract/helper for both native backends; callbacks, measurement updates, input and 5x40ms pump unchanged. Accepted visual reference remains native c4. Baseline c49 source is from git archive.
Three balanced rounds: LVGL/c49/new; c49/new/LVGL; new/LVGL/c49. 120 warmup and 600 measured logical steps per case. 54,000 raw steps total.
QPC host timing includes complete synchronous identical RGB565-to-RGB888 mock flush. Host firmware text 1.6.1 unchanged for all; actual firmware metadata 1.6.14-esp32 is not a host compiler input. Production port scheduling, DMA, rotation and DSI are NOT measured.

|Scene/theme|LVGL median/p95|c49 median/p95|new median/p95|new/c49 median|new/LVGL median|Display frames L/C/N|
|---|---:|---:|---:|---:|---:|---:|
|idle/dark|0.0058/0.0060|0.0081/0.0089|0.0081/0.0134|1.000|1.397|0/0/0|
|idle/light|0.0060/0.0102|0.0080/0.0087|0.0081/0.0085|1.013|1.350|0/0/0|
|angles/dark|0.5707/0.6368|0.4710/0.5074|0.4567/0.5086|0.970|0.800|1800/1800/1800|
|angles/light|0.6589/1.0227|0.4770/0.6625|0.4580/0.5646|0.960|0.695|1800/1800/1800|
|graph/dark|0.3975/1.0237|0.2556/0.8889|0.2456/0.8785|0.961|0.618|1800/1800/1800|
|graph/light|0.5481/1.5864|0.3686/1.2640|0.3678/1.1988|0.998|0.671|1800/1800/1800|
|modal/dark|0.2111/0.2291|0.2959/0.3308|0.1917/0.2082|0.648|0.908|1800/1800/1800|
|modal/light|0.2244/0.3567|0.3347/0.5261|0.2042/0.3372|0.610|0.910|1800/1800/1800|
|transition/dark|2.6569/3.5591|2.1873/2.7745|2.1690/2.7465|0.992|0.816|2520/2520/2520|
|transition/light|3.0314/4.0757|2.6587/3.7109|2.5954/3.7076|0.976|0.856|2520/2520/2520|

Times are ms/logical step, not ms/display frame. Table counts are across 1,800 logical steps/backend/case.

## Native engine excluding mock flush

Case-total coarse clocks divided by 1,800 steps: arithmetic MEANS, not medians/p95. Layout is not separately timed; engine render may resolve styles/layout. Update and input/pump are wall phases and include other work. LVGL pure engine timing is unavailable.

|Scene/theme|c49 engine mean|new engine mean|new/c49|
|---|---:|---:|---:|
|idle/dark|0.000000|0.000000|N/A|
|idle/light|0.000000|0.000000|N/A|
|angles/dark|0.091713|0.077783|0.848|
|angles/light|0.100224|0.079566|0.794|
|graph/dark|0.033487|0.027007|0.806|
|graph/light|0.042292|0.034027|0.805|
|modal/dark|0.144253|0.037941|0.263|
|modal/light|0.176595|0.045663|0.259|
|transition/dark|0.782039|0.738393|0.944|
|transition/light|0.925292|0.857737|0.927|

## Separate diagnostic cache/fill work

Extra adapter changes only post-loop stats printf; it is not the timed comparison adapter. Primitive clocks may overlap; fine counters unavailable in release are never compared as zeros. All scene gauges, peaks/capacities and per-step hits/misses/build/evictions are in cache-churn-summary.json.

|Scene/theme/backend|Style misses/step|Layout misses/step|Fill pixels/step|Fill ms/step|
|---|---:|---:|---:|---:|
|modal/dark/native_c49|1018.00|112.00|321686.0|0.043333|
|transition/dark/native_c49|1439.20|106.80|1836721.5|0.184415|
|modal/light/native_c49|1018.00|112.00|321686.0|0.086310|
|transition/light/native_c49|1439.20|106.80|1836721.5|0.197015|
|modal/dark/optimized|26.00|0.00|142136.0|0.018855|
|transition/dark/optimized|47.00|0.00|1788505.4|0.174917|
|modal/light/optimized|26.00|0.00|142136.0|0.024552|
|transition/light/optimized|47.00|0.00|1788505.4|0.217585|

## Cold first actual paint

Fresh process per backend/theme, no pump/input/ticks/render before the measured first draw. Asserted frames=0 before and frames=1 after. UI init/update is separate; before/after cache stats confirm actual cold build. One sample/theme is diagnostic, not a percentile estimate. Not included in 54,000 steady-state samples.

|Backend/theme|UI init/update ms|First draw incl mock sink ms|Mock sink ms|Engine excl sink ms|
|---|---:|---:|---:|---:|
|native_c49/dark|1.3841|4.4331|2.4400|1.9890|
|native_c49/light|1.2723|4.3175|2.4429|1.8720|
|optimized/dark|1.3264|4.0791|2.2326|1.8440|
|optimized/light|1.2217|4.3611|2.5495|1.8100|

## RAM accounting

Host ABI requested payload only; allocator headers/CRT overhead unknown. allocation_live_blocks is current, not cumulative calls. Peaks are process lifetime. No zero malloc/frame or target total-RAM claim.

|Backend|Metric|Min bytes/blocks|Max bytes/blocks|
|---|---|---:|---:|
|lvgl|lvgl_arena_reserved_bytes|8388608|8388608|
|lvgl|lvgl_current_used_block_bytes|144504|162904|
|lvgl|lvgl_process_peak_used_block_bytes|159240|165768|
|lvgl|lvgl_live_blocks|2614|2904|
|native_c49|native_logical_payload_current_bytes|289858|334511|
|native_c49|native_logical_payload_process_peak_bytes|289858|334592|
|native_c49|allocation_live_blocks|1927|2130|
|native_c49|native_allocation_failures_process|0|0|
|optimized|native_logical_payload_current_bytes|289858|334511|
|optimized|native_logical_payload_process_peak_bytes|289858|334592|
|optimized|allocation_live_blocks|1927|2130|
|optimized|native_allocation_failures_process|0|0|

LVGL reserved arena (8,388,608 B) is not comparable to native current payload. External framebuffer 1,843,200 B, plot 757,120 B, gauges 160,000 B are separate from tracked payload. Host RGB888 sink 3,686,400 B is host-only. LVGL host static draw buffer 512,000 B (256,000 active RGB565) is also separate.
Gradient static cache 2,600 B is separate. Shadow cache stats include 512 B static metadata; allocated shadow payload and object cache bytes are already INCLUDED in tracked heap and must not be added again. Actual per-scene cache bytes/peaks/capacities are in cache-profile/*/cache-stats.json. Independent target GDB/static ledger is owned by parent.

## Fidelity and evidence

- c49/new final scene RGB: 30/30 equal, native-baseline-optimized-rgb.json. This does not cover all intermediate frames.
- Standard and cache diagnostic modes for each native backend: 30/30 final RGB and coarse refresh/dirty-work equal per adapter; fine counters excluded from equivalence.
- Independent Hooke 98-frame and targeted gate: C:\Projects\ClaudeExp\diplom_project_tab5\.tools\native-1.6.14\final-20261023-005512-0dee13b5\FINAL_GATE.json. Imported PASS evidence, not rerun by this worker.
- final/raw_samples.csv: 54,000 ordered logical samples. final/aggregate.json: pooled wall phases. table.csv/table.json combine native coarse engine means.
- final/memory.json, profiles.json; profile/*/native.log; cache-profile/*/cache-stats.json; cold/*/results.json.
- optimized-source.json and baseline-c49/snapshot.json: immutable source hashes. build/*/*/build.json: compiler/flags/input hashes and mode probes.
- final-status.json: DONE. Earlier pilots and 1.6.13 evidence unchanged.
