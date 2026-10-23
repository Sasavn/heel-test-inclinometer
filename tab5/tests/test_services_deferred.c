/* Real shared startup/worker/retry/CSV/OTA core with injected mount boundary.
 * No mock of worker eligibility, record lifecycle or OTA state machine. */
#include "test_services_startup_hooks.h"
#define main retained_services_core_suite
#include "test_services.c"
#undef main
static tab5_services_startup_probe_t probe;
void __cyg_profile_func_enter(void *fn,void *caller) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *fn,void *caller) __attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *fn,void *caller){(void)caller;if(fn==(void*)ml)probe.medium_lock_entries++;}
void __cyg_profile_func_exit(void *fn,void *caller){(void)fn;(void)caller;}
static void explicit_begin(unsigned owner){
 char cmd[240];image_hash();snprintf(cmd,sizeof(cmd),"ota begin tab5 esp32p4 2 %s %u %s",VERSION,(unsigned)sizeof(image),digest);
 CHECK(tab5_services_line(owner,cmd,true,reply,NULL));CHECK(!strcmp(answer,"OK ota offset=0"));
}
static void upload_owner(unsigned owner){
 for(unsigned off=0;off<sizeof(image);off+=256){char cmd[1100];int pos=snprintf(cmd,sizeof(cmd),"ota chunk %u ",off);
  for(unsigned j=0;j<256;j++)sprintf(cmd+pos+j*2,"%02x",image[off+j]);
  CHECK(tab5_services_line(owner,cmd,true,reply,NULL));CHECK(!strncmp(answer,"OK ota offset=",14));
 }
}
static void firmware_fixture(void){
 memset(image,0,sizeof(image));image[0]=0xe9;image[1]=3;image[12]=0x12;image[15]=100;image[17]=0x8f;image[18]=1;
 uint32_t magic=0xabcd5432,segment=256;memcpy(image+28,&segment,4);memcpy(image+32,&magic,4);strcpy((char*)image+48,VERSION);strcpy((char*)image+80,"krenomer-esp32");
 const char *names[]={"firmware.bin","update.bin","native-1.6.15.bin"};
 for(unsigned i=0;i<3;i++){char path[128];snprintf(path,sizeof(path),"test-sd/%s",names[i]);FILE *f=fopen(path,"wb");CHECK(f);CHECK(fwrite(image,1,sizeof(image),f)==sizeof(image));CHECK(!fclose(f));}
}
static void csv_usb(const char *name,bool inflight){
 unsigned locks=probe.medium_lock_entries;CHECK(tab5_services_record_start(name)==ESP_OK);
 CHECK(probe.medium_lock_entries==locks+1); /* USB only; no SD medium-lock call */
 CHECK(recording&&media[TAB5_USB].status.recording&&!media[TAB5_SD].status.recording);
 tab5_service_sample_t sample={.sensor=2,.date="23.10.2026",.time="01:23:45",.raw_x=1.25,.raw_y=-2.5,.battery=NAN,.ms=1234},copy=sample;
 char expected[320];CHECK(tab5_services_format_csv(expected,sizeof(expected),&sample)>0);CHECK(strstr(expected,"1,25;-2,50;")!=NULL);
 CHECK(tab5_services_submit(&sample)==ESP_OK);CHECK(!memcmp(&sample,&copy,sizeof(sample)));
 locks=probe.medium_lock_entries;tab5_services_record_stop();CHECK(probe.medium_lock_entries==locks&&!recording);
 for(unsigned i=0;i<4&&media[TAB5_USB].status.recording;i++)storage_worker_step(TAB5_USB);
 CHECK(!media[TAB5_USB].status.recording&&media[TAB5_USB].status.written==1);
 /* Nonparticipating in-flight SD may not keep the recording session busy. */
 if(inflight)CHECK(medium_busy[TAB5_SD]);
 CHECK(!stopping);
 char path[160];snprintf(path,sizeof(path),"test-usb/%s_D2.CSV",name);FILE *f=fopen(path,"rb");CHECK(f);char contents[1024];size_t n=fread(contents,1,sizeof(contents)-1,f);contents[n]=0;fclose(f);
 /* Text-mode host writes CRLF; compare logical rows without weakening columns. */
 char normalized[1024];unsigned at=0;for(size_t i=0;i<n;i++)if(contents[i]!='\r')normalized[at++]=contents[i];normalized[at]=0;
 char wanted[1024];snprintf(wanted,sizeof(wanted),"%s%s",TAB5_SERVICE_CSV_HEADER,expected);CHECK(!strcmp(normalized,wanted));
}
static void healthy_snapshots(void){
 CHECK(tab5_services_record_start("healthy_snapshot")==ESP_OK);
 network_status=(tab5_network_snapshot_t){.initialized=true,.ap_started=true,.tcp_listening=true,.clients=2};strcpy(network_status.ap_ip,"192.168.4.1");
 tab5_medium_status_t before_status,after_status;tab5_network_snapshot_t before_net,after_net;
 tab5_services_status(TAB5_USB,&before_status);tab5_services_network_snapshot(&before_net);CHECK(before_status.mounted&&before_status.recording&&!before_status.failed);
 test_lock_busy=1;tab5_services_status(TAB5_USB,&after_status);tab5_services_network_snapshot(&after_net);test_lock_busy=0;
 CHECK(!after_status.failed&&after_status.error!=ESP_ERR_TIMEOUT);
 if(after_status.known){CHECK(after_status.mounted&&after_status.recording&&after_status.error==before_status.error);CHECK(after_status.written==before_status.written&&after_status.dropped==before_status.dropped);}
 CHECK(after_net.last_error!=ESP_ERR_TIMEOUT);
 if(after_net.known){CHECK(after_net.initialized==before_net.initialized&&after_net.ap_started==before_net.ap_started&&after_net.tcp_listening==before_net.tcp_listening&&after_net.clients==before_net.clients);CHECK(after_net.last_error==before_net.last_error&&!strcmp(after_net.ap_ip,before_net.ap_ip));}
 CHECK(recording&&media[TAB5_USB].status.recording);
 tab5_services_record_stop();storage_worker_step(TAB5_USB);CHECK(!stopping);
}
static void paused_sd(void){
 CHECK(medium_busy[TAB5_SD]&&!media[TAB5_SD].status.mounted&&!test_lock_depth&&!files_busy);
 tab5_medium_status_t usb;tab5_services_status(TAB5_USB,&usb);CHECK(usb.mounted&&!usb.failed);
 CHECK(tab5_services_line(1,"ota info",true,reply,NULL));CHECK(!strcmp(answer,"OK"));
 explicit_begin(1);CHECK(ota.active);test_now+=30001;maintenance();CHECK(!ota.active&&!test_erases&&!test_boot_selected);
 csv_usb("pause_first",true);csv_usb("pause_restart",true);
 healthy_snapshots();CHECK(medium_busy[TAB5_SD]&&!test_lock_depth);probe.inflight_checked=true;
}
static esp_err_t mount_fault(unsigned medium){
 CHECK(medium<2);probe.mounts[medium]++;CHECK(medium_busy[medium]&&!test_lock_depth);
 mounted_fs[medium]=(tab5_fs_type_t)probe.filesystem[medium];
 if(medium==TAB5_SD&&probe.pause_sd)paused_sd();
 return probe.mount_error[medium]?probe.mount_error[medium]:(test_present[medium]?ESP_OK:ESP_FAIL);
}
static void init_shared(void){
 _mkdir("test-sd");_mkdir("test-usb");strcpy(test_current_version,TAB5_TEST_RELEASE_VERSION);firmware_fixture();
 probe.filesystem[0]=probe.filesystem[1]=TAB5_FS_FAT32;
 test_mount_hook=mount_fault;tab5_services_config_t cfg={0};CHECK(tab5_services_init(&cfg)==ESP_OK);
 CHECK(initialized&&storage_ready&&!test_usb_starts&&!probe.mounts[0]&&!probe.mounts[1]);
 CHECK(!test_erases&&!test_boot_selected);test_state_blank=0;test_slot_valid[0]=1;
 unsigned observed=probe.medium_lock_entries;CHECK(ml(TAB5_USB));mu(TAB5_USB);CHECK(probe.medium_lock_entries==observed+1);
}
static unsigned listed_csv;
static uint64_t injected_file_size;
static bool injected_directory,injected_stat_fail;
static bool file_metadata(unsigned medium,const char *name,uint64_t *size,bool *directory){CHECK(medium<2&&name);if(injected_stat_fail)return false;*size=injected_file_size;*directory=injected_directory;return true;}
static void files_reply(void *ctx,const char *s){if(!strncmp(s,"F,",2)&&strstr(s,"integration_D"))listed_csv++;reply(ctx,s);}
static void fs_integration(void){
 storage_worker_step(TAB5_SD);storage_worker_step(TAB5_USB);
 for(unsigned medium=0;medium<2;medium++){tab5_medium_status_t status;tab5_services_status((tab5_medium_t)medium,&status);CHECK(status.known&&status.mounted&&!status.failed&&status.fs_type==(tab5_fs_type_t)probe.filesystem[medium]);}
 unsigned locks=probe.medium_lock_entries;CHECK(tab5_services_record_start("integration")==ESP_OK);CHECK(probe.medium_lock_entries==locks+2);
 tab5_service_sample_t sample={.sensor=2,.date="23.10.2026",.time="02:34:56",.raw_x=1.25,.raw_y=-2.5,.battery=12.4,.ms=1234};char expected[2][320];
 for(unsigned sensor=2;sensor<=3;sensor++){sample.sensor=sensor;CHECK(tab5_services_format_csv(expected[sensor-2],sizeof(expected[0]),&sample)>0);CHECK(tab5_services_submit(&sample)==ESP_OK);}
 locks=probe.medium_lock_entries;tab5_services_record_stop();CHECK(probe.medium_lock_entries==locks);
 for(unsigned i=0;i<8&&stopping;i++){storage_worker_step(TAB5_SD);storage_worker_step(TAB5_USB);}CHECK(!stopping);
 for(unsigned medium=0;medium<2;medium++)for(unsigned sensor=2;sensor<=3;sensor++){
  char path[160];snprintf(path,sizeof(path),"%s/integration_D%u.CSV",media[medium].root,sensor);FILE *f=fopen(path,"rb");CHECK(f);char data[1024],normal[1024],wanted[1024];size_t n=fread(data,1,sizeof(data)-1,f);fclose(f);unsigned used=0;for(size_t i=0;i<n;i++)if(data[i]!='\r')normal[used++]=data[i];normal[used]=0;snprintf(wanted,sizeof(wanted),"%s%s",TAB5_SERVICE_CSV_HEADER,expected[sensor-2]);CHECK(!strcmp(normal,wanted));
 }
 for(unsigned medium=0;medium<2;medium++){
  /* Force real CLI selection of each mounted medium in turn. */
  if(medium==TAB5_USB)media[TAB5_SD].status.failed=true;
  listed_csv=0;CHECK(tab5_services_line(1,"files",true,files_reply,NULL));unsigned guard=0;while(files_busy&&guard++<50)tab5_services_poll(1,files_reply,NULL);CHECK(!files_busy&&listed_csv==2);
  line(1,"get integration_D2.CSV");guard=0;while(files_busy&&guard++<50)tab5_services_poll(1,reply,NULL);CHECK(!files_busy&&!strcmp(answer,"OK")&&received_bytes==final_bytes&&received_crc==final_crc&&received_bytes>strlen(TAB5_SERVICE_CSV_HEADER));
 }
 CHECK(!test_erases&&!test_boot_selected);
}
int main(int argc,char **argv){
 CHECK(argc==2);const char *scenario=argv[1];init_shared();test_now=2500;
 if(!strcmp(scenario,"fs-fat32")||!strcmp(scenario,"fs-exfat")||!strcmp(scenario,"fs-mixed")){
  if(!strcmp(scenario,"fs-exfat"))probe.filesystem[0]=probe.filesystem[1]=TAB5_FS_EXFAT;
  if(!strcmp(scenario,"fs-mixed"))probe.filesystem[TAB5_USB]=TAB5_FS_EXFAT;
  fs_integration();
 }else if(!strcmp(scenario,"fs-refused")){
  probe.mount_error[TAB5_SD]=ESP_FAIL;probe.filesystem[TAB5_SD]=TAB5_FS_UNKNOWN;storage_worker_step(TAB5_SD);CHECK(media[0].status.failed&&!media[0].status.mounted&&media[0].status.fs_type==TAB5_FS_UNKNOWN);storage_worker_step(TAB5_USB);csv_usb("refused_sd_usb",false);CHECK(!test_erases&&!test_boot_selected);
 }else if(!strcmp(scenario,"wire-file-limits")){
  storage_worker_step(TAB5_USB);test_file_size_hook=file_metadata;
  const uint64_t sizes[]={0,1,INT32_MAX,(uint64_t)INT32_MAX+1,UINT32_MAX,(uint64_t)UINT32_MAX+1,UINT64_MAX};
  for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++){injected_file_size=sizes[i];uint32_t out=0xdeadbeef;bool expect=sizes[i]<=UINT32_MAX&&(sizeof(off_t)>4||sizes[i]<=INT32_MAX);CHECK(file_wire_size(TAB5_USB,"firmware.bin",&out)==expect);if(expect)CHECK(out==sizes[i]);else CHECK(out==0xdeadbeef);}
  injected_file_size=1;injected_directory=true;uint32_t out=0xdeadbeef;CHECK(!file_wire_size(TAB5_USB,"dir",&out)&&out==0xdeadbeef);injected_directory=false;injected_stat_fail=true;CHECK(!file_wire_size(TAB5_USB,"firmware.bin",&out));injected_stat_fail=false;
  injected_file_size=(uint64_t)UINT32_MAX+1;line(1,"get firmware.bin");CHECK(!strcmp(answer,"ERR file")&&!files_busy&&!ota.active&&!test_erases);CHECK(!file_wire_size((unsigned)TAB5_USB+1,"firmware.bin",&out));CHECK(!file_wire_size(TAB5_USB,"../escape",&out));
  puts("WIRE_METADATA_LIMITS actual file_wire_size + CLI get: original64bit metadata hook, signed off_t + uint32 bounds, no large-file-on-disk claim PASS");
 }else if(!strcmp(scenario,"input-limits")){
  storage_worker_step(TAB5_USB);CHECK(tab5_services_record_start("../escape")==ESP_ERR_INVALID_ARG);char long_name[80];memset(long_name,'a',sizeof(long_name)-1);long_name[sizeof(long_name)-1]=0;CHECK(tab5_services_record_start(long_name)==ESP_ERR_INVALID_ARG);
  char oversized[TAB5_SERVICE_LINE_MAX+2];memset(oversized,'x',sizeof(oversized)-1);oversized[sizeof(oversized)-1]=0;memcpy(oversized,"ota ",4);CHECK(tab5_services_line(1,oversized,true,reply,NULL));CHECK(!strcmp(answer,"ERR auth/line"));CHECK(!ota.active&&!test_erases&&!test_boot_selected);
 }else if(!strcmp(scenario,"optional-usb-failure")){
  test_usb_start_error=0x1703;storage_worker_step(TAB5_USB);tab5_medium_status_t st;tab5_services_status(TAB5_USB,&st);CHECK(st.failed&&!st.mounted&&st.error==0x1703);
  CHECK(initialized&&storage_ready);CHECK(tab5_services_line(1,"ota info",true,reply,NULL));CHECK(!strcmp(answer,"OK"));explicit_begin(1);line(1,"ota abort");CHECK(!ota.active);
 }else if(!strcmp(scenario,"paused-sd")){
  storage_worker_step(TAB5_USB);CHECK(media[TAB5_USB].status.mounted);probe.pause_sd=true;probe.mount_error[TAB5_SD]=ESP_ERR_TIMEOUT;
  storage_worker_step(TAB5_SD);CHECK(probe.inflight_checked);CHECK(!medium_busy[TAB5_SD]&&media[TAB5_SD].status.failed&&media[TAB5_SD].status.error==ESP_ERR_TIMEOUT);
 }else if(!strcmp(scenario,"sd-timeout")){
  probe.mount_error[TAB5_SD]=ESP_ERR_TIMEOUT;storage_worker_step(TAB5_SD);CHECK(probe.mounts[0]==1&&media[0].status.failed&&media[0].status.error==ESP_ERR_TIMEOUT);
  storage_worker_step(TAB5_USB);CHECK(media[1].status.mounted);csv_usb("timeout_usb",false);CHECK(initialized&&storage_ready);
 }else if(!strcmp(scenario,"snapshot-contention")){
  storage_worker_step(TAB5_USB);healthy_snapshots();
 }else if(!strcmp(scenario,"sd-bin-ignored")){
  for(unsigned i=0;i<6;i++){storage_worker_step(TAB5_SD);storage_worker_step(TAB5_USB);maintenance();test_now+=5000;}
  CHECK(media[0].status.mounted);CHECK(!ota.active&&!ota.committed&&!test_erases&&!test_flash_offset&&!test_boot_selected&&!test_restarted);
  const char *names[]={"firmware.bin","update.bin","native-1.6.15.bin"};
  for(unsigned i=0;i<3;i++){char path[128];snprintf(path,sizeof(path),"test-sd/%s",names[i]);FILE *f=fopen(path,"rb");CHECK(f);uint8_t readback[sizeof(image)];CHECK(fread(readback,1,sizeof(readback),f)==sizeof(readback));fclose(f);CHECK(!memcmp(readback,image,sizeof(image)));}
  line(1,"get firmware.bin");unsigned guard=0;while(files_busy&&guard++<50)tab5_services_poll(1,reply,NULL);CHECK(!files_busy&&!strcmp(answer,"OK")&&received_bytes==sizeof(image)&&received_crc==crc32_update(0,image,sizeof(image)));CHECK(!test_erases&&!test_boot_selected&&!ota.active&&!ota.committed);
 }else if(!strcmp(scenario,"explicit-usb")||!strcmp(scenario,"explicit-tcp")){
  unsigned owner=!strcmp(scenario,"explicit-usb")?1:2;explicit_begin(owner);CHECK(!test_erases);upload_owner(owner);CHECK(test_erases==1&&!test_boot_selected);
  CHECK(tab5_services_line(owner,"ota end",true,reply,NULL));CHECK(!strcmp(answer,"OK ota committed scheduled reboot")&&test_boot_selected==1);
  test_now+=10000;maintenance();CHECK(!test_restarted);tab5_services_response_flushed(owner);test_now+=500;maintenance();CHECK(test_restarted==1);
 }else if(!strcmp(scenario,"auth-guard")){
  CHECK(tab5_services_line(2,"ota info",false,reply,NULL));CHECK(!strcmp(answer,"ERR auth/line"));CHECK(tab5_services_line(0,"ota info",true,reply,NULL));CHECK(!strcmp(answer,"ERR auth/line"));CHECK(!ota.active&&!test_erases&&!test_boot_selected);
 }else CHECK(!"unknown scenario");
 printf("DEFERRED_SERVICES %s %u checks PASS actual shared init/worker/record/OTA, mounts=%u/%u\n",scenario,checks,probe.mounts[0],probe.mounts[1]);return 0;
}
