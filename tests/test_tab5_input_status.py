"""Execute exact production command formatter with actual bundled cJSON.

GPIO poll task is covered separately by test_tab5_input_gpio.py. This gate
checks the read-only protocol, unavailable values, queue getter and line size.
"""
import json
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
main = (ROOT / 'tab5/main/main.c').read_text(encoding='utf-8')
formatter = main[main.index('static void input_status('):main.index('static void command(')]
assert 'else if(strcmp(line,"input status")==0)input_status(reply,ctx);' in main
cjson = ROOT / '.tools/esp-idf/components/json/cJSON'
with tempfile.TemporaryDirectory(prefix='tab5-input-status-') as tmp:
    d = pathlib.Path(tmp)
    (d / 'esp_err.h').write_text('typedef int esp_err_t;\n')
    shim = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tab5_input_gpio.h"
#include "cJSON.h"
typedef void (*tab5_service_reply_fn)(void *,const char *);
typedef struct { uint32_t posted,consumed,rejected,pending; } tab5_native_input_stats_t;
static tab5_gpio_status source;
static unsigned snapshots,queues;
void tab5_input_gpio_snapshot(tab5_gpio_status *out){*out=source;snapshots++;}
void tab5_native_port_input_stats(tab5_native_input_stats_t *out){
 *out=(tab5_native_input_stats_t){100,97,2,3};queues++;
}
int64_t esp_timer_get_time(void){return 1000000;}
static void reply(void *ctx,const char *line){(void)ctx;assert(strlen(line)<3072);puts(line);}
'''
    shim += formatter
    shim += r'''
int main(void){
 source=(tab5_gpio_status){.enabled=true,.initialized=true,.sampled=true,
  .config={3,4,2,48,30,5},.raw={0,1,0,1},.sampled_ms=990,.samples=UINT32_MAX,
  .poll_gap_ms=5,.poll_gap_max_ms=25,.raw_changes={4,5,6,7},
  .ab_changes=9,.invalid_ab=2,.detent_positive=1,.detent_negative=2,
  .attempts={3,0,2},.accepted={2,0,1},.rejected={1,0,1},
  .event_ms={970,0,950},.attempt_ms={980,0,960},
  .event_age_ms={10,0,10},.event_age_max_ms={20,0,10}};
 tab5_gpio_status before=source;input_status(reply,NULL);
 assert(memcmp(&before,&source,sizeof(source))==0);
 source=(tab5_gpio_status){0};input_status(reply,NULL); /* disabled */
 source.enabled=true;source.start_error=3;input_status(reply,NULL); /* failed init */
 /* Worst possible decimal widths must fit existing tx_line_t, no truncation. */
 source=before;source.sampled_ms=0;
 uint32_t *arrays[]={source.attempts,source.accepted,source.rejected,source.event_ms,
  source.attempt_ms,source.event_age_ms,source.event_age_max_ms};
 for(unsigned a=0;a<7;a++)for(unsigned i=0;i<3;i++)arrays[a][i]=UINT32_MAX;
 input_status(reply,NULL);assert(snapshots==4 && queues==4);
 return 0;
}
'''
    (d / 'test.c').write_text(shim, encoding='utf-8')
    exe = d / 'test.exe'
    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(d), '-I'+str(ROOT/'tab5/components/tab5_input'),
                    '-I'+str(cjson), str(d/'test.c'), str(cjson/'cJSON.c'),
                    '-o', str(exe)], check=True)
    lines = subprocess.check_output([str(exe)], text=True).splitlines()
    assert len(lines) == 8 and all(lines[i] == 'OK input status' for i in (1, 3, 5, 7))
    live, disabled, failed, worst = [json.loads(lines[i]) for i in (0, 2, 4, 6)]
    assert live['enabled'] and live['initialized'] and live['raw_available'] and live['read_only']
    assert live['gpio'] == dict(a=3, b=4, press=2, rec=48)
    assert live['raw'] == dict(a=0, b=1, press=0, rec=1)
    assert live['changes'] == dict(a=4, b=5, press=6, rec=7)
    assert live['age_ms'] == 10 and live['poll_gap_max_ms'] == 25
    assert live['decoder']['positive'] == 1 and live['decoder']['negative'] == 2
    assert live['decoder']['invalid_ab'] == 2
    assert live['callbacks']['encoder']['event_age_max_ms'] == 20
    assert live['callbacks']['button']['event_ms'] is None
    assert live['ui_queue'] == dict(posted=100, consumed=97, rejected=2, pending=3)
    for s in (disabled, failed):
        assert not s['raw_available'] and not s['initialized']
        assert all(v is None for v in s['gpio'].values())
        assert all(v is None for v in s['raw'].values())
        assert s['sample_ms'] is None and s['age_ms'] is None
    assert not disabled['enabled'] and failed['enabled'] and failed['start_error'] == 3
    assert worst['callbacks']['encoder']['attempts'] == 2**32-1
    print('input status production JSON: PASS (live/disabled/init failure/queue/counters/read-only/3072-byte line)')
