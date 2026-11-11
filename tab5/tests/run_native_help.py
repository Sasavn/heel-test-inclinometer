"""Render actual Tab5 help offscreen; assert text/glyph fit and input/state contract."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess
from PIL import Image
root=Path(__file__).resolve().parents[2];main=root/'tab5/main'
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
sources=[root/'tab5/tests/test_native_help.c']+[main/n for n in ['tab5_ui_graph.c','tab5_measure.c','tab5_time.c','tab5_plot.c','tab5_level.c','native/native_ui.c','native/font_native_fallback14.c']]+sorted(main.glob('font_tab5_*.c'))
exe=out/'native-help.exe'
command=[shutil.which('gcc'),'-std=c11','-O2','-Wall','-Werror=misleading-indentation','-Werror=format-truncation','-DNUI_PROFILE_FINE_COUNTERS=0','-DNUI_PROFILE_PRIMITIVE_TIMING=0','-I'+str(main),*[str(s) for s in sources],'-lm','-o',str(exe)]
subprocess.run(command,check=True)
r=subprocess.run([str(exe),str(out)],capture_output=True,text=True,timeout=60);(out/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
frames=[]
for path in sorted(out.glob('*.rgb565')):
    image=Image.frombytes('RGB',(1280,720),path.read_bytes(),'raw','BGR;16');image.save(path.with_suffix('.png'));frames.append(path.with_suffix('.png'))
sheet=Image.new('RGB',(1280,360*((len(frames)+1)//2)),(238,238,238))
for i,path in enumerate(frames):
    with Image.open(path) as im:sheet.paste(im.resize((640,360)),((i%2)*640,(i//2)*360))
sheet.save(out/'contact-sheet.png')
(out/'result.json').write_text(json.dumps({'passed':True,'frames':[str(x) for x in frames],'command':command,'sources':{str(x.relative_to(root)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [main/'tab5_ui.c',*sources]}},indent=2)+'\n')
