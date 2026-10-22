# Final three-way native benchmark

120 warmup + 600 measured logical steps per case; 3 rounds; 5 scenes x 2 themes x 3 backends = 54,000 raw samples.
All backends -O2; native release timing=0 fine=0. Immutable v1 ignores unsupported fine gate and retains its counters.
Sequence: LVGL/v1/optimized; v1/optimized/LVGL; optimized/LVGL/v1.
Host QPC; complete identical RGB565-to-RGB888 mock flush. No rotation/DSI/hardware speed claim.
Host fixture firmware text 1.6.1 identical for all; actual firmware 1.6.13-esp32 PROJECT_VER is not a host input.

|Scene/theme|LVGL median/p95|v1 median/p95|optimized median/p95|optimized/v1 median|optimized/LVGL median|Display frames L/V/O|
|---|---:|---:|---:|---:|---:|---:|
|idle/dark|0.0059/0.0062|0.0078/0.0081|0.0080/0.0086|1.026|1.356|0/0/0|
|idle/light|0.0099/0.0111|0.0128/0.0131|0.0080/0.0087|0.625|0.808|0/0/0|
|angles/dark|0.5762/0.6494|0.9568/1.1251|0.4729/0.5070|0.494|0.821|1800/1800/1800|
|angles/light|1.0082/1.0291|1.7622/1.7972|0.8266/0.8964|0.469|0.820|1800/1800/1800|
|graph/dark|0.4533/1.0489|0.4825/1.0916|0.2967/0.9079|0.615|0.655|1800/1800/1800|
|graph/light|0.4009/1.8040|1.2827/1.9742|0.2748/1.6952|0.214|0.685|1800/1800/1800|
|modal/dark|0.2117/0.2426|0.5621/0.6419|0.2965/0.3348|0.527|1.401|1800/3600/1800|
|modal/light|0.3815/0.3927|1.0585/1.0836|0.5598/0.5738|0.529|1.467|1800/3600/1800|
|transition/dark|2.7394/4.3471|8.1873/10.9357|2.2257/2.8164|0.272|0.812|2520/3600/2520|
|transition/light|4.2566/5.4195|8.2694/10.9377|4.0665/5.1228|0.492|0.955|2520/3600/2520|

Times are ms/logical step, not ms/display frame. 1,800 measured logical steps per table row/backend. Refresh/display counts above include multiple refreshes per logical step.
Update includes UI/layout preparation; pump combines input, layout, draw and flush. Pure layout cannot be isolated. Flush copy overlaps total/pump. See aggregate.json for all phase median/p95 and raw_samples.csv for every ordered step.

## Diagnostic and fidelity evidence

- profile/optimized/timing-mode-equivalence.json: equal=True, 3 rounds x 10 final RGB captures plus refresh/dirty work. Fine primitive/cache counters excluded as unavailable in release, never compared as zero.
- cache-profile/optimized/timing-mode-equivalence.json: equal=True, 3 rounds x 10 final RGB captures plus refresh/dirty work. Fine primitive/cache counters excluded as unavailable in release, never compared as zero.
- Independent Hooke gate: .tools\native-opt\final-pack\20261022-231053-becf5ffae692\FINAL_GATE.json; status=PASS, exact_rgb_vs_immutable_v1=True, frames_per_mode=98. This is imported independent evidence, not a suite rerun by this worker.
- Cold first paint: unavailable/not run. Warmup excludes initial cache build costs; warmed-up timings cannot establish cold cost.

## Memory and cache accounting

Native allocation_live_blocks is current live blocks, not cumulative calls. Tracked bytes are requested payload; headers/CRT overhead unknown. No zero per-frame allocation assertion. LVGL reserved arena and usable block totals are separate from native current payload.
External framebuffer 1,843,200 B, plot 757,120 B, gauges 160,000 B; host RGB888 mock sink 3,686,400 B. These are separate from tracked allocator payload.
Gradient static cache 2,600 B separate; object/shadow dynamically allocated cache payload is already included in tracked heap and must not be added again. Shadow stats include 512 B static entry metadata; split that portion out. Cache peaks are process-lifetime peaks, not reset per scene.
Diagnostic cache values below come from a separately generated adapter with only extra post-loop printf; not timed comparison evidence. Fine=1/timing=1.

|Scene/theme|shadow bytes (incl static)|object cache bytes|gradient static bytes|shadow hits/misses/build pixels/evictions|style hits/misses/evictions|layout hits/misses/evictions|gradient hits/misses/build pixels/evictions|
|---|---:|---:|---:|---|---|---|---|
|idle/dark|4737|130872|2600|0/0/0/0|0/0/0|600/0/0|0/0/0/0|
|angles/dark|4737|130872|2600|2352/0/0/0|1654395/0/0|685812/0/0|597/0/0/0|
|graph/dark|4737|130872|2600|2352/0/0/0|493092/0/0|185552/0/0|0/0/0/0|
|modal/dark|10066|144648|2600|5400/0/0/0|1295100/610800/67200|569100/67200/67200|0/0/0/0|
|transition/dark|10066|130872|2600|3334/0/0/0|1087082/863520/65280|419125/64080/64080|240/0/0/0|
|idle/light|14291|130872|2600|0/0/0/0|0/0/0|600/0/0|0/0/0/0|
|angles/light|14291|130872|2600|2352/0/0/0|1654395/0/0|685812/0/0|597/0/0/0|
|graph/light|14291|130872|2600|2352/0/0/0|493092/0/0|185552/0/0|0/0/0/0|
|modal/light|19620|144648|2600|5400/0/0/0|1295100/610800/67200|569100/67200/67200|0/0/0/0|
|transition/light|19620|130872|2600|3334/0/0/0|1087082/863520/65280|419125/64080/64080|240/0/0/0|

All hit/miss/build/eviction totals are per 600 measured diagnostic logical steps. Divide by 600 for per-step values. Byte gauges are current; peak/capacity values remain in cache-stats.json.

## Paths

- final/raw_samples.csv: 54,000 ordered samples
- final/results.json and aggregate.json: per-run summaries and pooled phases
- final/memory.json and profiles.json: release memory and coarse work
- profile/optimized/native.log: standard diagnostic primitive breakdown
- cache-profile/optimized/cache-stats.json: extra cache diagnostic only
- optimized-source.json: frozen optimized main closure
- build/*/*/build.json: exact flags/compiler/ABI probes/source hashes
- final/status.json: complete/final_results=true
