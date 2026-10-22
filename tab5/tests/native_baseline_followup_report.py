"""Offline-only report: no compiler, renderer, timing loop or image conversion."""
import argparse
import csv
import json
from pathlib import Path

import native_baseline_followup as bench


def report(root, gate):
    def read(name):
        return json.loads((root/name).read_text(encoding='utf-8'))
    aggregate=read('final/aggregate.json')['rows']
    coarse=read('final/profiles.json')['rows']
    memory=read('final/memory.json')['rows']
    lookup={(r['backend'],r['scene'],r['theme']):r for r in aggregate}
    rows=[]
    for scene in ('idle','angles','graph','modal','transition'):
        for theme in ('dark','light'):
            row=dict(scene=scene,theme=theme)
            for backend in bench.BACKENDS:
                item=lookup[(backend,scene,theme)]
                for key,value in item.items():
                    if key not in ('backend','scene','theme'):row[backend+'_'+key]=value
                p=[x for x in coarse if (x['backend'],x['scene'],x['theme'])==(backend,scene,theme)]
                # Coarse profile provides only a case-total clock, NOT a per-step
                # distribution. Never call this a median or infer LVGL pure draw.
                row[backend+'_engine_mean_ms_per_step']=sum(x['engine_render_us_excluding_flush'] for x in p)/1800000 if p else None
            rows.append(row)
    bench.save(root/'table.json',dict(rows=rows,units='Wall phases median/p95 ms per logical step; native engine mean ms per logical step excludes mock flush. LVGL engine mean unavailable.'))
    with (root/'table.csv').open('w',newline='',encoding='utf-8') as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    raw_count=sum(1 for _ in (root/'final/raw_samples.csv').open(encoding='utf-8'))-1
    assert raw_count==54000
    equivalence=[]
    for backend in ('native_c49','optimized'):
        for mode in ('profile','cache-profile'):
            p=f'{mode}/{backend}/timing-mode-equivalence.json'
            e=read(p);assert e['equal'] and len(e['checks'])==3
            equivalence.append(dict(path=p,equal=e['equal'],cases=30))
    rgb=[]
    for run in range(1,4):
        folder=root/'final'/f'round-{run}'
        r=bench.compare_frames(folder/'native_c49',folder/'optimized',10)
        assert r['equal'];rgb.append(r)
    bench.save(root/'native-baseline-optimized-rgb.json',dict(equal=True,checks=rgb,scope='Final ten scene captures per round, not every intermediate frame'))
    ram={}
    for backend in bench.BACKENDS:
        selected=[r for r in memory if r['backend']==backend]
        keys= ('lvgl_arena_reserved_bytes','lvgl_current_used_block_bytes','lvgl_process_peak_used_block_bytes','lvgl_live_blocks') if backend=='lvgl' else ('native_logical_payload_current_bytes','native_logical_payload_process_peak_bytes','allocation_live_blocks','native_allocation_failures_process')
        ram[backend]={k:dict(min=min(r[k] for r in selected),max=max(r[k] for r in selected)) for k in keys}
    bench.save(root/'ram-summary.json',dict(ranges=ram,notice='Native requested payload/current live blocks, not total RAM or cumulative calls. Lifetime peaks. Cache heap is a subset, not additive. LVGL arena reserved separate from used allocator blocks. Header/CRT overhead unknown. Target GDB ledger belongs to independent main worker.'))
    churn=[];cold=[]
    for backend in ('native_c49','optimized'):
        cache=read(f'cache-profile/{backend}/cache-stats.json')['rows']
        profiles=[json.loads(x[8:]) for x in (root/f'cache-profile/{backend}/native.log').read_text(encoding='utf-8').splitlines() if x.startswith('PROFILE ')]
        for c in cache:
            p=next(x for x in profiles if (x['scene'],x['theme'])==(c['scene'],c['theme']))
            r=dict(backend=backend,**c)
            r['diagnostic_engine_ms_per_step']=p['engine_render_us_excluding_flush']/600000
            r['diagnostic_fill_ms_per_step']=p['primitives'][0]['us']/600000
            r['diagnostic_fill_pixels_per_step']=p['primitives'][0]['pixels']/600
            for k,v in c.items():
                if k.endswith(('_hits','_misses','_evictions','_build_pixels')) and v is not None:r[k+'_per_step']=v/600
            churn.append(r)
        cold.append(dict(backend=backend,**read(f'cold/{backend}/results.json')))
    bench.save(root/'cache-churn-summary.json',dict(diagnostic_only=True,rows=churn))
    bench.save(root/'cold-summary.json',dict(diagnostic_only=True,backends=cold,notice='One fresh process per theme; no warmup. These are samples, not median/p95. Fine1/timing1 instrumentation overhead is active. Same RGB mock flush separately timed; production DSI/rotation/port scheduling absent.'))
    gate_data=json.loads(gate.read_text(encoding='utf-8'));assert gate_data['status']=='PASS'
    status=dict(status='DONE',quiet_timings_complete=True,raw_logical_samples=raw_count,
        source_commit_baseline=bench.COMMIT,source_sha256=read('prepared.json')['source_sha256'],
        native_c49_new_rgb_equal=True,mode_equivalence=equivalence,
        independent_98_gate=str(gate),independent_98_gate_status=gate_data['status'],
        cold_samples=4,hardware_timings=False)
    bench.save(root/'final-status.json',status)
    lines=['# Final 1.6.14 three-way benchmark','',
        'LVGL 369c7fc / native c49eacf (1.6.13) / optimized 0dee13b5 (1.6.14). All -O2; native release timing0/fine0, separate diagnostic timing1/fine1.',
        'Immutable c4 adapter/contract/helper for both native backends; callbacks, measurement updates, input and 5x40ms pump unchanged. Accepted visual reference remains native c4. Baseline c49 source is from git archive.',
        'Three balanced rounds: LVGL/c49/new; c49/new/LVGL; new/LVGL/c49. 120 warmup and 600 measured logical steps per case. 54,000 raw steps total.',
        'QPC host timing includes complete synchronous identical RGB565-to-RGB888 mock flush. Host firmware text 1.6.1 unchanged for all; actual firmware metadata 1.6.14-esp32 is not a host compiler input. Production port scheduling, DMA, rotation and DSI are NOT measured.',
        '', '|Scene/theme|LVGL median/p95|c49 median/p95|new median/p95|new/c49 median|new/LVGL median|Display frames L/C/N|',
        '|---|---:|---:|---:|---:|---:|---:|']
    for row in rows:
        v=[lookup[(b,row['scene'],row['theme'])] for b in bench.BACKENDS]
        lines.append('|'+row['scene']+'/'+row['theme']+'|'+'|'.join('{:.4f}/{:.4f}'.format(r['total_ms_p50'],r['total_ms_p95']) for r in v)+'|{:.3f}|{:.3f}|'.format(v[2]['total_ms_p50']/v[1]['total_ms_p50'],v[2]['total_ms_p50']/v[0]['total_ms_p50'])+'/'.join(str(r['display_frames']) for r in v)+'|')
    lines+=['','Times are ms/logical step, not ms/display frame. Table counts are across 1,800 logical steps/backend/case.','',
        '## Native engine excluding mock flush','',
        'Case-total coarse clocks divided by 1,800 steps: arithmetic MEANS, not medians/p95. Layout is not separately timed; engine render may resolve styles/layout. Update and input/pump are wall phases and include other work. LVGL pure engine timing is unavailable.','',
        '|Scene/theme|c49 engine mean|new engine mean|new/c49|','|---|---:|---:|---:|']
    for r in rows:
        a,z=r['native_c49_engine_mean_ms_per_step'],r['optimized_engine_mean_ms_per_step']
        lines.append('|'+r['scene']+'/'+r['theme']+'|{:.6f}|{:.6f}|{}|'.format(a,z,'{:.3f}'.format(z/a) if a else 'N/A'))
    lines+=['','## Separate diagnostic cache/fill work','',
        'Extra adapter changes only post-loop stats printf; it is not the timed comparison adapter. Primitive clocks may overlap; fine counters unavailable in release are never compared as zeros. All scene gauges, peaks/capacities and per-step hits/misses/build/evictions are in cache-churn-summary.json.','',
        '|Scene/theme/backend|Style misses/step|Layout misses/step|Fill pixels/step|Fill ms/step|','|---|---:|---:|---:|---:|']
    for r in churn:
        if r['scene'] in ('modal','transition'):
            lines.append('|'+r['scene']+'/'+r['theme']+'/'+r['backend']+'|{:.2f}|{:.2f}|{:.1f}|{:.6f}|'.format(r['style_cache_misses_per_step'],r['layout_cache_misses_per_step'],r['diagnostic_fill_pixels_per_step'],r['diagnostic_fill_ms_per_step']))
    lines+=['','## Cold first actual paint','',
        'Fresh process per backend/theme, no pump/input/ticks/render before the measured first draw. Asserted frames=0 before and frames=1 after. UI init/update is separate; before/after cache stats confirm actual cold build. One sample/theme is diagnostic, not a percentile estimate. Not included in 54,000 steady-state samples.','',
        '|Backend/theme|UI init/update ms|First draw incl mock sink ms|Mock sink ms|Engine excl sink ms|','|---|---:|---:|---:|---:|']
    for c in cold:
        for r in c['rows']:lines.append('|'+c['backend']+'/'+r['theme']+'|{:.4f}|{:.4f}|{:.4f}|{:.4f}|'.format(r['ui_init_update_ms'],r['first_render_with_mock_flush_ms'],r['mock_flush_ms'],r['engine_render_excluding_flush_ms']))
    lines+=['','## RAM accounting','',
        'Host ABI requested payload only; allocator headers/CRT overhead unknown. allocation_live_blocks is current, not cumulative calls. Peaks are process lifetime. No zero malloc/frame or target total-RAM claim.','',
        '|Backend|Metric|Min bytes/blocks|Max bytes/blocks|','|---|---|---:|---:|']
    for backend,metrics in ram.items():
        for key,r in metrics.items():lines.append(f'|{backend}|{key}|{r["min"]}|{r["max"]}|')
    lines+=['',
        'LVGL reserved arena (8,388,608 B) is not comparable to native current payload. External framebuffer 1,843,200 B, plot 757,120 B, gauges 160,000 B are separate from tracked payload. Host RGB888 sink 3,686,400 B is host-only. LVGL host static draw buffer 512,000 B (256,000 active RGB565) is also separate.',
        'Gradient static cache 2,600 B is separate. Shadow cache stats include 512 B static metadata; allocated shadow payload and object cache bytes are already INCLUDED in tracked heap and must not be added again. Actual per-scene cache bytes/peaks/capacities are in cache-profile/*/cache-stats.json. Independent target GDB/static ledger is owned by parent.',
        '', '## Fidelity and evidence','',
        '- c49/new final scene RGB: 30/30 equal, native-baseline-optimized-rgb.json. This does not cover all intermediate frames.',
        '- Standard and cache diagnostic modes for each native backend: 30/30 final RGB and coarse refresh/dirty-work equal per adapter; fine counters excluded from equivalence.',
        '- Independent Hooke 98-frame and targeted gate: '+str(gate)+'. Imported PASS evidence, not rerun by this worker.',
        '- final/raw_samples.csv: 54,000 ordered logical samples. final/aggregate.json: pooled wall phases. table.csv/table.json combine native coarse engine means.',
        '- final/memory.json, profiles.json; profile/*/native.log; cache-profile/*/cache-stats.json; cold/*/results.json.',
        '- optimized-source.json and baseline-c49/snapshot.json: immutable source hashes. build/*/*/build.json: compiler/flags/input hashes and mode probes.',
        '- final-status.json: DONE. Earlier pilots and 1.6.13 evidence unchanged.','']
    (root/'RESULTS.md').write_text('\n'.join(lines),encoding='utf-8')
    print('\n'.join(lines[:23]))
    print('DONE:',root/'final-status.json')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=bench.OUT/'final-0dee13b5')
    p.add_argument('--gate',type=Path,default=bench.ROOT/'.tools/native-1.6.14/final-20261023-005512-0dee13b5/FINAL_GATE.json')
    a=p.parse_args();report(a.root,a.gate)
