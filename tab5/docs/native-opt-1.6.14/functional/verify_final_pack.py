"""Read-only strict verifier: frozen source, decoded RGB, reported gates and hashes."""
from pathlib import Path
import hashlib,json,sys
from PIL import Image
pack=Path(__file__).resolve().parent
ref=Path(sys.argv[1]) if len(sys.argv)>1 else Path('C:/Projects/ClaudeExp/tab5-feedback/2026-10-22-native-opt/native-v1-reference/captures/native')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((pack/'SOURCE_MANIFEST.json').read_text())
for n,h in manifest['files'].items():assert sha(pack/'source'/n)==h,('source',n)
for n,h in manifest['expected_owner_hashes'].items():assert sha(pack/'source'/n)==h,('owner-hash',n)
frames=json.loads((ref.parents[1]/'tab5/docs/native/frames.json').read_text());expected={r['name']:r for r in frames['frames']};assert frames['count']==len(expected)==98
both=[]
for mode in ('diagnostic','release'):
 folder=pack/'equivalence'/mode;run=json.loads((folder/'run.json').read_text());assert run['overall_returncode']==0 and run['native_v1_exact_rgb']=='PASS' and run['exact_geometry_difference_count']==0
 assert set(run['captures'])==set(expected)
 assert '-DNUI_PROFILE_PRIMITIVE_TIMING=0' in run['flags']
 assert '-DNUI_PROFILE_FINE_COUNTERS='+('1' if mode=='diagnostic' else '0') in run['flags']
 for n,h in {**run['source_sha256'],**run['header_sha256']}.items():assert manifest['files'][n]==h
 values={}
 for n,row in expected.items():
  assert sha(ref/n)==row['sha256'],('immutable-v1',n)
  with Image.open(ref/n) as a,Image.open(folder/n) as b:
   assert a.size==b.size==(1280,720)
   assert a.convert('RGB').tobytes()==b.convert('RGB').tobytes(),(mode,n)
   values[n]=hashlib.sha256(b.convert('RGB').tobytes()).hexdigest()
 both.append(values)
assert both[0]==both[1]
results=json.loads((pack/'TEST_RESULTS.json').read_text());required={'targeted','atomic-role','run_native_gradient','run_native_profile_flags','run_draw_profile_mode','run_native_lifetime','run_native_page_dirty','final-regressions-build','final-regressions'}
assert required=={r['name'] for r in results};assert all(r['status']=='PASS' and r['returncode']==0 for r in results)
targeted=json.loads((pack/'targeted/TARGETED_GATE.json').read_text());assert targeted['status']=='PASS' and len(targeted['results'])==10
atomic=json.loads((pack/'atomic-role/ATOMIC_ROLE_GATE.json').read_text());assert atomic['status']=='PASS' and len(atomic['results'])==2
for path in (pack/'targeted/TARGETED_GATE.json',pack/'atomic-role/ATOMIC_ROLE_GATE.json'):
 report=json.loads(path.read_text())
 for n,h in report['source_sha256'].items():assert manifest['files'][n]==h
for n,h in json.loads((pack/'ARTIFACT_SHA256.json').read_text()).items():assert sha(pack/n)==h,('artifact',n)
print('FINAL_STRICT_CHECK PASS: all source/owner hashes; both modes98 exact-v1 RGB; geometry0; 10 targeted + atomic + helpers/lifetime64/dirty40; artifact hashes')
