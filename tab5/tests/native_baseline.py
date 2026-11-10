"""Frozen 369c7fc RGB565 capture, O2 scene benchmark and strict pixel comparison.
All generated files are restricted to .tools/native-baseline/. Production files are never modified; baseline inputs are frozen.
"""
import argparse
import concurrent.futures
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import time

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT/'.tools/native-baseline'
SNAPSHOT = BASE/'snapshot'
COMMIT = '369c7fcd435ffd09631e49c0118e102c66ea4d75'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def save_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')

def validate_lvgl_source(source):
    source=source.resolve()
    header=source/'lv_version.h'
    if not header.is_file() or not (source/'src').is_dir():
        raise ValueError('LVGL source needs lv_version.h and src/: '+str(source))
    text=header.read_text(encoding='utf-8')
    version=[]
    for part in ['MAJOR','MINOR','PATCH']:
        match=re.search(r'^#define\s+LVGL_VERSION_'+part+r'\s+(\d+)\s*$',text,re.M)
        if not match:raise ValueError('Cannot validate LVGL version')
        version.append(int(match.group(1)))
    if version!=[9,3,0]:raise ValueError('Only explicit LVGL 9.3.0 is supported; found '+str(version))
    return source

def freeze(lvgl_source=None):
    BASE.mkdir(parents=True, exist_ok=True)
    if (BASE/'snapshot.json').exists():
        return verify()
    if lvgl_source is None:
        raise SystemExit('Fresh freeze needs --lvgl-source PATH_TO_LVGL_9_3_0. See tab5/tests/native_baseline_provisioning.md; production managed_components is never used implicitly.')
    dependency=validate_lvgl_source(lvgl_source)
    archive = subprocess.check_output(['git','archive',COMMIT,'tab5/main','tab5/tests'],cwd=ROOT)
    (BASE/'source.tar').write_bytes(archive)
    SNAPSHOT.mkdir(exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(archive)) as stream:
        stream.extractall(SNAPSHOT,filter='data')
    shutil.copytree(dependency,SNAPSHOT/'tab5/managed_components/lvgl__lvgl',dirs_exist_ok=True,ignore=shutil.ignore_patterns('.git'))
    files = {p.relative_to(SNAPSHOT).as_posix():sha(p) for p in sorted(SNAPSHOT.rglob('*')) if p.is_file()}
    save_json(BASE/'snapshot.json',dict(commit=COMMIT,source_archive_sha256=sha(BASE/'source.tar'),lvgl_origin=str(dependency),files=files))
    verify()

def verify():
    manifest = json.loads((BASE/'snapshot.json').read_text(encoding='utf-8'))
    assert manifest['commit'] == COMMIT
    assert sha(BASE/'source.tar') == manifest['source_archive_sha256']
    for name,digest in manifest['files'].items():
        assert sha(SNAPSHOT/name) == digest, 'Frozen file changed: '+name
    print('Verified frozen commit',COMMIT,'files',len(manifest['files']))

def index_frames(directory):
    from PIL import Image
    files = sorted(directory.glob('*.ppm'))
    records = []
    for path in files:
        with Image.open(path) as im:
            assert im.size == (1280,720)
            records.append(dict(name=path.stem,size=list(im.size),rgb_sha256=hashlib.sha256(im.convert('RGB').tobytes()).hexdigest(),ppm_sha256=sha(path)))
    save_json(directory/'frames.json',dict(commit=COMMIT,format='RGB565 expanded exactly by frozen flush to RGB888 PPM',count=len(records),frames=records))
    return records

def capture():
    verify()
    out=BASE/'frames'
    subprocess.run([sys.executable,str(SNAPSHOT/'tab5/tests/run_ui_test.py'),'--rgb565','--out',str(out)],check=True,cwd=SNAPSHOT)
    frames=index_frames(out)
    assert len(frames)==98, 'Unexpected frozen screenshot count'
    print('Frozen capture suite:',len(frames),'frames')

def build_bench(harness_name="native_baseline_bench.c"):
    verify()
    text=(ROOT/'tab5/tests/native_baseline_bench.c').read_text(encoding='utf-8')
    for label in ['30 \u0441','\u0413\u043e\u0442\u043e\u0432\u043e']:
        assert label in text, 'Benchmark source UTF-8 label corrupted'

    cc=shutil.which('gcc')
    if not cc:
        raise RuntimeError('gcc required')
    lvgl=SNAPSHOT/'tab5/managed_components/lvgl__lvgl'
    main=SNAPSHOT/'tab5/main'
    tests=SNAPSHOT/'tab5/tests'
    flags=['-std=c11','-O2','-DNB_FROZEN_TEST=\"'+str(tests/'test_ui.c').replace('\\','/')+'\"','-DLV_CONF_SKIP','-DLV_COLOR_DEPTH=16','-DLV_USE_LOG=0',
           '-DLV_MEM_SIZE=8388608','-DLV_USE_OS=0','-DLV_USE_DRAW_SW=1','-DLV_USE_DRAW_SW_ASM=0',
           '-I'+str(lvgl),'-I'+str(main),'-I'+str(tests),'-I'+str(ROOT/'tab5/tests')]
    # Assertions deliberately remain enabled; no fast-math or NDEBUG.
    sources=sorted((lvgl/'src').rglob('*.c'))
    sources += [main/name for name in ['tab5_ui.c','tab5_measure.c','tab5_ui_graph.c','tab5_time.c','tab5_plot.c','tab5_level.c']]
    sources += sorted(main.glob('font_tab5_*.c'))
    sources += [ROOT/'tab5/tests'/harness_name]
    dependencies=sorted(lvgl.rglob('*.h'))+sorted(main.glob('*.h'))+[tests/'test_ui.c',ROOT/'tab5/tests/native_baseline_contract.h']
    compiler_version=subprocess.check_output([cc,'--version'],text=True).splitlines()[0]
    fingerprint=hashlib.sha256((str(flags)+compiler_version+''.join(sha(p) for p in dependencies)).encode()).hexdigest()
    cache=BASE/'build'/fingerprint[:16];cache.mkdir(parents=True,exist_ok=True)
    def compile_one(source):
        digest=hashlib.sha256((str(source)+sha(source)).encode()).hexdigest()
        obj=cache/(digest+'.o')
        if not obj.exists():
            subprocess.run([cc,*flags,'-c',str(source),'-o',str(obj)],check=True)
        return obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        objects=list(pool.map(compile_one,sources))
    exe=cache/(Path(harness_name).stem+'.exe')
    response=cache/'link.rsp'
    response.write_text('\n'.join('"'+str(p).replace('\\','/')+'"' for p in [*objects,'-lm','-o',exe]),encoding='utf-8')
    subprocess.run([cc,'@'+str(response)],check=True)
    save_json(BASE/('build.json' if harness_name=='native_baseline_bench.c' else Path(harness_name).stem+'-build.json'),dict(commit=COMMIT,compiler=cc,compiler_version=compiler_version,flags=flags,
              host=platform.platform(),cpu=platform.processor(),exe=str(exe),snapshot_manifest_sha256=sha(BASE/'snapshot.json'),
              harness_sha256=sha(ROOT/'tab5/tests/native_baseline_bench.c'),contract_sha256=sha(ROOT/'tab5/tests/native_baseline_contract.h')))
    return exe

def build_native_bench(primitive_timing=0):
    cc=shutil.which('gcc')
    if not cc:raise RuntimeError('gcc required')
    main=ROOT/'tab5/main'
    helper=ROOT/'tab5/tests/test_native_ui.c'
    harness=ROOT/'tab5/tests/native_baseline_native.c'
    assert 'NUI_PROFILE_PRIMITIVE_TIMING' in (main/'native/native_ui.c').read_text(encoding='utf-8'), 'Native primitive timing compile gate is not yet available'
    flags=['-std=c11','-O2','-DNUI_PROFILE_PRIMITIVE_TIMING='+str(primitive_timing),'-DNUI_COLOR_DEPTH=16','-DNB_NATIVE_TEST="'+helper.as_posix()+'"','-I'+str(main),'-I'+str(ROOT/'tab5/tests')]
    sources=[harness,main/'native/native_ui.c',main/'native/font_native_fallback14.c']
    sources += [main/name for name in ['tab5_ui.c','tab5_measure.c','tab5_ui_graph.c','tab5_time.c','tab5_plot.c','tab5_level.c']]
    sources += sorted(main.glob('font_tab5_*.c'))
    headers=sorted(main.rglob('*.h'))+[helper,ROOT/'tab5/tests/native_baseline_contract.h']
    hashes={str(path):sha(path) for path in sources+headers}
    version=subprocess.check_output([cc,'--version'],text=True).splitlines()[0]
    fingerprint=hashlib.sha256((str(flags)+version+json.dumps(hashes,sort_keys=True)).encode()).hexdigest()
    cache=BASE/'native-build'/fingerprint[:16];cache.mkdir(parents=True,exist_ok=True)
    def compile_one(source):
        obj=cache/(hashlib.sha256((str(source)+sha(source)).encode()).hexdigest()+'.o')
        if not obj.exists():subprocess.run([cc,*flags,'-c',str(source),'-o',str(obj)],check=True)
        return obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        objects=list(pool.map(compile_one,sources))
    exe=cache/'native_adapter.exe';response=cache/'link.rsp'
    response.write_text('\n'.join('"'+str(path).replace('\\','/')+'"' for path in [*objects,'-lm','-o',exe]),encoding='utf-8')
    subprocess.run([cc,'@'+str(response)],check=True)
    assert all(sha(Path(name))==digest for name,digest in hashes.items()), 'Native sources changed during compilation; rebuild when edits settle'
    metadata=dict(compiler=cc,compiler_version=version,flags=flags,source_sha256=hashes,exe=str(exe),host=platform.platform(),primitive_timing=primitive_timing,mode='instrumented' if primitive_timing else 'coarse',hardware_transport='N/A')
    save_json(BASE/('build-native-instrumented.json' if primitive_timing else 'build-native-coarse.json'),metadata)
    if not primitive_timing:save_json(BASE/'build-native.json',metadata)
    return exe

def wait_for_compile_idle():
    # Do not compete with the parent's IDF/native builds. Two quiet samples
    # guard a brief gap between compiler invocations. No host timing here.
    if os.name!='nt':return
    probe="@(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match '^(ninja|cmake|gcc|g\\+\\+|cc1|cc1plus|clang|riscv32-esp-elf-gcc|xtensa.*gcc)$' }).Count"
    quiet=0
    while quiet<2:
        result=subprocess.check_output(['powershell','-NoProfile','-Command',probe],text=True).strip()
        quiet=quiet+1 if result=='0' else 0
        if quiet<2:time.sleep(3)

def require_timing_release():
    hold=BASE/'timing-hold.json'
    if hold.exists() and json.loads(hold.read_text(encoding='utf-8')).get('hold'):
        raise SystemExit('TIMING HOLD: internal build/source-freeze coordination. Benchmarks already authorized; await coordination signal: timing ready. No additional approval needed.')

def benchmark(args):
    require_timing_release()
    if not args.allow_timing:
        raise SystemExit('Timing disabled while other compilation jobs run. Re-run benchmark --allow-timing once they finish.')
    wait_for_compile_idle()
    exe=build_bench()
    rows=[]
    memory=[]
    for run in range(args.runs):
        out=BASE/'benchmark'/('run-'+str(run+1));out.mkdir(parents=True,exist_ok=True)
        wait_for_compile_idle()
        result=subprocess.run([str(exe),str(args.warmup),str(args.frames),str(out)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
        (out/'native.log').write_bytes(result.stdout)
        print(result.stdout.decode('utf-8',errors='replace'),end='')
        result.check_returncode()
        parsed=[json.loads(line) for line in result.stdout.decode('utf-8').splitlines() if line.startswith('{')]
        allocations=[json.loads(line[len('MEMORY '):]) for line in result.stdout.decode('utf-8').splitlines() if line.startswith('MEMORY ')]
        assert len(allocations)==10
        for entry in allocations: entry['run']=run+1
        memory.extend(allocations)
        assert len(parsed)==10
        for row in parsed: row['run']=run+1
        rows.extend(parsed)
        index_frames(out)
    save_json(BASE/'benchmark/memory.json',dict(rows=memory,notice='LVGL usable TLSF blocks versus native logical payload are different metrics. Allocator headers, CRT overhead, code/fonts/static UI storage and external buffers are not included in logical payload; no combined total inferred. Native external framebuffer is not allocated by this baseline mock sink.'))
    save_json(BASE/'benchmark/results.json',dict(contract_version=1,format='RGB565',optimization='-O2',
              warmup=args.warmup,frames=args.frames,runs=args.runs,clock='QueryPerformanceCounter on Windows',
              timed_scope='update + input + LVGL pump (5x40ms virtual) + synchronous RGB565-to-RGB888 mock flush; no file/PNG IO',rows=rows))

def wait_for_native_settle():
    previous=None;quiet=0
    while quiet<5:
        files=sorted((ROOT/'tab5/main').rglob('*.h'))+sorted((ROOT/'tab5/main').rglob('*.c'))+[ROOT/'tab5/tests/test_native_ui.c']
        current={str(path):sha(path) for path in files}
        quiet=quiet+1 if current==previous else 0
        previous=current
        if quiet<5:time.sleep(3)

def paired_benchmark(args):
    require_timing_release()
    if not args.allow_timing:raise SystemExit('paired requires --allow-timing after other builds settle')
    wait_for_native_settle()
    wait_for_compile_idle()
    (BASE/'paired').mkdir(exist_ok=True)
    save_json(BASE/'paired/status.json',dict(status='building',final_results=False))
    executables={'baseline':build_bench(),'native':build_native_bench()}
    source_hashes=json.loads((BASE/'build-native.json').read_text(encoding='utf-8'))['source_sha256']
    rows=[];memory=[];profiles=[];sequence=[]
    for run in range(1,args.runs+1):
        order=['baseline','native'] if run%2 else ['native','baseline']
        for backend in order:
            wait_for_compile_idle()
            assert all(sha(Path(name))==digest for name,digest in source_hashes.items()), 'Native sources changed; rerun paired after renderer edits settle'
            out=BASE/'paired'/('pair-'+str(run))/backend;out.mkdir(parents=True,exist_ok=True)
            result=subprocess.run([str(executables[backend]),str(args.warmup),str(args.frames),str(out)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
            (out/'native.log').write_bytes(result.stdout)
            print(result.stdout.decode('utf-8',errors='replace'),end='')
            result.check_returncode()
            assert all(sha(Path(name))==digest for name,digest in source_hashes.items()), 'Native sources changed during timing; discard this run and rebuild'
            sequence.append(dict(pair=run,backend=backend))
            parsed=[]
            for line in result.stdout.decode('utf-8').splitlines():
                if line.startswith('{'):item=json.loads(line);parsed.append(item);target=rows
                elif line.startswith('MEMORY '):item=json.loads(line[7:]);target=memory
                elif line.startswith('PROFILE '):item=json.loads(line[8:]);target=profiles
                else:continue
                item.update(pair=run,backend=backend);target.append(item)
            assert len(parsed)==10
            index_frames(out)
            build=json.loads((BASE/('build-native-coarse.json' if backend=='native' else 'build.json')).read_text(encoding='utf-8'))
            save_json(out/'run.json',dict(valid=True,backend=backend,primitive_timing=0 if backend=='native' else None,contract_version=1,contract_sha256=sha(ROOT/'tab5/tests/native_baseline_contract.h'),format='RGB565',warmup=args.warmup,frames=args.frames,build=build))
    save_json(BASE/'paired/results.json' ,dict(contract_version=1,format='RGB565',optimization='-O2',warmup=args.warmup,frames=args.frames,runs=args.runs,sequence=sequence,
              clock='QPC',native_primitive_timing=0,backend_modes={'baseline':'uninstrumented','native':'coarse NUI_PROFILE_PRIMITIVE_TIMING=0'},hardware_rotation_dsi='N/A',metric_definitions=dict(total='UI update + logical input + render/layout processing + complete RGB565 conversion mock flush',
              update='tab5_ui_update wall time; may combine layout preparation and invalidation',render='input/pump/layout/render wall time; not a separable pure draw metric',
              flush_copy='synchronous RGB565 expansion and complete mock flush; overlaps total/render',native_engine_render='coarse profile render_us excludes flush; per-primitive clock calls disabled'),rows=rows))
    save_json(BASE/'paired/memory.json',dict(counter_semantics={'allocation_live_blocks':'current live blocks (mem_alloc increments, mem_free decrements)', 'native_logical_payload_current_bytes':'current tracked requested payload only', 'native_logical_payload_process_peak_bytes':'lifetime peak of tracked requested payload only', 'cumulative_malloc_calls':None, 'malloc_calls_per_frame':None},notice='Reserved, usable allocator blocks, logical requested payload and external buffers are separate; allocator headers/CRT overhead unknown; peaks are lifetime, not reset per scene. No cumulative allocation-call counter is available; live-block stability does not prove zero malloc calls per frame.',rows=memory))
    save_json(BASE/'paired/status.json',dict(status='complete',final_results=True,native_primitive_timing=0,pairs=args.runs))
    save_json(BASE/'paired/profiles.json',dict(primitive_order=['fill','border','label','image','line','arc','triangle','shadow'],rows=profiles))
    check_timing_modes()

def profile_native(args):
    require_timing_release()
    if not args.allow_timing:raise SystemExit('profile requires --allow-timing after builds settle')
    wait_for_native_settle();wait_for_compile_idle();exe=build_native_bench(1)
    wait_for_compile_idle()
    build=json.loads((BASE/'build-native-instrumented.json').read_text(encoding='utf-8'))
    assert all(sha(Path(name))==digest for name,digest in build['source_sha256'].items()), 'Native sources changed before diagnostic run'
    out=BASE/'profile';out.mkdir(parents=True,exist_ok=True)
    result=subprocess.run([str(exe),str(args.warmup),str(args.frames),str(out)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
    (out/'native.log').write_bytes(result.stdout);print(result.stdout.decode('utf-8',errors='replace'),end='');result.check_returncode()
    assert all(sha(Path(name))==digest for name,digest in build['source_sha256'].items()), 'Native sources changed during diagnostic run'
    profiles=[json.loads(line[8:]) for line in result.stdout.decode('utf-8').splitlines() if line.startswith('PROFILE ')]
    assert len(profiles)==10
    save_json(out/'profiles.json',dict(primitive_timing=1,mode='instrumented diagnostic only, excluded from before/after speed comparisons',clock='QPC us',hardware_rotation_dsi='N/A',rows=profiles))
    index_frames(out)
    save_json(out/'run.json',dict(valid=True,backend='native',primitive_timing=1,contract_version=1,contract_sha256=sha(ROOT/'tab5/tests/native_baseline_contract.h'),format='RGB565',warmup=args.warmup,frames=args.frames,build=build))
    check_timing_modes()

def check_timing_modes():
    """Offline evidence only. Never executes a renderer or releases timing hold."""
    output=BASE/'timing-mode-equivalence.json'
    report=dict(status='unavailable',equal=None,scope='Final scene framebuffers plus deterministic flush/render/primitive work counters, not every intermediate framebuffer',results=[])
    profile=BASE/'profile'
    status=BASE/'paired/status.json'
    if not status.exists() or not json.loads(status.read_text(encoding='utf-8')).get('final_results') or not (profile/'run.json').exists():
        report['reason']='Completed coarse paired runs and instrumented diagnostic run are both required; prototypes are not evidence'
        save_json(output,report);print('Timing-mode equivalence: unavailable (await completed valid runs)');return report
    def load(directory):
        run=json.loads((directory/'run.json').read_text(encoding='utf-8'))
        frames=json.loads((directory/'frames.json').read_text(encoding='utf-8'))
        images={item['name']:item for item in frames['frames']}
        counts={};profiles={}
        for line in (directory/'native.log').read_text(encoding='utf-8').splitlines():
            if line.startswith('{'):
                item=json.loads(line);counts[item['scene']+'_'+item['theme']]=item
            elif line.startswith('PROFILE '):
                item=json.loads(line[8:]);profiles[item['scene']+'_'+item['theme']]=item
        return run,images,counts,profiles
    instrumented,ref_images,ref_counts,ref_profiles=load(profile)
    if not instrumented.get('valid') or instrumented.get('primitive_timing')!=1:
        report['reason']='Instrumented run provenance is invalid';save_json(output,report);return report
    failures=0
    for directory in sorted((BASE/'paired').glob('pair-*/native')):
        coarse,images,counts,profiles=load(directory)
        metadata=['contract_version','contract_sha256','format','warmup','frames']
        compatible=coarse.get('valid') and coarse.get('primitive_timing')==0 and all(coarse.get(key)==instrumented.get(key) for key in metadata)
        a,b=coarse['build'],instrumented['build']
        compatible=compatible and a.get('source_sha256')==b.get('source_sha256') and a.get('compiler_version')==b.get('compiler_version')
        clean_flags=lambda flags:[flag for flag in flags if not flag.startswith('-DNUI_PROFILE_PRIMITIVE_TIMING=')]
        compatible=compatible and clean_flags(a['flags'])==clean_flags(b['flags'])
        if not compatible:
            failures+=1;report['results'].append(dict(run=str(directory.relative_to(BASE)),status='incompatible_provenance'));continue
        names=sorted(set(images)|set(ref_images))
        if len(names)!=10:failures+=1
        for name in names:
            x,y=images.get(name),ref_images.get(name)
            same=bool(x and y and x['size']==y['size'] and x['rgb_sha256']==y['rgb_sha256'])
            # Verify manifests still describe actual stored captures.
            integrity=bool(x and y and sha(directory/(name+'.ppm'))==x['ppm_sha256'] and sha(profile/(name+'.ppm'))==y['ppm_sha256'])
            work=bool(name in counts and name in ref_counts and all(counts[name][key]==ref_counts[name][key] for key in ['flushes','pixels','refreshes']))
            draw=bool(name in profiles and name in ref_profiles and all(profiles[name][key]==ref_profiles[name][key] for key in ['frames','dirty_rects','dirty_pixels']))
            if draw:
                p,q=profiles[name]['primitives'],ref_profiles[name]['primitives']
                draw=len(p)==len(q) and all(all(left[key]==right[key] for key in ['id','calls','pixels']) for left,right in zip(p,q))
            equal=same and integrity and work and draw
            failures+=not equal
            report['results'].append(dict(run=str(directory.relative_to(BASE)),case=name,status='equal' if equal else 'different',frame_equal=same,stored_frame_integrity=integrity,flush_work_equal=work,render_primitive_work_equal=draw,coarse_rgb_sha256=x['rgb_sha256'] if x else None,instrumented_rgb_sha256=y['rgb_sha256'] if y else None))
    if not report['results']:
        report['reason']='No completed coarse native scene captures found'
    else:
        report.update(status='equal' if not failures else 'different',equal=not failures,failures=failures)
    save_json(output,report);print('Timing-mode equivalence:',report['status']);return report

def introspect():
    exe=build_bench('native_baseline_introspect.c')
    result=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
    (BASE/'introspection.log').write_bytes(result.stdout)
    result.check_returncode()
    rows=[json.loads(line) for line in result.stdout.decode('utf-8').splitlines() if line.startswith('{')]
    keyboard=[row for row in rows if row['context'].startswith('keyboard')]
    textareas=[row for row in rows if not row['context'].startswith('keyboard')]
    controls=dict(width_mask=15,hidden=16,no_repeat=32,disabled=64,checkable=128,checked=256,click_trig=512,popover=1024,recolor=2048,custom_1=16384,custom_2=32768)
    save_json(BASE/'keyboard-defaults.json',dict(commit=COMMIT,format='RGB565',area_semantics='inclusive coordinates, local relative to keyboard outer rect',ctrl_definitions=controls,rows=keyboard))
    save_json(BASE/'textarea-defaults.json',dict(commit=COMMIT,format='RGB565',rows=textareas))
    for row in keyboard:print(row['context'],row['key_count'],'keys',row['rows'],'rows')

def compare(candidate):
    from PIL import Image,ImageChops
    baseline=BASE/'frames'
    references=sorted(baseline.glob('*.ppm'))
    assert len(references)==98
    rows=[]
    for ref in references:
        other=candidate/(ref.stem+'.png')
        if not other.exists():other=candidate/ref.name
        if not other.exists():rows.append(dict(name=ref.stem,status='missing'));continue
        with Image.open(ref) as a,Image.open(other) as b:
            a=a.convert('RGB');b=b.convert('RGB')
            if a.size!=b.size:rows.append(dict(name=ref.stem,status='size_mismatch'));continue
            diff=ImageChops.difference(a,b)
            differing=sum(pixel!=(0,0,0) for pixel in diff.getdata())
            extrema=diff.getextrema()
            rows.append(dict(name=ref.stem,status='identical' if not differing else 'different',differing_pixels=differing,
                             bbox=diff.getbbox(),max_channel_delta=max(pair[1] for pair in extrema)))
    failures=sum(row['status']!='identical' for row in rows)
    save_json(BASE/'comparison.json',dict(candidate=str(candidate),frames=len(rows),failures=failures,results=rows))
    print('Comparison:',len(rows),'frames;',failures,'non-identical/missing')
    return failures!=0

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=['freeze','verify','index','capture','build','benchmark','build-native','paired','profile','check-modes','introspect','compare'])
    p.add_argument('--lvgl-source',type=Path,help='Explicit source tree of LVGL 9.3.0, needed only for first freeze')
    p.add_argument('--allow-timing',action='store_true')
    p.add_argument('--warmup',type=int,default=120)
    p.add_argument('--frames',type=int,default=600)
    p.add_argument('--runs',type=int,default=3)
    p.add_argument('--candidate',type=Path)
    a=p.parse_args()
    if a.command=='freeze':freeze(a.lvgl_source)
    elif a.command=='verify':verify()
    elif a.command=='index':print('Indexed',len(index_frames(BASE/'frames')),'frames')
    elif a.command=='capture':capture()
    elif a.command=='build':build_bench()
    elif a.command=='build-native':build_native_bench()
    elif a.command=='benchmark':
        assert 0<=a.warmup<=20000 and 100<=a.frames<=20000 and 1<=a.runs<=20
        benchmark(a)
    elif a.command=='paired':
        assert 0<=a.warmup<=20000 and 100<=a.frames<=20000 and 1<=a.runs<=20
        paired_benchmark(a)
    elif a.command=='profile':
        assert 0<=a.warmup<=20000 and 100<=a.frames<=20000
        profile_native(a)
    elif a.command=='check-modes':check_timing_modes()
    elif a.command=='introspect':introspect()
    elif a.command=='compare':
        assert a.candidate is not None
        return compare(a.candidate.resolve())
    return 0
if __name__=='__main__':raise SystemExit(main())
