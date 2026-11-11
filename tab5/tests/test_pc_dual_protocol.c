/* Production command/owner bodies are generated without editing their logic. */
#define main retained_services_suite
#include "test_services.c"
#undef main
#include "tab5_ui.h"
#include "tab5_time.h"
#include "cJSON.h"
#include <setjmp.h>
typedef struct { bool signaled; } test_sem_t;
typedef test_sem_t *SemaphoreHandle_t;
#include "actions_generated.h"
#define pdTRUE 1
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(n) (n)
typedef struct { char line[3072]; } tx_line_t;
static int action_token,tx_token,app_token;
static void *actions=&action_token,*usb_tx=&tx_token,*app_mutex=&app_token;
static action_event_t pending;
static bool queued,queue_full,allocation_fail,in_owner;
static unsigned ui_events,owner_events,legacy_events;
static uint32_t usb_drops,rejected_actions;
static char output[16000],input[128];
static unsigned usb_iteration;
static jmp_buf usb_exit;
static void run_owner(void);
static void collect(void *ctx,const char *s){
 (void)ctx;assert(!in_owner&&!test_lock_depth);
 assert(strlen(output)+strlen(s)+2<sizeof(output));strcat(output,s);strcat(output,"\n");
}
static int xQueueSend(void *q,const void *value,int ticks){
 (void)ticks;
 if(q==usb_tx){collect(NULL,((const tx_line_t*)value)->line);return pdTRUE;}
 assert(q==actions);if(queue_full)return 0;assert(!queued);pending=*(const action_event_t*)value;queued=true;return pdTRUE;
}
static int xQueueReceive(void *q,void *value,int ticks){
 (void)ticks;if(q==usb_tx)return 0;
 assert(q==actions);if(!queued)return 0;*(action_event_t*)value=pending;queued=false;owner_events++;return pdTRUE;
}
static int uxQueueMessagesWaiting(void *q){(void)q;return 0;}
static SemaphoreHandle_t xSemaphoreCreateBinary(void){return allocation_fail?NULL:calloc(1,sizeof(test_sem_t));}
static void xSemaphoreTake(void *s,int ticks){
 (void)ticks;if(s==app_mutex)return;
 assert(queued&&!in_owner);in_owner=true;run_owner();in_owner=false;
 assert(((test_sem_t*)s)->signaled);
}
static void xSemaphoreGive(void *s){if(s!=app_mutex)((test_sem_t*)s)->signaled=true;}
static void vSemaphoreDelete(SemaphoreHandle_t s){assert(!queued);free(s);}
static bool usb_serial_jtag_is_connected(void){if(usb_iteration++)longjmp(usb_exit,1);return true;}
static int usb_serial_jtag_read_bytes(char *s,size_t cap,int ticks){(void)ticks;size_t n=strlen(input);assert(n<cap);memcpy(s,input,n);return (int)n;}
static int usb_serial_jtag_write_bytes(const char *s,size_t n,int ticks){(void)s;(void)ticks;return (int)n;}
static int usb_serial_jtag_wait_tx_done(int ticks){(void)ticks;return ESP_OK;}

typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
static tab5_relative_t relative_state;
static tab5_workspace_t workspace;
static bool app_recording,missing=true,workspace_missing=true;
static int fail_write,fail_commit,commits;
static tab5_relative_config_t committed,staged;
static tab5_workspace_config_t committed_workspace,staged_workspace;
static tab5_measure_snapshot_t actual;
static uint64_t now=1020000;
static char notice[160];
void tab5_measure_snapshot(tab5_measure_snapshot_t *s){*s=actual;}
static int64_t esp_timer_get_time(void){return now;}
static int nvs_open(const char *k,int mode,int *h){(void)k;(void)mode;*h=1;staged=committed;staged_workspace=committed_workspace;return 0;}
static int nvs_get_blob(int h,const char *k,void *out,size_t *size){
 (void)h;if(!strcmp(k,"workspace")){if(workspace_missing||*size!=sizeof(committed_workspace))return 1;memcpy(out,&committed_workspace,*size);return 0;}
 if(missing||*size!=sizeof(committed))return 1;
 memcpy(out,&committed,*size);return 0;
}
static int nvs_set_blob(int h,const char *k,const void *v,size_t size){
 (void)h;if(fail_write)return 1;
 if(!strcmp(k,"workspace")){assert(size==sizeof(staged_workspace));memcpy(&staged_workspace,v,size);}
 else{assert(size==sizeof(staged));memcpy(&staged,v,size);}return 0;
}
static int nvs_commit(int h){(void)h;if(fail_commit)return 1;committed=staged;committed_workspace=staged_workspace;missing=workspace_missing=false;commits++;return 0;}
static void nvs_close(int h){(void)h;}
#define FW_VERSION "1.8.5-esp32"
static bool light_theme,record_sync,physical_inputs_ready;
static char time_sync_status[128];
static uint64_t record_start_us;
static unsigned measurement_number;
static float battery=NAN;
static uint32_t sample_drops,storage_drops,route_drops;
static struct {bool cpu_known,known;unsigned cpu;size_t ram_used,ram_total,psram_used,psram_total;} resources;
static const char *filesystem_name(tab5_fs_type_t t){return t==TAB5_FS_FAT32?"FAT32":t==TAB5_FS_EXFAT?"exFAT":"unknown";}
static bool tab5_time_sync_busy(void){return false;}
static bool tab5_board_time_valid(void){return false;}
static bool tab5_board_rtc_valid(void){return false;}
static unsigned tab5_input_gpio_retries(void){return 0;}
#include "control_generated.h"
#include "owner_generated.h"
static void legacy_command(void *unused,const char *s,tab5_service_reply_fn emit,void *ctx){
 (void)unused;legacy_events++;if(!strcmp(s,"status"))status(emit,ctx);else emit(ctx,"ERR legacy test boundary");
}
#include "command_generated.h"
#include "usb_generated.h"
typedef struct {bool auth;uint32_t owner;const char *rx;} test_client_t;
static void tcp_reply(void *c,const char *s){collect(c,s);}
#include "tcp_generated.h"

static void send_command(unsigned transport,const char *s){
 bool reserved=control_busy;
 output[0]=0;
 if(transport==0){snprintf(input,sizeof(input),"%s\n",s);usb_iteration=0;if(!setjmp(usb_exit))usb_task(NULL);}
 else{test_client_t c={.auth=true,.owner=2,.rx=s};dispatch_tcp(&c);}
 assert(!queued&&control_busy==reserved);
}
static cJSON *get_status(unsigned transport){
 send_command(transport,"relative status");assert(strstr(output,"\nOK relative\n"));
 cJSON *j=cJSON_Parse(output);assert(j);return j;
}
static double num(cJSON *j,const char *key){cJSON *v=cJSON_GetObjectItem(j,key);assert(cJSON_IsNumber(v));return v->valuedouble;}
static void null(cJSON *j,const char *key){assert(cJSON_IsNull(cJSON_GetObjectItem(j,key)));}
static void fresh(void){
 for(unsigned i=0;i<2;i++){
  actual.sensor[i].address=i+2;actual.sensor[i].valid=true;actual.sensor[i].good++;
  actual.sensor[i].timestamp_us=now-1000+i*500;actual.sensor[i].last_error=TAB5_MEASURE_OK;actual.sensor[i].fail_streak=0;
  actual.sensor[i].decoded_x=2+(float)i*1.25f;actual.sensor[i].decoded_y=-3+(float)i*.5f;
  actual.sensor[i].zero_x=40;actual.sensor[i].zero_y=-30; /* REL must ignore head zeros. */
 }
 tab5_relative_result_t p;tab5_relative_observe(&relative_state,&actual,now,&p);
}
static void unchanged(unsigned transport,const char *s){
 tab5_relative_t old=relative_state;tab5_relative_config_t saved=committed;
 int count=commits;send_command(transport,s);assert(!strncmp(output,"ERR",3));
 assert(!memcmp(&old,&relative_state,sizeof(old))&&!memcmp(&saved,&committed,sizeof(saved))&&count==commits);
}
static void checks_for_transport(unsigned tr){
 missing=workspace_missing=true;app_recording=false;memset(&actual,0,sizeof(actual));load_relative_config();
 const char *bad[]={"instrument","instrument level extra","instrument unknown","relative", "relative roles 2 2","relative roles 1 3","relative roles 2 4","relative roles 02 3","relative roles 2 3 extra","relative axes true","relative axes confirm extra","relative zero extra","relative reset extra","relative status extra"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)unchanged(tr,bad[i]);
 unchanged(tr,"relative zero");
 cJSON *j=get_status(tr);null(j,"raw_x");null(j,"x");null(j,"ref_x");null(j,"ref_age_us");assert(num(j,"heads_supported")==2);cJSON_Delete(j);
 send_command(tr,"instrument level");assert(!strcmp(output,"OK instrument\n")&&relative_state.mode==TAB5_MODE_LEVEL&&committed.startup_mode==TAB5_MODE_LEVEL);
 send_command(tr,"relative axes confirm");assert(!strcmp(output,"OK relative\n")&&committed.axes_confirmed);
 fresh();j=get_status(tr);assert(num(j,"raw_x")==1.25&&num(j,"raw_y")==.5&&num(j,"x")==1.25&&num(j,"ref_x")==2&&num(j,"obj_x")==3.25);assert(num(j,"ref_us")==actual.sensor[0].timestamp_us&&num(j,"ref_seq")==actual.sensor[0].good);cJSON_Delete(j);
 /* Poll after just one new head: preserve the accepted pair's provenance. */
 actual.sensor[0].good++;actual.sensor[0].timestamp_us++;actual.sensor[0].decoded_x=10;
 uint64_t seq=relative_state.pair.sequence[0];j=get_status(tr);assert(num(j,"ref_seq")==seq&&num(j,"ref_x")==2&&num(j,"raw_x")==1.25);cJSON_Delete(j);
 uint32_t gen=relative_state.generation;send_command(tr,"relative zero");assert(!strcmp(output,"OK relative\n")&&relative_state.offset_valid&&relative_state.generation==gen+1&&!relative_state.pair_known);
 assert(actual.sensor[0].zero_x==40&&actual.sensor[1].zero_y==-30);
 j=get_status(tr);null(j,"x");assert(num(j,"offset_x")==1.25);cJSON_Delete(j);
 fresh();j=get_status(tr);assert(num(j,"raw_x")==1.25&&num(j,"x")==0);cJSON_Delete(j);
 now+=500001;j=get_status(tr);null(j,"x");null(j,"raw_x");assert(((unsigned)num(j,"quality")&TAB5_PAIR_STALE)&&num(j,"ref_x")==2);cJSON_Delete(j);
 unchanged(tr,"relative zero");fresh();
 actual.sensor[0].decoded_x=NAN;actual.sensor[0].decoded_y=INFINITY;actual.sensor[1].decoded_x=-INFINITY;
 actual.sensor[0].good++;actual.sensor[1].good++;tab5_relative_result_t r;tab5_relative_observe(&relative_state,&actual,now,&r);
 j=get_status(tr);null(j,"ref_x");null(j,"ref_y");null(j,"obj_x");null(j,"raw_x");null(j,"x");assert(!strstr(output,"NaN")&&!strstr(output,"Infinity"));cJSON_Delete(j);fresh();
 fail_commit=1;unchanged(tr,"instrument heel");unchanged(tr,"relative roles 3 2");unchanged(tr,"relative axes reset");fail_commit=0;
 fail_write=1;unchanged(tr,"instrument heel");unchanged(tr,"relative roles 3 2");fail_write=0;
 const char *mutations[]={"instrument heel","instrument level","relative roles 3 2","relative axes reset","relative axes confirm","relative zero","relative reset"};
 for(unsigned busy=0;busy<9;busy++){
  app_recording=busy==0;recording=busy==1;stopping=busy==2;files_busy=busy==3;ota.active=busy==4;ota.committed=busy==5;test_lock_busy=busy==6;control_busy=busy==7;
  transfer.owner=busy==8?99:0;
  for(unsigned k=0;k<sizeof(mutations)/sizeof(*mutations);k++)unchanged(tr,mutations[k]);
  j=get_status(tr);assert(cJSON_IsObject(j));cJSON_Delete(j);
  control_busy=false;transfer.owner=0;
  app_recording=recording=stopping=files_busy=ota.active=ota.committed=false;test_lock_busy=0;
 }
 queue_full=true;unchanged(tr,"instrument heel");queue_full=false;
 allocation_fail=true;unchanged(tr,"relative reset");allocation_fail=false;
 gen=relative_state.generation;send_command(tr,"relative roles 3 2");assert(relative_state.config.reference_index==1&&committed.reference_index==1&&relative_state.generation==gen+1&&!relative_state.offset_valid);
 fresh();j=get_status(tr);assert(num(j,"raw_x")==-1.25&&num(j,"ref_x")==3.25);cJSON_Delete(j);
 send_command(tr,"relative axes reset");assert(!relative_state.config.axes_confirmed&&!committed.axes_confirmed);send_command(tr,"relative axes confirm");fresh();send_command(tr,"relative zero");assert(relative_state.offset_valid);
 load_relative_config();assert(relative_state.mode==TAB5_MODE_LEVEL&&relative_state.config.reference_index==1&&relative_state.config.axes_confirmed&&!relative_state.offset_valid);
 fresh();send_command(tr,"relative reset");assert(!relative_state.offset_valid&&!relative_state.pair_known);send_command(tr,"instrument heel");load_relative_config();assert(relative_state.mode==TAB5_MODE_HEEL);
 send_command(tr,"status");j=cJSON_Parse(output);assert(j);assert(cJSON_IsTrue(cJSON_GetObjectItem(j,"dual_mode_cap"))&&cJSON_IsTrue(cJSON_GetObjectItem(j,"instrument_control_cap"))&&cJSON_IsTrue(cJSON_GetObjectItem(j,"relative_data_cap"))&&cJSON_IsTrue(cJSON_GetObjectItem(j,"file_media_cap")));assert(!strcmp(cJSON_GetObjectItem(j,"instrument")->valuestring,"heel"));cJSON_Delete(j);
 printf("PASS %s actual entry/queue/owner: strict syntax, NVS/reboot, transient REL, source provenance, nullable JSON, generation, guards recording/draining/transfer/OTA/lock, busy read-only status\n",tr?"TCP":"USB");
}
int main(void){
 _mkdir("test-sd");_mkdir("test-usb");tab5_services_config_t cfg={.command=command};assert(tab5_services_init(&cfg)==ESP_OK);pump_initial_media();
 test_client_t unauth={.owner=3,.rx="instrument level"};dispatch_tcp(&unauth);assert(strstr(output,"ERR auth"));
 checks_for_transport(0);checks_for_transport(1);
 for(unsigned i=0;i<2;i++){char path[64];snprintf(path,sizeof(path),"test-%s/same.CSV",i?"usb":"sd");FILE *f=fopen(path,"wb");assert(f);fputs(i?"usb":"sd",f);fclose(f);}
 for(unsigned tr=0;tr<2;tr++)for(unsigned i=0;i<2;i++){
  char cmd[64];snprintf(cmd,sizeof(cmd),"get %s same.CSV",i?"usb":"sd");send_command(tr,cmd);
  assert(strstr(output,i?"G,same.CSV,3,0":"G,same.CSV,2,0")&&transfer.medium==i);
  if(tr)tab5_services_poll(2,collect,NULL);
  assert(strstr(output,i?"D,dXNi":"D,c2Q="));
  unchanged(tr,"instrument level");send_command(tr,"get abort");
  snprintf(cmd,sizeof(cmd),"files %s",i?"usb":"sd");send_command(tr,cmd);assert(transfer.medium==i);send_command(tr,"get abort");
 }
 send_command(0,"get same.CSV");assert(transfer.medium==TAB5_SD);send_command(0,"get abort");send_command(1,"files");assert(transfer.medium==TAB5_SD);send_command(1,"get abort");
 send_command(0,"files invalid");assert(strstr(output,"ERR medium")&&!transfer.owner&&!files_busy);
 media[TAB5_SD].status.mounted=false;
 for(unsigned tr=0;tr<2;tr++){
  send_command(tr,"files sd");assert(strstr(output,"ERR")&&!transfer.owner&&!files_busy);
  send_command(tr,"get sd same.CSV");assert(strstr(output,"ERR")&&!transfer.owner&&!files_busy);
  send_command(tr,"files");assert(transfer.medium==TAB5_USB);send_command(tr,"get abort");
 }
 media[TAB5_SD].status.mounted=true;
 assert(owner_events>100&&legacy_events==2&&ui_events==0);
 tab5_ui_action_t ui={.kind=TAB5_UI_REL_CAPTURE};ui_callback(&ui,NULL);
 assert(queued&&!pending.protocol&&pending.ui.kind==TAB5_UI_REL_CAPTURE);run_owner();
 queue_full=true;ui_callback(&ui,NULL);assert(!queued&&(rejected_actions&(1u<<TAB5_UI_REL_CAPTURE)));queue_full=false;
 assert(physical_record(true,0)&&queued&&!pending.protocol&&pending.ui.kind==TAB5_UI_START);run_owner();
 assert(physical_record(false,0)&&queued&&pending.ui.kind==TAB5_UI_STOP);run_owner();assert(ui_events==3);
 puts("PASS additive SD/USB files/get and legacy selection; real services reservation; no hardware access");
 return 0;
}
