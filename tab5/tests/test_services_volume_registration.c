/* Real registration wrapper + Windows threads. Inject the SDK's split
 * get-free/register boundary: SD pauses after choosing 0, before registration. */
#define main service_core_suite_main
#include "test_services.c"
#undef main
/* The shared suite's CHECK increments a plain counter. This threaded fixture
 * uses a counter-free check so its own assertions introduce no data race. */
#undef CHECK
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);ExitProcess(1);}}while(0)

static HANDLE sd_selected,release_sd,usb_attempted,usb_entered;
static int registry_owner[2]={-1,-1};
static LONG registrations,max_registrations;
static bool pause_sd=true,fail_registration;
static int sdk_mount_probe(unsigned i){
 CHECK(!test_lock_depth);
 LONG active=InterlockedIncrement(&registrations);
 if(active>max_registrations)InterlockedExchange(&max_registrations,active);
 if(i==TAB5_USB)SetEvent(usb_entered);
 /* Re-mount includes unregister, which must use this same registry lock. */
 for(unsigned d=0;d<2;d++)if(registry_owner[d]==(int)i)registry_owner[d]=-1;
 if(fail_registration){InterlockedDecrement(&registrations);return ESP_FAIL;}
 unsigned chosen=0;while(chosen<2&&registry_owner[chosen]!=-1)chosen++;
 CHECK(chosen<2);
 if(i==TAB5_SD&&pause_sd){
  pause_sd=false;SetEvent(sd_selected);
  CHECK(WaitForSingleObject(release_sd,5000)==WAIT_OBJECT_0);
 }
 registry_owner[chosen]=(int)i;mounted_drive[i]=(uint8_t)chosen;
 InterlockedDecrement(&registrations);return ESP_OK;
}
static DWORD WINAPI sd_worker(void *arg){(void)arg;return mount_registered(TAB5_SD);}
static DWORD WINAPI usb_worker(void *arg){(void)arg;SetEvent(usb_attempted);return mount_registered(TAB5_USB);}
int main(void){
 tab5_medium_status_t s;tab5_services_status(TAB5_SD,&s);CHECK(s.volume_id==0xff);
 _mkdir("test-sd");_mkdir("test-usb");
 tab5_services_config_t cfg={0};CHECK(tab5_services_init(&cfg)==ESP_OK);
 tab5_services_status(TAB5_USB,&s);CHECK(s.volume_id==0xff&&!s.mounted);
 test_mount_hook=sdk_mount_probe;
 sd_selected=CreateEvent(NULL,TRUE,FALSE,NULL);release_sd=CreateEvent(NULL,TRUE,FALSE,NULL);
 usb_attempted=CreateEvent(NULL,TRUE,FALSE,NULL);usb_entered=CreateEvent(NULL,TRUE,FALSE,NULL);
 CHECK(sd_selected&&release_sd&&usb_attempted&&usb_entered);
 HANDLE sd=CreateThread(NULL,0,sd_worker,NULL,0,NULL);CHECK(sd!=NULL);
 CHECK(WaitForSingleObject(sd_selected,5000)==WAIT_OBJECT_0);
 HANDLE usb=CreateThread(NULL,0,usb_worker,NULL,0,NULL);CHECK(usb!=NULL);
 CHECK(WaitForSingleObject(usb_attempted,5000)==WAIT_OBJECT_0);
 /* USB worker is running, but cannot enter get-drive while SD holds 0. */
 CHECK(WaitForSingleObject(usb_entered,100)==WAIT_TIMEOUT);
 SetEvent(release_sd);
 CHECK(WaitForSingleObject(sd,5000)==WAIT_OBJECT_0);
 CHECK(WaitForSingleObject(usb,5000)==WAIT_OBJECT_0);
 DWORD sd_result=1,usb_result=1;GetExitCodeThread(sd,&sd_result);GetExitCodeThread(usb,&usb_result);
 CHECK(sd_result==ESP_OK&&usb_result==ESP_OK);
 CHECK(max_registrations==1&&registrations==0);
 CHECK(mounted_drive[TAB5_SD]==0&&mounted_drive[TAB5_USB]==1);
 CHECK(registry_owner[0]==TAB5_SD&&registry_owner[1]==TAB5_USB);
 CloseHandle(sd);CloseHandle(usb);
 /* Exercise the actual worker/status publication as well as the wrapper. */
 test_now=2000;storage_worker_step(TAB5_SD);storage_worker_step(TAB5_USB);
 tab5_medium_status_t sd_status,usb_status;
 tab5_services_status(TAB5_SD,&sd_status);tab5_services_status(TAB5_USB,&usb_status);
 CHECK(sd_status.mounted&&usb_status.mounted);
 CHECK(sd_status.volume_id==0&&usb_status.volume_id==1);
 fail_registration=true;CHECK(tab5_services_retry(TAB5_USB)==ESP_OK);storage_worker_step(TAB5_USB);
 tab5_services_status(TAB5_USB,&s);CHECK(!s.mounted&&s.volume_id==0xff);
 fail_registration=false;CHECK(tab5_services_retry(TAB5_USB)==ESP_OK);storage_worker_step(TAB5_USB);
 tab5_services_status(TAB5_USB,&s);CHECK(s.mounted&&s.volume_id==1);
 fail_medium(&media[TAB5_USB],ENODEV);tab5_services_status(TAB5_USB,&s);
 CHECK(!s.mounted&&s.volume_id==0xff&&!s.capacity_known);
 CloseHandle(sd_selected);CloseHandle(release_sd);CloseHandle(usb_attempted);CloseHandle(usb_entered);
 puts("volume registration PASS: concurrent delayed SD/USB workers serialized, pdrv 0/1 distinct, failure releases mutex, status unknown/mount/failure/unmount IDs");
 return 0;
}
