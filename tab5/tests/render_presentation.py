from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import hashlib,json,shutil,subprocess
from PIL import Image,ImageDraw,ImageFont
import argparse
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description='Actual Tab5 UI gallery from synthetic host state; no device or window.')
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--publish',type=Path,help='Optional clean PNG/manifest/README gallery directory')
args=parser.parse_args()
OUT=args.out.resolve();OUT.mkdir(parents=True,exist_ok=True)
SRC=ROOT/'tab5/main'
BUILD=OUT/'build';BUILD.mkdir(exist_ok=True)
sources=[ROOT/'tab5/tests/test_presentation_ui.c',SRC/'native/native_ui.c',SRC/'native/font_native_fallback14.c']
sources += [SRC/n for n in ('tab5_measure.c','tab5_ui_graph.c','tab5_time.c','tab5_plot.c','tab5_level.c','tab5_relative.c','tab5_workspace.c')]
sources += sorted(SRC.glob('font_tab5_*.c'))
gcc=shutil.which('gcc');flags=['-std=c11','-O2','-Werror=misleading-indentation','-Werror=format-truncation','-DNUI_PROFILE_FINE_COUNTERS=0','-DNUI_PROFILE_PRIMITIVE_TIMING=0','-I'+str(SRC)]
closure=[*sources,SRC/'tab5_ui.c',SRC/'CMakeLists.txt',ROOT/'tab5/tools/gen_fonts.py',Path(__file__),*sorted(SRC.rglob('*.h'))]
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in closure}
header_bytes=b''.join(p.read_bytes() for p in sorted(SRC.rglob('*.h')))
def compile(p):
    obj=BUILD/(p.stem+'.o')
    stamp=obj.with_suffix('.sha256')
    dependencies=(SRC/'tab5_ui.c').read_bytes() if p.name=='test_presentation_ui.c' else b''
    digest=hashlib.sha256(p.read_bytes()+dependencies+header_bytes+str(flags).encode()).hexdigest()
    if not obj.exists() or not stamp.exists() or stamp.read_text()!=digest:
        subprocess.run([gcc,*flags,'-c',str(p),'-o',str(obj)],check=True,cwd=OUT);stamp.write_text(digest)
    return obj
with ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile,sources))
exe=BUILD/'gallery.exe';rsp=BUILD/'link.rsp';rsp.write_text('\n'.join('"'+p.as_posix()+'"' for p in objects)+'\n-lm\n-o\n"'+exe.as_posix()+'"\n')
subprocess.run([gcc,'@'+str(rsp)],check=True,cwd=OUT)
r=subprocess.run([str(exe),str(OUT)],cwd=OUT,capture_output=True,text=True,encoding='utf-8',errors='replace');(OUT/'gallery.log').write_text(r.stdout+r.stderr,encoding='utf-8');print(r.stdout+r.stderr);r.check_returncode()
frames=[]
for row in (OUT/'index.tsv').read_text(encoding='utf-8').splitlines():
    name,mode,page,theme,kind=row.split('\t');dest=OUT/kind;dest.mkdir(exist_ok=True)
    raw=OUT/(name+'.rgb565');png=dest/(name+'.png');im=Image.frombytes('RGB',(1280,720),raw.read_bytes(),'raw','BGR;16');im.save(png)
    frames.append(dict(file=str(png.relative_to(OUT)).replace('\\','/'),mode=mode,page=page,theme=theme,group=kind,demo=True,hardware_verified=False,data_source='tab5/tests/test_presentation_ui.c: synthetic host fixture; real relative/workspace engines',renderer='production native RGB565 tab5_ui.c',size=[1280,720],sha256=hashlib.sha256(png.read_bytes()).hexdigest()))
assert all(hashlib.sha256(p.read_bytes()).hexdigest()==before[str(p)] for p in closure)
(OUT/'private-manifest.json').write_text(json.dumps(dict(source=str(SRC/'tab5_ui.c'),source_hashes=before,source_unchanged_during_run=True,hardware=False,window=False,fixtures_only=True,frames=frames),ensure_ascii=False,indent=2),encoding='utf-8')
for theme in ('light','dark'):
    rows=[f for f in frames if f['theme']==theme and f['group']=='main']
    sheet=Image.new('RGB',(1280,400*((len(rows)+2)//3)),'#e9edf4');d=ImageDraw.Draw(sheet)
    for i,f in enumerate(rows):
        im=Image.open(OUT/f['file']);im.thumbnail((420,237));x=(i%3)*426;y=(i//3)*400;sheet.paste(im,(x,y));d.text((x+6,y+245),f['file'].split('/')[-1],fill='#1a1d24')
    sheet.save(OUT/('contact_main_'+theme+'.png'))
print('Frames:',len(frames),'main:',sum(f['group']=='main' for f in frames))

if args.publish:
    publish=args.publish.resolve();publish.mkdir(parents=True,exist_ok=True)
    for f in frames:
        dest=publish/f['file'];dest.parent.mkdir(exist_ok=True)
        shutil.copy2(OUT/f['file'],dest)
    public_sources={str(Path(p).relative_to(ROOT)).replace('\\','/'):h for p,h in before.items()}
    (publish/'manifest.json').write_text(json.dumps(dict(
        schema_version=1,title='Tab5 1.8.7 UI presentation gallery',resolution=[1280,720],
        source='tab5/main/tab5_ui.c',source_hashes=public_sources,
        fixture='tab5/tests/test_presentation_ui.c',runner='tab5/tests/render_presentation.py',
        fixtures_only=True,hardware_verified=False,native_window=False,
        demo_metrics='CPU, RAM, media, time, network and angles are synthetic fixture state, not device measurements',
        network_data='DEMO SSIDs, documentation-only 192.0.2.0/24 addresses, masked synthetic keys',
        fixture_firmware_label='1.8.7-esp32; a UI fixture string, not binary verification',
        counts={group:sum(f['group']==group for f in frames) for group in ('main','help','dialog','validation')},
        inventory={'heel':['angles','rolling','chart','recording','settings','network'],
                   'level':['alignment','observation','quality','journal','settings','network'],
                   'settings_scroll_sections':['polling','rolling','rolling_more','device/theme/time','display/firmware/backlight/PC synchronization'],
                   'network_scroll_sections':['access point','station'], 'help_topics':8},
        frames=frames),ensure_ascii=False,indent=2),encoding='utf-8')
    titles={'angles':'Углы','rolling':'Качка','chart':'График','recording':'Запись',
            'settings':'Настройки: опрос','settings_rolling':'Настройки: качка',
            'settings_rolling_more':'Настройки: параметры покоя','settings_device':'Настройки: прибор, тема, часы',
            'settings_bottom':'Настройки: экран, прошивка, выключение, синхронизация',
            'network':'Сеть: точка доступа','network_bottom':'Сеть: подключение Wi-Fi',
            'alignment':'Угломер / выравнивание','observation':'Наблюдение','quality':'Качество пары','journal':'Журнал'}
    text=['# Tab5 1.8.7 — галерея интерфейса','',
        'Все PNG — 1280×720, штатный native RGB565 рендерер настоящего Tab5 UI. Входы только синтетические хост-фикстуры. Эти изображения не являются аппаратными измерениями или проверкой собранной прошивки.',
        '', 'CPU, память, носители, часы, сеть и углы заданы демонстрационным состоянием. SSID демонстрационные, IP из 192.0.2.0/24, ключи скрывает обычный UI. Номер прошивки в настройках задан фикстурой.',
        '', '## Инвентаризация','',
        'Креномер: Углы, Качка, График, Запись, Настройки, Сеть. Угломер: Выравнивание, Наблюдение, Качество пары, Журнал, Настройки, Сеть. Каждая страница показана в светлой и тёмной темах.',
        '', 'Настройки и сеть прокручиваются: дополнительные PNG показывают их разделы, а не новые вкладки. Основные страницы (`main`), справка (`help`), диалоги (`dialog`) и граничные проверки (`validation`) разделены. Имена файлов, роли, темы, происхождение и SHA-256 перечислены в `manifest.json`.',
        '', '## Основные изображения для слайдов','',
        '| Режим / страница | Светлая тема | Тёмная тема |', '|---|---|---|']
    for mode in ('heel','level'):
        pages=list(dict.fromkeys(f['page'] for f in frames if f['mode']==mode and f['group']=='main'))
        for page in pages:
            links=[]
            for theme in ('light','dark'):
                file=next(f['file'] for f in frames if f['mode']==mode and f['page']==page and f['theme']==theme and f['group']=='main')
                links.append('['+Path(file).name+']('+file+')')
            text.append('| '+('Креномер' if mode=='heel' else 'Угломер')+' / '+titles[page]+' | '+' | '.join(links)+' |')
    text += ['', '![Угломер: светлая тема](main/demo_level_alignment_light.png)',
             '', '![Угломер: тёмная тема](main/demo_level_alignment_dark.png)',
             '', '## Справка, диалоги и проверки','', '| Группа / экран | Светлая тема | Тёмная тема |','|---|---|---|']
    for group in ('help','dialog','validation'):
        keys=list(dict.fromkeys((f['mode'],f['page']) for f in frames if f['group']==group))
        for mode,page in keys:
            links=[]
            for theme in ('light','dark'):
                file=next(f['file'] for f in frames if f['group']==group and f['mode']==mode and f['page']==page and f['theme']==theme)
                links.append('['+Path(file).name+']('+file+')')
            text.append('| '+group+' / '+page+' | '+' | '.join(links)+' |')
    text += ['', '## Источники и повторение','',
             'Стенд: [test_presentation_ui.c](../../tests/test_presentation_ui.c). Запуск: [render_presentation.py](../../tests/render_presentation.py). Состояние пары и наблюдения получают настоящие tab5_relative / tab5_workspace; обычные страницы не заменяются макетными слоями.',
             '', '```text', 'python tab5/tests/render_presentation.py --out .tools/tab5-presentation --publish tab5/docs/ui-1.8.7','```',
             '', 'Нужны GCC и Python с Pillow. Окно, UART/COM и устройство не используются. Рабочая папка `--out` содержит технические результаты; `--publish` содержит только PNG, manifest.json и README.md.',
             '', 'В угломере разности используют настоящий Roboto 128 px с контролируемым переходом к 108 px для длинных чисел; исходные углы — 48 px. Проверены градусы, мм/м, ±180°, трёхзначные углы, знак и градус, диапазон tan около 90°, доступность Справки и переключателя единиц. Протокол и измерительные вычисления не менялись.']
    (publish/'README.md').write_text('\n'.join(text)+'\n',encoding='utf-8')
    assert not any(p.suffix not in ('.png','.json','.md') for p in publish.rglob('*') if p.is_file())
    print('Clean publication gallery:',publish)
