"""OLD FAIL -> NEW PASS, actual core, ordered consume_sample, no hardware/UI."""
import argparse,json,pathlib,shutil,subprocess
root=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
cc=shutil.which('gcc') or 'C:/Users/aaaor/gcc/bin/gcc.exe'
results=[]
def execute(name,sources,inc,extra=(),expected=0):
 exe=a.out/(name+'.exe');cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-fstack-usage','-I'+str(inc),*extra,*map(str,sources),'-lm','-o',str(exe)]
 build=subprocess.run(cmd,capture_output=True,text=True);(a.out/(name+'-build.log')).write_text(build.stdout+build.stderr)
 if build.returncode:raise RuntimeError(build.stderr)
 run=subprocess.run([str(exe)],capture_output=True,text=True);(a.out/(name+'.log')).write_text(run.stdout+run.stderr)
 record={'name':name,'compile_command':cmd,'compile_exit':build.returncode,'run_exit':run.returncode,'expected_exit':expected,'pass':run.returncode==expected,'stdout':run.stdout,'stderr':run.stderr};results.append(record);print(json.dumps(record))
 if not record['pass']:raise RuntimeError(name+' unexpected result')
baseline=a.out/'baseline';baseline.mkdir(exist_ok=True)
for rel in ['tab5/main/tab5_relative.c','tab5/main/tab5_relative.h','tab5/main/tab5_measure.h']:
 (baseline/pathlib.Path(rel).name).write_bytes(subprocess.check_output(['git','-C',str(root),'show','c1798d82:'+rel]))
execute('old-video',[root/'tab5/tests/test_coherent_pair.c',baseline/'tab5_relative.c',root/'tab5/main/tab5_workspace.c'],baseline,['-DOLD_PAIR','-I'+str(root/'tab5/main')],1)
execute('new-core',[root/'tab5/tests/test_coherent_pair.c',root/'tab5/main/tab5_relative.c',root/'tab5/main/tab5_workspace.c',root/'tab5/main/tab5_measure.c'],root/'tab5/main')
text=(root/'tab5/main/main.c').read_text(encoding='utf-8');start=text.index('static void consume_sample(');i=text.index('{',start)+1;depth=1
while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
body=text[start:i]
shim=r'''
#include "tab5_services.h"
#include "tab5_workspace.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
typedef struct {unsigned index;tab5_measure_sensor_t value;} sample_event_t;
static tab5_relative_t relative_state;
static tab5_workspace_t workspace;
static bool samples_on;
static float battery;
static uint64_t now;
static uint64_t esp_timer_get_time(void){return now;}
int32_t tab5_services_scaled(double v,unsigned n){(void)v;(void)n;return 0;}
static void broadcast(const char *s){(void)s;}
static tab5_service_sample_t make_row(const sample_event_t *e,float v){(void)e;(void)v;return (tab5_service_sample_t){0};}
'''+body+r'''
int main(void){
 tab5_relative_config_t cfg;tab5_relative_config_default(&cfg);cfg.axes_confirmed=1;cfg.startup_mode=TAB5_MODE_LEVEL;tab5_relative_init(&relative_state,&cfg);
 static tab5_measure_core_t core;tab5_measure_core_init(&core,NULL,0);tab5_workspace_init(&workspace,NULL);
 tab5_relative_result_t r;
 for(unsigned n=1;n<=6;n++){
  uint16_t raw[2]={(uint16_t)(10000+n*100),10000};now=1000000+n*100000;
  assert(tab5_measure_core_sample(&core,0,raw,now));sample_event_t e={.index=0,.value=core.state.sensor[0]};consume_sample(&e);
  /* UI only polls BETWEEN A and B; it never sees the raw valid phase. */
  tab5_relative_read(&relative_state,&core.state,now+13333,&r);
  if(n==1)assert(!r.ready);else{assert(r.ready&&r.sequence[0]==n-1&&r.sequence[1]==n-1);assert(r.source[0][0]==(float)(n-1));}
  raw[0]+=100;now+=33333;assert(tab5_measure_core_sample(&core,1,raw,now));e=(sample_event_t){.index=1,.value=core.state.sensor[1]};consume_sample(&e);
 }
 tab5_measure_core_error(&core,0,TAB5_MEASURE_TIMEOUT);
 sample_event_t fault={.index=0,.value=core.state.sensor[0]};consume_sample(&fault);
 uint16_t recovered[2]={10700,10000};now=1700000;
 assert(tab5_measure_core_sample(&core,0,recovered,now));sample_event_t e={.index=0,.value=core.state.sensor[0]};consume_sample(&e);
 tab5_relative_read(&relative_state,&core.state,now+13333,&r);assert(!r.ready&&(r.quality&TAB5_PAIR_ERROR));
 now+=33333;assert(tab5_measure_core_sample(&core,1,recovered,now));e=(sample_event_t){.index=1,.value=core.state.sensor[1]};consume_sample(&e);
 tab5_relative_read(&relative_state,&core.state,now,&r);assert(r.ready);
 fprintf(stderr,"live history pairs=%llu gaps=%llu\n",(unsigned long long)workspace.live.live_pairs,(unsigned long long)workspace.live.live_gaps);
 assert(workspace.live.live_pairs==7&&workspace.live.live_gaps==1);
 puts("PASS exact consume_sample: real core init/sample, six complete acquisitions; UI phase-locked at A_new/B_old advances every accepted pair, no boot invention; transient error latched despite healthy read, both-new recovery");
}
'''
c=a.out/'consume.c';c.write_text(shim)
execute('live-consumer',[c,root/'tab5/main/tab5_relative.c',root/'tab5/main/tab5_measure.c',root/'tab5/main/tab5_workspace.c'],root/'tab5/main')
(a.out/'result.json').write_text(json.dumps({'pass':all(r['pass'] for r in results),'results':results},indent=2)+'\n')
