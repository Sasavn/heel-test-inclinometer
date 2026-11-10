"""Three-way benchmark. Stage1 evidence is read-only; timing release is internal coordination.

prepare freezes/verifies c49; build never executes timed scenes. Final run requires
an explicit new timing-hold.json with hold=false, plus an optimized source snapshot.
All adapters/helpers/contracts come from immutable c4cf2cf, including c49 and optimized runs. Accepted RGB reference remains c4.
New output root .tools/native-optimization-1.6.14; prior evidence is read-only.
"""
import argparse
import concurrent.futures
import csv
import hashlib
import json
import math
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / '.tools/native-optimization-1.6.14'
V1 = OUT / 'baseline-c49'
HARNESS = OUT / 'harness-c4/tab5/tests'
LV = ROOT / '.tools/native-baseline/snapshot'
COMMIT = 'c49eacfc5a48119afc7f158a8637e9d8ebe00457'
BACKENDS = ('lvgl', 'native_c49', 'optimized')
SHARED_GATE = ROOT / '.tools/native-opt/BENCHMARK_GATE'
TIMING_HOLD = OUT / 'timing-hold.json'
OWNS_SHARED_GATE = False  # Set only by the final coordinator after TIMINGREADY.


def require_timing_ready(provisional=False):
    if provisional:
        return
    hold = TIMING_HOLD
    if (not hold.exists() or json.loads(hold.read_text(encoding='utf-8')).get('hold', True)
            or SHARED_GATE.exists() and not OWNS_SHARED_GATE):
        raise SystemExit('Internal timing HOLD: require timing-hold.json hold=false AND absence of .tools/native-opt/BENCHMARK_GATE. Await coordination signal; no additional approval needed.')


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def save(p, value):
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(value, indent=2), encoding='utf-8')


def inventory(root):
    return {p.relative_to(root).as_posix(): sha(p) for p in sorted(root.rglob('*')) if p.is_file()}


def verify(root, manifest):
    m = json.loads(manifest.read_text(encoding='utf-8'))
    assert inventory(root) == m['files'], 'Snapshot content mismatch: ' + str(root)
    return m


def prepare():
    V1.mkdir(parents=True, exist_ok=True)
    archive, snap, manifest = V1/'source.tar', V1/'snapshot', V1/'snapshot.json'
    if not manifest.exists():
        assert not snap.exists(), 'Refuse to adopt an unverified partial snapshot'
        subprocess.run(['git', 'archive', '--format=tar', '--output='+str(archive), COMMIT,
                        'tab5/main', 'tab5/tests'], cwd=ROOT, check=True)
        snap.mkdir()
        with tarfile.open(archive) as t:
            t.extractall(snap, filter='data')
        save(manifest, dict(commit=COMMIT, archive_sha256=sha(archive), files=inventory(snap)))
    m = verify(snap, manifest)
    assert m['commit'] == COMMIT and sha(archive) == m['archive_sha256']
    harness_manifest=json.loads((HARNESS.parents[1]/'manifest.json').read_text(encoding='utf-8'))
    assert harness_manifest['commit']=='c4cf2cf0410d89e588b908525a2ea288e18ebec1'
    assert inventory(HARNESS)==harness_manifest['files'], 'Immutable c4 cadence/helper closure mismatch'
    OUT.mkdir(parents=True, exist_ok=True)
    if not (OUT/'timing-hold.json').exists():
        save(OUT/'timing-hold.json', dict(hold=True, reason='Internal agent source-freeze/build coordination gate; already authorized. Await new timing ready signal.'))
    # Pin stage1 evidence without changing any existing result or raw CSV.
    if not (OUT/'stage1-evidence.json').exists():
        base = ROOT/'.tools/native-baseline'
        files = {str(p.relative_to(ROOT)): sha(p) for folder in ('paired', 'profile')
                 for p in sorted((base/folder).rglob('*')) if p.is_file()}
        save(OUT/'stage1-evidence.json', dict(files=files))
    print('Verified immutable native c49:', len(m['files']), 'files')


def freeze_optimized():
    prepare()
    dest = OUT/'optimized-source'
    assert not dest.exists(), 'Candidate already frozen; use a new output root for a new candidate'
    src = ROOT/'tab5/main'
    before = inventory(src)
    shutil.copytree(src, dest/'tab5/main')
    assert before == inventory(src) == inventory(dest/'tab5/main'), 'Source changed during snapshot'
    save(OUT/'optimized-source.json', dict(files=inventory(dest), origin='working tree main closure', git_head=subprocess.check_output(['git','rev-parse','HEAD'], cwd=ROOT, text=True).strip()))


def prepare_cache_adapter(main, destination):
    """Generate diagnostics only: extra printf AFTER the original timed loop.

    Missing API fields become JSON null, never a fabricated measured zero.
    Immutable v1 helper, contract, input, callbacks and timing loop are untouched.
    """
    original=HARNESS/'native_baseline_native.c'
    source=original.read_text(encoding='utf-8')
    header=(main/'native/native_ui.h').read_text(encoding='utf-8')
    fields=('shadow_cache_hits shadow_cache_misses shadow_cache_build_pixels '
        'shadow_cache_evictions shadow_cache_bytes shadow_cache_peak_bytes '
        'shadow_cache_capacity_bytes shadow_cache_static_bytes '
        'style_cache_hits style_cache_misses style_cache_evictions '
        'object_cache_bytes object_cache_peak_bytes object_cache_bytes_per_object '
        'layout_cache_hits layout_cache_misses layout_cache_evictions '
        'skipped_state_invalidations clear_pixels_skipped '
        'gradient_cache_hits gradient_cache_misses gradient_cache_build_pixels '
        'gradient_cache_evictions gradient_cache_bytes gradient_cache_peak_bytes '
        'gradient_cache_capacity_bytes').split()
    import re
    available=[name for name in fields if re.search(r'\b'+name+r'\b',header)]
    text='CACHE {"scene":"%s","theme":"%s","logical_steps":%u,"diagnostic_only":true'
    arguments=['nb_names[scene]','theme?"light":"dark"','frames']
    for name in fields:
        text+=',"'+name+'":'+('%llu' if name in available else 'null')
        if name in available:arguments.append('(unsigned long long)stats.'+name)
    text+='}\n'
    extra='\n    /* Diagnostic-only extra output; outside all measured loops. */\n    printf('+json.dumps(text)+','+','.join(arguments)+');\n'
    anchor='nui_native_stats_t stats;nui_native_profile_snapshot(&stats);'
    assert source.count(anchor)==1
    generated=source.replace(anchor,anchor+extra)
    assert generated.replace(extra,'')==source
    destination.mkdir(parents=True,exist_ok=True)
    output=destination/'native_baseline_cache_diagnostic.c'
    if output.exists():assert output.read_text(encoding='utf-8')==generated
    else:output.write_text(generated,encoding='utf-8')
    save(destination/'adapter-manifest.json',dict(diagnostic_only=True,
        immutable_adapter_sha256=sha(original),generated_sha256=sha(output),
        target_header_sha256=sha(main/'native/native_ui.h'),available_fields=available,
        unavailable_fields=[x for x in fields if x not in available],
        change='Only post-loop CACHE printf after existing profile snapshot; no pump/input/callback/measurement changes',
        semantics='Hits/misses/build/evictions follow engine profile-reset semantics; byte gauges current, peaks lifetime. Fine counters require diagnostic fine1. Cache payload is a subset of tracked heap where allocated through mem_alloc; do not add it twice. Static cache bytes are separate. No cold-first-paint measurement included.'))
    return output


def build(backend, timing=0, cache_diagnostic=False):
    prepare()
    tests = HARNESS
    if backend == 'lvgl':
        # Full frozen LVGL dependency and original production source, not installed LVGL.
        old = json.loads((ROOT/'.tools/native-baseline/snapshot.json').read_text(encoding='utf-8'))
        assert all(sha(LV/p) == h for p, h in old['files'].items())
        main = LV/'tab5/main'; lvgl = LV/'tab5/managed_components/lvgl__lvgl'
        flags = ['-DLV_CONF_SKIP','-DLV_COLOR_DEPTH=16','-DLV_USE_LOG=0','-DLV_MEM_SIZE=8388608',
                 '-DLV_USE_OS=0','-DLV_USE_DRAW_SW=1','-DLV_USE_DRAW_SW_ASM=0',
                 '-DNB_FROZEN_TEST="'+(LV/'tab5/tests/test_ui.c').as_posix()+'"', '-I'+str(lvgl), '-I'+str(LV/'tab5/tests')]
        sources = sorted((lvgl/'src').rglob('*.c')) + [tests/'native_baseline_bench.c']
        deps = sorted(lvgl.rglob('*.h')) + [LV/'tab5/tests/test_ui.c']
    else:
        if backend == 'native_c49':
            main = V1/'snapshot/tab5/main'
        else:
            verify(OUT/'optimized-source', OUT/'optimized-source.json')
            main = OUT/'optimized-source/tab5/main'
        flags = ['-DNUI_COLOR_DEPTH=16','-DNUI_PROFILE_PRIMITIVE_TIMING='+str(timing),
                 '-DNUI_PROFILE_FINE_COUNTERS='+str(timing),
                 '-DNB_NATIVE_TEST="'+(tests/'test_native_ui.c').as_posix()+'"']
        sources = [tests/'native_baseline_native.c', main/'native/native_ui.c', main/'native/font_native_fallback14.c']
        deps = [tests/'test_native_ui.c']
    flags = ['-std=c11','-O2', *flags, '-I'+str(main), '-I'+str(tests)]
    sources += [main/p for p in ('tab5_ui.c','tab5_measure.c','tab5_ui_graph.c','tab5_time.c','tab5_plot.c','tab5_level.c')]
    sources += sorted(main.glob('font_tab5_*.c'))
    if cache_diagnostic:
        assert backend!='lvgl' and timing==1, 'Extra cache adapter is native diagnostic only'
        sources[0]=prepare_cache_adapter(main,OUT/'cache-diagnostic-adapter'/backend)
    deps += sorted(main.rglob('*.h')) + [tests/'native_baseline_contract.h']
    hashes = {str(p): sha(p) for p in sources+deps}
    cc = shutil.which('gcc')
    if not cc:
        raise RuntimeError('gcc required')
    version = subprocess.check_output([cc,'--version'], text=True).splitlines()[0]
    fingerprint = hashlib.sha256(json.dumps([flags, version, hashes], sort_keys=True).encode()).hexdigest()
    cache = OUT/'build'/backend/fingerprint[:20]; cache.mkdir(parents=True, exist_ok=True)
    def compile_one(p):
        obj = cache/(hashlib.sha256(str(p).encode()).hexdigest()+'.o')
        if not obj.exists():
            subprocess.run([cc,*flags,'-MMD','-MF',str(obj)+'.d','-c',str(p),'-o',str(obj)], check=True)
        return obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_one, sources))
    exe = cache/'adapter.exe'; rsp = cache/'link.rsp'
    rsp.write_text('\n'.join('"'+str(p).replace('\\','/')+'"' for p in [*objects,'-lm','-o',exe]), encoding='utf-8')
    subprocess.run([cc,'@'+str(rsp)], check=True)
    assert all(sha(p)==h for p,h in hashes.items())
    fine_supported = backend!='lvgl' and 'NUI_PROFILE_FINE_COUNTERS' in (main/'native/native_ui.h').read_text(encoding='utf-8')
    release_probe = None
    if backend!='lvgl':
        # Probe actual frozen headers with the exact target compiler/flags. This
        # is not a timed UI run. sizeof reflects compiler ABI padding as well.
        gradient = main/'native/native_gradient.h'
        probe = cache/'mode-memory-probe.c'
        probe.write_text('#include <stdio.h>\n#include "native/native_ui.h"\n'
            + ('#include "native/native_gradient.h"\n' if gradient.exists() else '')
            + '#ifndef NUI_PROFILE_FINE_COUNTERS\n#define NUI_PROFILE_FINE_COUNTERS -1\n#endif\n'
            + 'int main(void){printf("%d %d %zu\\n",NUI_PROFILE_PRIMITIVE_TIMING,NUI_PROFILE_FINE_COUNTERS,'
            + ('sizeof(nui_gradient_cache_t)' if gradient.exists() else '(size_t)0') + ');return 0;}\n',encoding='utf-8')
        probe_exe=cache/'mode-memory-probe.exe'
        subprocess.run([cc,*flags,str(probe),'-o',str(probe_exe)],check=True)
        actual_timing,actual_fine,gradient_bytes=map(int,subprocess.check_output([str(probe_exe)],text=True).split())
        assert actual_timing==timing and actual_fine==timing
        release_probe=dict(actual_timing_macro=actual_timing,actual_fine_macro=actual_fine,
            fine_gate_supported=fine_supported,
            gradient_cache_struct_bytes=gradient_bytes,
            gradient_static_cache_bytes=gradient_bytes if gradient.exists() and 'nui_gradient_cache_t' in (main/'native/native_ui.c').read_text(encoding='utf-8') else None,
            notice='Static gradient storage is separate from tracked heap payload; struct size is compiler-measured. Static instance integration must also be verified in frozen engine. Verify frozen API support independently for every backend.')
    meta = dict(backend=backend, primitive_timing=timing if backend!='lvgl' else None,
                fine_counters_requested=timing if backend!='lvgl' else None,
                fine_counters_supported=fine_supported,
                fine_counters_effective=(timing if fine_supported else 1) if backend!='lvgl' else None,
                mode='diagnostic' if timing else 'release',
                extra_cache_diagnostic_adapter=cache_diagnostic,
                frozen_header_mode_memory_probe=release_probe,
                counter_overhead_notice='Native c49 and optimized both support fine counters gating; release explicitly uses fine0/timing0.',
                flags=flags, compiler=cc, compiler_version=version, source_sha256=hashes,
                harness_root=str(tests), host=platform.platform(), exe=str(exe), hardware='N/A')
    save(cache/'build.json', meta)
    return exe, meta


def run(args):
    prepare()
    require_timing_ready(args.provisional)
    assert args.frames >= 100 and (args.rounds == 3 or args.provisional and args.rounds == 1)
    target = OUT/('provisional' if args.provisional else 'final')
    assert not target.exists(), 'Never overwrite existing measurements'
    builds = {b: build(b) for b in BACKENDS}
    target.mkdir(parents=True)
    save(target/'status.json', dict(status='running', final_results=False))
    rows=[]; memory=[]; profiles=[]; sequence=[]
    for round_index in range(args.rounds):
        order = BACKENDS[round_index:]+BACKENDS[:round_index]
        for backend in order:
            require_timing_ready(args.provisional)
            exe, meta = builds[backend]
            assert all(sha(p)==h for p,h in meta['source_sha256'].items())
            out=target/('round-'+str(round_index+1))/backend; out.mkdir(parents=True)
            result=subprocess.run([str(exe),str(args.warmup),str(args.frames),str(out)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
            (out/'native.log').write_bytes(result.stdout); result.check_returncode()
            require_timing_ready(args.provisional)
            assert all(sha(p)==h for p,h in meta['source_sha256'].items())
            count=0
            for line in result.stdout.decode('utf-8').splitlines():
                if line.startswith('{'): value=json.loads(line); dest=rows; count+=1
                elif line.startswith('MEMORY '): value=json.loads(line[7:]); dest=memory
                elif line.startswith('PROFILE '): value=json.loads(line[8:]); dest=profiles
                else: continue
                value.update(backend=backend, round=round_index+1); dest.append(value)
            assert count==10
            save(out/'run.json', dict(build=meta, warmup=args.warmup, logical_steps=args.frames,
                provisional=args.provisional, final_rgb_sha256={p.name:sha(p) for p in sorted(out.glob('*.ppm'))}))
            sequence.append(dict(round=round_index+1, backend=backend))
    save(target/'results.json', dict(provisional=args.provisional, sequence=sequence, rows=rows,
        units='ms/logical step, not ms/display frame; refreshes count actual display frames',
        phases='update and input/pump/layout/render combined; flush conversion overlaps total/pump',
        cadence='unchanged immutable c4cf2cf adapters: 5x40ms pump; press/release each pump',
        warmup=args.warmup, logical_steps=args.frames, hardware='N/A'))
    save(target/'memory.json', dict(rows=memory,
        separate_static_cache_evidence={b:m['frozen_header_mode_memory_probe'] for b,(_,m) in builds.items() if b!='lvgl'},
        notice='allocation_live_blocks is current, not cumulative calls. Payload excludes headers/CRT and external buffers. Reserved LVGL arena and gradient static cache are separate. No zero per-frame allocation claim.'))
    save(target/'profiles.json', dict(rows=profiles, notice='Release requests primitive timing=0 and fine counters=0. Native c49 and optimized both support the fine flag. Disabled primitive/cache hit counters are unavailable, not measured zero. Cache byte accounting remains available.'))
    aggregate(target)
    save(target/'status.json', dict(status='complete', final_results=not args.provisional))


def aggregate(target):
    groups={}
    path=target/'raw_samples.csv'
    with path.open('w', newline='', encoding='utf-8') as f:
        writer=None
        for p in sorted(target.glob('round-*/*/*_samples.csv')):
            for row in csv.DictReader(p.open(newline='', encoding='utf-8')):
                scene,theme=p.name.removesuffix('_samples.csv').rsplit('_',1)
                row=dict(backend=p.parent.name, round=p.parent.parent.name, scene=scene, theme=theme, **row)
                if writer is None: writer=csv.DictWriter(f, fieldnames=list(row)); writer.writeheader()
                writer.writerow(row)
                groups.setdefault((p.parent.name,scene,theme),[]).append(row)
    result=[]
    for key, values in sorted(groups.items()):
        item=dict(zip(('backend','scene','theme'),key)); item['logical_steps']=len(values)
        item['display_frames']=sum(int(v['refreshes']) for v in values)
        for name in ('total_ms','update_ms','pump_ms','flush_copy_ms'):
            ordered=sorted(float(v[name]) for v in values)
            for q in (50,95): item[name+'_p'+str(q)]=ordered[math.ceil(len(ordered)*q/100)-1]
        result.append(item)
    save(target/'aggregate.json', dict(rows=result, units='ms/logical step'))


def compare_frames(reference, candidate, expected):
    """Offline decoded RGB equality; never executes UI or changes input cadence."""
    from PIL import Image
    def images(folder):
        files=list(folder.glob('*.ppm')) or list(folder.glob('*.png'))
        return {p.stem:p for p in files}
    a,b=images(reference),images(candidate)
    assert len(a)==len(b)==expected and a.keys()==b.keys(), 'Frame inventory mismatch'
    rows=[]
    for name in sorted(a):
        with Image.open(a[name]) as x, Image.open(b[name]) as y:
            x,y=x.convert('RGB'),y.convert('RGB')
            rows.append(dict(name=name, equal=x.size==y.size and x.tobytes()==y.tobytes(),
                reference_rgb_sha256=hashlib.sha256(x.tobytes()).hexdigest(),
                candidate_rgb_sha256=hashlib.sha256(y.tobytes()).hexdigest()))
    return dict(equal=all(r['equal'] for r in rows), count=expected, rows=rows,
                reference=str(reference), candidate=str(candidate), scope='All supplied final captured RGB images; not every intermediate rendered framebuffer')


def profile(args):
    prepare()
    require_timing_ready(args.provisional)
    assert args.backend!='lvgl'
    cache_diagnostic=getattr(args,'cache_diagnostic',False)
    exe,meta=build(args.backend,1,cache_diagnostic)
    dest=OUT/(('provisional-' if args.provisional else '')+('cache-profile' if cache_diagnostic else 'profile'))/args.backend
    assert not dest.exists(), 'Never overwrite profile evidence'
    dest.mkdir(parents=True)
    require_timing_ready(args.provisional)
    result=subprocess.run([str(exe),str(args.warmup),str(args.frames),str(dest)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=300)
    (dest/'native.log').write_bytes(result.stdout);result.check_returncode()
    require_timing_ready(args.provisional)
    assert all(sha(p)==h for p,h in meta['source_sha256'].items())
    save(dest/'run.json',dict(build=meta, warmup=args.warmup,logical_steps=args.frames,provisional=args.provisional))
    if cache_diagnostic:
        rows=[json.loads(line[6:]) for line in result.stdout.decode('utf-8').splitlines() if line.startswith('CACHE ')]
        assert len(rows)==10
        save(dest/'cache-stats.json',dict(diagnostic_only=True,rows=rows,
            notice='Separate generated post-loop stats adapter, not timed comparison evidence. Byte gauges not additive to tracked heap. Missing fields null. No cold-first-paint cost measured.'))
    # Profiling can change clock overhead only, never rendering work or input.
    if not args.provisional:
        checks=[]
        def counters(folder):
            result=[]
            for line in (folder/'native.log').read_text(encoding='utf-8').splitlines():
                if line.startswith('{'):
                    row=json.loads(line);result.append({k:row[k] for k in ('scene','theme','flushes','pixels','refreshes')})
                elif line.startswith('PROFILE '):
                    row=json.loads(line[8:]);row.pop('engine_render_us_excluding_flush',None)
                    # Fine counters are intentionally disabled in optimized release.
                    # Never compare their zero placeholders with diagnostic work.
                    row.pop('primitives',None)
                    result.append(row)
            return result
        for i in range(1,4):
            coarse=OUT/'final'/('round-'+str(i))/args.backend
            c=json.loads((coarse/'run.json').read_text())
            coarse_sources=c['build']['source_sha256']
            diagnostic_sources=meta['source_sha256']
            if cache_diagnostic:
                adapter=str(HARNESS/'native_baseline_native.c')
                generated=str(OUT/'cache-diagnostic-adapter'/args.backend/'native_baseline_cache_diagnostic.c')
                assert {k:v for k,v in coarse_sources.items() if k!=adapter}=={k:v for k,v in diagnostic_sources.items() if k!=generated}
            else:
                assert coarse_sources==diagnostic_sources
            assert c['warmup']==args.warmup and c['logical_steps']==args.frames
            comparison=compare_frames(coarse,dest,10)
            comparison['refresh_dirty_work_equal']=counters(coarse)==counters(dest)
            comparison['primitive_counter_comparison']=dict(status='unavailable',reason='Release fine counters may be disabled; diagnostic fine counters are enabled. Zero placeholders are not evidence of equal work.')
            checks.append(comparison)
        save(dest/'timing-mode-equivalence.json',dict(equal=all(c['equal'] and c['refresh_dirty_work_equal'] for c in checks), scope='Actual final RGB images, flush/refresh counts and available coarse dirty work only; excludes fine primitive/cache counters', checks=checks))


def build_cold(backend):
    """Separate native-only first actual render probe; never used for steady-state results."""
    assert backend!='lvgl'
    exe,meta=build(backend,1)
    main=(V1/'snapshot/tab5/main') if backend=='native_c49' else OUT/'optimized-source/tab5/main'
    original=(HARNESS/'native_baseline_native.c').read_text(encoding='utf-8')
    extra=prepare_cache_adapter(main,OUT/'cold-adapter'/backend).read_text(encoding='utf-8')
    cache_printf=next(line.strip() for line in extra.splitlines() if line.strip().startswith('printf("CACHE '))
    state=original.split('static void run_scene(',1)[1].split('    tab5_ui_init(action,NULL);',1)[1].split('    if(scene==NB_GRAPH)',1)[0]
    assert state.endswith('pump();\n')
    state=state.removesuffix('pump();\n')
    prefix=original[:original.rindex('int main(')]
    cold=r'''
static void cold_snapshot(const char *stage,unsigned theme){
    enum nb_scene scene=NB_IDLE;unsigned frames=1;
    nui_native_stats_t stats;nui_native_profile_snapshot(&stats);
    printf("STAGE %s\n",stage);
    CACHE_PRINTF
}
int main(int argc,char **argv){
    assert(argc==2);unsigned theme=(unsigned)strtoul(argv[1],NULL,10);assert(theme<2);
    enum nb_scene scene=NB_IDLE;rgb565_mode=true;setbuf(stdout,NULL);
    double init_start=bench_now_ms();
    assert(nui_native_init(1280,720,framebuffer));
    nui_native_set_clock_cb(qpc_us);nui_native_set_flush_cb(bench_flush_complete,NULL);
    tab5_ui_init(action,NULL);
    INITIAL_STATE
    double init_end=bench_now_ms();
    nui_native_stats_t before,after;nui_native_profile_snapshot(&before);assert(before.frames==0);
    cold_snapshot("before_first_render",theme);
    flushes=flushed_pixels=refreshes=0;flush_time_ms=0;
    double start=bench_now_ms();render_now();double end=bench_now_ms();
    nui_native_profile_snapshot(&after);assert(after.frames==1&&refreshes==1&&flushes>0);
    cold_snapshot("after_first_render",theme);
    printf("COLD {\"theme\":\"%s\",\"mode\":\"diagnostic fine1 timing1\",\"ui_init_update_ms\":%.9f,\"first_render_with_mock_flush_ms\":%.9f,\"mock_flush_ms\":%.9f,\"engine_render_excluding_flush_ms\":%.9f,\"refreshes\":%llu,\"flushes\":%llu,\"dirty_pixels\":%llu}\n",
        theme?"light":"dark",init_end-init_start,end-start,flush_time_ms,
        (after.render_us-before.render_us)/1000.0,(unsigned long long)refreshes,
        (unsigned long long)flushes,(unsigned long long)(after.dirty_pixels-before.dirty_pixels));
    tab5_ui_destroy();nui_native_deinit();return 0;
}
'''.replace('CACHE_PRINTF',cache_printf).replace('INITIAL_STATE',state)
    dest=OUT/'cold-build'/backend;dest.mkdir(parents=True,exist_ok=True)
    src=dest/'native_baseline_cold.c';src.write_text(prefix+cold,encoding='utf-8')
    obj=dest/'cold.o';cc=meta['compiler']
    subprocess.run([cc,*meta['flags'],'-c',str(src),'-o',str(obj)],check=True)
    adapter_obj=hashlib.sha256(str(HARNESS/'native_baseline_native.c').encode()).hexdigest()+'.o'
    objects=[p for p in sorted(exe.parent.glob('*.o')) if p.name!=adapter_obj]
    assert (exe.parent/adapter_obj).exists()
    output=dest/'cold.exe';rsp=dest/'link.rsp'
    rsp.write_text('\n'.join('"'+str(p).replace('\\','/')+'"' for p in [*objects,obj,'-lm','-o',output]),encoding='utf-8')
    subprocess.run([cc,'@'+str(rsp)],check=True)
    save(dest/'build.json',dict(base_build=meta,diagnostic_only=True,generated_source_sha256=sha(src),
        exe=str(output),scope='Fresh process per theme, UI init/state update then FIRST actual render. No pump, touch, virtual ticks or hidden warmup. No file IO within measured render. Mock flush is separately timed and overlaps wall time. Not a steady-state baseline adapter.'))
    return output


def cold_probe(args):
    require_timing_ready(args.provisional)
    exe=build_cold(args.backend)
    dest=OUT/('provisional-cold' if args.provisional else 'cold')/args.backend
    assert not dest.exists();dest.mkdir(parents=True)
    rows=[];caches=[]
    for theme in range(2):
        require_timing_ready(args.provisional)
        result=subprocess.run([str(exe),str(theme)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
        (dest/('light.log' if theme else 'dark.log')).write_bytes(result.stdout);result.check_returncode()
        stage=None
        for line in result.stdout.decode('utf-8').splitlines():
            if line.startswith('STAGE '):stage=line[6:]
            elif line.startswith('CACHE '):row=json.loads(line[6:]);row['stage']=stage;caches.append(row)
            elif line.startswith('COLD '):rows.append(json.loads(line[5:]))
    assert len(rows)==2 and len(caches)==4
    save(dest/'results.json',dict(diagnostic_only=True,rows=rows,caches=caches,
        notice='One fresh-process sample per theme; not median/p95, not part 54000 steady-state samples. Engine coarse time excludes same mock RGB sink conversion; target hardware CPU/DSI speed not inferred. Fine/timing instrumentation is active.'))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=('prepare','freeze-optimized','build','run','profile','compare-98','prepare-cache-adapter','build-cold','cold'))
    p.add_argument('--cache-diagnostic',action='store_true')
    p.add_argument('--reference', type=Path)
    p.add_argument('--candidate', type=Path)
    p.add_argument('--backend', choices=BACKENDS, default='native_c49')
    p.add_argument('--primitive-timing', type=int, choices=(0,1), default=0)
    p.add_argument('--warmup', type=int, default=120)
    p.add_argument('--frames', type=int, default=600)
    p.add_argument('--rounds', type=int, default=3)
    p.add_argument('--provisional', action='store_true')
    args=p.parse_args()
    if args.command=='prepare': prepare()
    elif args.command=='freeze-optimized': freeze_optimized()
    elif args.command=='build': print(build(args.backend,args.primitive_timing,args.cache_diagnostic)[0])
    elif args.command=='prepare-cache-adapter':
        prepare()
        print(prepare_cache_adapter(ROOT/'tab5/main',OUT/'cache-adapter-preview'))
    elif args.command=='profile': profile(args)
    elif args.command=='build-cold':print(build_cold(args.backend))
    elif args.command=='cold':cold_probe(args)
    elif args.command=='compare-98':
        assert args.reference and args.candidate
        result=compare_frames(args.reference,args.candidate,98)
        save(OUT/'v1-optimized-98-equivalence.json',result)
        assert result['equal'], 'RGB images differ'
    else: run(args)
