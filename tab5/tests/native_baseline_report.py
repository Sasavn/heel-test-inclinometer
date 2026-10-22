"""Post-process completed timing artifacts only; never renders or builds."""
import csv
import json
import math
from pathlib import Path

BASE=Path(__file__).resolve().parents[2]/'.tools/native-baseline'

def read(path):return json.loads(path.read_text(encoding='utf-8'))
def percentile(values,p):
    values=sorted(values)
    return values[max(0,math.ceil(len(values)*p)-1)]
def main():
    status=read(BASE/'paired/status.json')
    assert status.get('final_results'), 'No complete paired timing result'
    metadata=read(BASE/'paired/results.json')
    assert metadata['native_primitive_timing']==0
    groups={};combined=[]
    for path in sorted((BASE/'paired').glob('pair-*/*/*_samples.csv')):
        backend=path.parent.name;pair=int(path.parent.parent.name.split('-')[1])
        name=path.name.removesuffix('_samples.csv');scene,theme=name.rsplit('_',1)
        with path.open(newline='',encoding='utf-8') as f:
            rows=list(csv.DictReader(f))
        assert len(rows)==metadata['frames']
        group=groups.setdefault((backend,scene,theme),[])
        for row in rows:
            values={('logical_step' if k=='frame' else k):(float(v) if k.endswith('_ms') else int(v)) for k,v in row.items()}
            group.append(values);combined.append(dict(backend=backend,pair=pair,scene=scene,theme=theme,**values))
    assert len(groups)==20
    with (BASE/'paired/raw_frames.csv').open('w',newline='',encoding='utf-8') as f:
        writer=csv.DictWriter(f,fieldnames=list(combined[0]));writer.writeheader();writer.writerows(combined)
    aggregates=[]
    for (backend,scene,theme),rows in sorted(groups.items()):
        item=dict(backend=backend,scene=scene,theme=theme,samples=len(rows))
        assert len(rows)==metadata['frames']*metadata['runs']
        for metric in ['total_ms','update_ms','pump_ms','flush_copy_ms']:
            values=[row[metric] for row in rows]
            item[metric+'_median']=percentile(values,.5);item[metric+'_p95']=percentile(values,.95)
        for count in ['flushes','pixels','refreshes']:item[count]=sum(row[count] for row in rows)
        item['logical_steps']=len(rows)
        item['observed_display_refreshes']=item['refreshes']
        item['refreshes_per_logical_step']=item['refreshes']/len(rows)
        item['amortized_mean_total_ms_per_observed_refresh']=sum(row['total_ms'] for row in rows)/item['refreshes'] if item['refreshes'] else None
        item['percentile_unit']='ms/logical_step'
        aggregates.append(item)
    evidence=read(BASE/'timing-mode-equivalence.json')
    native_build=read(BASE/'build-native-coarse.json')
    summary=dict(status='complete',baseline_commit='369c7fcd435ffd09631e49c0118e102c66ea4d75',
                 warmup=metadata['warmup'],frames=metadata['frames'],rounds=metadata['runs'],raw_rows=len(combined),
                 percentile_method='nearest rank, pooled measured frames from three alternating rounds',
                 measured_logical_steps_per_case_run=metadata['frames'],warmup_logical_steps=metadata['warmup'],legacy_frames_arg_semantics='logical steps, not individually timed display frames',native_mode='coarse NUI_PROFILE_PRIMITIVE_TIMING=0',primitive_profile_mode='separate flag=1 diagnostic',
                 hardware_rotation_dsi='N/A',timing_mode_equivalence=evidence['status'],aggregates=aggregates,
                 native_source_sha256=native_build['source_sha256'])
    (BASE/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    with (BASE/'paired/aggregate.csv').open('w',newline='',encoding='utf-8') as f:
        writer=csv.DictWriter(f,fieldnames=list(aggregates[0]));writer.writeheader();writer.writerows(aggregates)
    lookup={(row['backend'],row['scene'],row['theme']):row for row in aggregates}
    lines=['# Frozen LVGL versus native host RGB565 benchmark','',
           'Baseline: 369c7fcd435ffd09631e49c0118e102c66ea4d75. Both -O2. Native coarse flag=0; primitive diagnostic flag=1 is separate.',
           f"Three alternating rounds, {metadata['warmup']} warmup + {metadata['frames']} measured logical steps per scene/theme/run; {len(combined)} raw measured rows.",
           'QPC wall time includes update + logical input + layout/render completion + exact RGB565-to-RGB888 synchronous mock flush. Input generation, CSV/PPM/PNG/file IO excluded.',
           'All percentiles are ms/logical step. A touch step includes press/release and may produce multiple display refreshes. No individual-display-frame p95 is inferred. Layout is not independently separable; pump combines input/layout/render work. Flush time overlaps total/pump. Hardware rotation/DSI/target speed: N/A.','',
           '| Scene | Theme | Logical steps / backend | LVGL display refreshes | Native display refreshes | LVGL median / p95 ms/step | Native median / p95 ms/step | Native / LVGL median |',
           '|---|---|---:|---:|---:|---:|---:|---:|']
    for scene in ['idle','angles','graph','modal','transition']:
        for theme in ['dark','light']:
            a=lookup['baseline',scene,theme];b=lookup['native',scene,theme]
            lines.append(f"| {scene} | {theme} | {a['logical_steps']} | {a['observed_display_refreshes']} | {b['observed_display_refreshes']} | {a['total_ms_median']:.4f} / {a['total_ms_p95']:.4f} | {b['total_ms_median']:.4f} / {b['total_ms_p95']:.4f} | {b['total_ms_median']/a['total_ms_median']:.3f}x |")
    lines += ['', '## Timing-mode verification','',f"Coarse/instrumented: **{evidence['status']}**. See timing-mode-equivalence.json for per-case hashes and deterministic flush/render/primitive work counts.",
              'This checks final case framebuffers and work counts; it does not compare every intermediate framebuffer.','',
              '## Memory accounting','',
              'LVGL reserved arena: 8,388,608 bytes, separate from its usable used-block current/peak metrics.',
              'Native requested tracked payload excludes allocator headers/CRT overhead. allocation_live_blocks is CURRENT LIVE BLOCKS, not cumulative malloc calls. No per-frame allocation-call claim is possible.',
              'External native display framebuffer: 1,843,200 bytes; plot: 757,120; two gauges: 160,000. Do not double-count as tracked engine payload.',
              'Host mock sink: 3,686,400 bytes. Baseline static draw buffer: 512,000 bytes, 256,000 active for RGB565. Harness arrays/code/fonts/static UI state are outside allocation metrics.',
              'Peaks are lifetime-scoped since init, not per-scene reset. The following columns deliberately have different allocation semantics.','',
              '| Scene | Theme | LVGL current usable blocks bytes | Native current tracked payload bytes | Native lifetime tracked peak bytes | Native live blocks |',
              '|---|---|---:|---:|---:|---:|']
    memory=read(BASE/'paired/memory.json')['rows']
    for scene in ['idle','angles','graph','modal','transition']:
        for theme in ['dark','light']:
            a=[r for r in memory if r['backend']=='baseline' and r['scene']==scene and r['theme']==theme]
            b=[r for r in memory if r['backend']=='native' and r['scene']==scene and r['theme']==theme]
            cell=lambda rows,key: str(rows[0][key]) if len({r[key] for r in rows})==1 else str(min(r[key] for r in rows))+'..'+str(max(r[key] for r in rows))
            lines.append(f"| {scene} | {theme} | {cell(a,'lvgl_current_used_block_bytes')} | {cell(b,'native_logical_payload_current_bytes')} | {cell(b,'native_logical_payload_process_peak_bytes')} | {cell(b,'allocation_live_blocks')} |")
    lines += ['', '## Evidence','', '- paired/raw_frames.csv: ordered per-logical-step measurements; per-case C CSV legacy frame column is the logical-step ordinal.',
              '- paired/results.json and aggregate.csv: update/pump/flush/total ms/logical-step metrics, display refresh counts and explicitly amortized mean ms/refresh (not an individual-frame percentile).',
              '- paired/memory.json: reserved/current/peak/external accounting.',
              '- profile/profiles.json: separate instrumented primitive calls/us/pixels.',
              '- timing-mode-equivalence.json: checksums and draw-work equivalence.',
              '- build.json, build-native-coarse.json, build-native-instrumented.json: compiler flags and source provenance.',
              '- frames/: 98 untouched frozen reference captures, separate from benchmark scene captures.']
    (BASE/'RESULTS.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print('Report complete:',len(combined),'raw rows; modes',evidence['status'])
    for row in lines[10:22]:print(row)
if __name__=='__main__':main()
