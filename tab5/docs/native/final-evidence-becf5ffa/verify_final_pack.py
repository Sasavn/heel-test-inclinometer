"""Read-only independent verifier for the frozen final candidate and captured RGB."""
from pathlib import Path
import hashlib,json,sys
from PIL import Image
pack=Path(__file__).resolve().parent
ref=Path(sys.argv[1]) if len(sys.argv)>1 else Path('C:/Projects/ClaudeExp/tab5-feedback/2026-10-22-native-opt/native-v1-reference/captures/native')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=json.loads((pack/'SOURCE_MANIFEST.json').read_text())
for name,digest in source['files'].items():assert sha(pack/'source'/name)==digest,('source',name)
manifest=json.loads((ref.parents[1]/'tab5/docs/native/frames.json').read_text());expected={x['name']:x for x in manifest['frames']};assert manifest['count']==len(expected)==98
mode_hashes=[]
for mode in ['diagnostic','release']:
 folder=pack/'equivalence'/mode;run=json.loads((folder/'run.json').read_text())
 assert run['overall_returncode']==0 and run['native_v1_exact_rgb']=='PASS' and run['exact_geometry_difference_count']==0
 assert set(run['captures'])==set(expected)
 flags=run['flags'];assert '-DNUI_PROFILE_PRIMITIVE_TIMING=0' in flags
 assert '-DNUI_PROFILE_FINE_COUNTERS='+('1' if mode=='diagnostic' else '0') in flags
 for name,digest in {**run['source_sha256'],**run['header_sha256']}.items():assert source['files'][name]==digest
 hashes={}
 for name,row in expected.items():
  assert sha(ref/name)==row['sha256'],('immutable-reference',name)
  with Image.open(ref/name) as a,Image.open(folder/name) as b:
   assert a.size==b.size==(1280,720)
   assert a.convert('RGB').tobytes()==b.convert('RGB').tobytes(),(mode,name)
   hashes[name]=hashlib.sha256(b.convert('RGB').tobytes()).hexdigest()
 mode_hashes.append(hashes)
assert mode_hashes[0]==mode_hashes[1]
helpers=json.loads((pack/'HELPERS.json').read_text());assert all(x['status']=='PASS' or x['name']=='asan' and x['status']=='UNAVAILABLE' for x in helpers)
required={'run_native_gradient','run_native_profile_flags','run_draw_profile_mode','run_native_lifetime','final_regressions_build','final_regressions'}
assert required<={x['name'] for x in helpers if x['status']=='PASS'}
for name,digest in json.loads((pack/'ARTIFACT_SHA256.json').read_text()).items():assert sha(pack/name)==digest,('artifact',name)
print('FINAL_STRICT_CHECK PASS: immutable source closure, both modes 98 RGB-exact v1, geometry0, helpers and artifact hashes verified')
