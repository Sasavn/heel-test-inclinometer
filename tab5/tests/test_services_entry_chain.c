/* Execute the actual app_main startup prefix generated from main.c. The
 * service implementation is real; only ESP allocation/driver/task boundaries
 * and UI/measurement hardware startup are replaced. No GUI/device access. */
#include "test_services_startup_hooks.h"
#define main retained_services_core_suite
#include "test_services.c"
#undef main
#include "../main/tab5_ui.h"

typedef struct {unsigned index;tab5_measure_sensor_t value;} sample_event_t;
typedef struct {char line[3072];} tx_line_t;
typedef struct {int tx_buffer_size,rx_buffer_size;} usb_serial_jtag_driver_config_t;
static void *app_mutex,*route_mutex,*samples,*storage_samples,*actions,*usb_tx;
static tab5_network_config_t network;
static struct {uint64_t since;} ui_perf;
static unsigned entry_ui_ready,entry_failed,entry_tasks,entry_usb_tasks,entry_router_tasks,entry_persist;
static unsigned entry_unsafe_tasks;
static unsigned entry_mounts[2];
static int entry_ui_error;
static unsigned entry_allocs,entry_frees;
static bool entry_alloc_fail;
static void *entry_clients_alloc(size_t count,size_t size,unsigned caps){
 entry_allocs++;CHECK(count==CLIENTS&&size==sizeof(client_t));
 CHECK(caps==((1u<<10)|(1u<<2))); /* pinned SPIRAM | 8BIT, no internal fallback */
 return entry_alloc_fail?NULL:calloc(count,size);
}
static void entry_clients_free(void *p){CHECK(p==clients);entry_frees++;free(p);}
#define pdPASS 1
static void *xSemaphoreCreateMutex(void){return (void*)(uintptr_t)1;}
static void *xQueueCreate(unsigned n,size_t size){(void)n;(void)size;return (void*)(uintptr_t)1;}
static int nvs_flash_init(void){return ESP_OK;}
static void load_config(tab5_measure_config_t *out){memset(out,0,sizeof(*out));}
static int bsp_i2c_init(void){return ESP_OK;}
static void tab5_board_init(void){}
static int usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t *c){(void)c;return ESP_OK;}
static void sample_callback(unsigned sensor,const tab5_measure_sensor_t *sample,void *context){(void)sensor;(void)sample;(void)context;}
static int tab5_measure_start(const tab5_measure_config_t *c,void (*cb)(unsigned,const tab5_measure_sensor_t*,void*),void *context){(void)c;(void)cb;(void)context;return ESP_OK;}
static uint64_t esp_timer_get_time(void){return test_now*1000;}
static void ui_callback(const tab5_ui_action_t *action,void *context){(void)action;(void)context;}
static int tab5_native_port_start(void (*cb)(const tab5_ui_action_t*,void*),void *context){
 (void)cb;(void)context;CHECK(!initialized&&!test_core_starts);if(!entry_ui_error)entry_ui_ready++;return entry_ui_error;
}
static void command(void *context,const char *s,tab5_service_reply_fn emit,void *reply_context){(void)context;(void)s;emit(reply_context,"OK entry command");}
static void storage_router_task(void *unused){(void)unused;}
static void usb_task(void *unused){(void)unused;}
static int xTaskCreate(void (*fn)(void*),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){
 (void)fn;(void)stack;(void)arg;(void)priority;(void)handle;entry_tasks++;
 if(!initialized||!storage_ready)entry_unsafe_tasks++;
 CHECK(entry_ui_ready==1);
 if(!strcmp(name,"sample_storage"))entry_router_tasks++;
 if(!strcmp(name,"protocol_usb"))entry_usb_tasks++;
 return pdPASS;
}
static bool persist(void){entry_persist++;return true;}
static void boot_failed(void){entry_failed++;}
#include "startup_entry_generated.h"
static esp_err_t entry_mount(unsigned i){CHECK(i<2);entry_mounts[i]++;return ESP_FAIL;}

static void reset_entry(void){
 clients_storage_release();
 test_clients_alloc_hook=entry_clients_alloc;test_clients_free_hook=entry_clients_free;
 entry_allocs=entry_frees=0;entry_alloc_fail=false;
 memset(media,0,sizeof(media));memset(&network_status,0,sizeof(network_status));memset(&network,0,sizeof(network));
 initialized=storage_ready=recording=stopping=files_busy=boot_health=usb_driver_ready=false;
 tcp_started=network_task_failed=false;usb_driver_error=0;test_now=0;
 entry_ui_ready=entry_failed=entry_tasks=entry_usb_tasks=entry_router_tasks=entry_persist=entry_unsafe_tasks=0;entry_ui_error=0;
 test_core_starts=test_usb_starts=test_worker_starts=test_netif_starts=test_wifi_starts=test_listener_starts=test_network_task_starts=0;
 test_core_start_error=test_usb_start_error=test_worker_start_error=test_netif_start_error=test_wifi_start_error=test_listener_start_error=test_network_task_start_error=0;
 test_present[0]=test_present[1]=true;test_lock_depth=0;
 entry_mounts[0]=entry_mounts[1]=0;test_mount_hook=entry_mount;
}
int main(void){
 _mkdir("test-sd");_mkdir("test-usb");
 for(unsigned optional=0;optional<4;optional++){
  reset_entry();test_present[TAB5_SD]=optional!=1;test_usb_start_error=optional==2?0x1703:0;
  if(optional==3){network.ap_enabled=true;strcpy(network.ap_ssid,"entry");strcpy(network.ap_password,"entry-password");test_wifi_start_error=0x779;}
  run_actual_app_startup();CHECK(entry_ui_ready==1&&!entry_failed&&!entry_unsafe_tasks);CHECK(entry_usb_tasks==1&&entry_router_tasks==1&&entry_persist==1);
  CHECK(initialized&&storage_ready);CHECK(test_usb_starts==0); /* deferred USB driver, not a permissive stub */
  CHECK(entry_allocs==1&&!entry_frees&&clients);
  for(unsigned c=0;c<CLIENTS;c++){
   CHECK(clients[c].fd==-1&&!clients[c].used&&!clients[c].tx_len&&!clients[c].auth);
   for(size_t j=0;j<sizeof(clients[c].rx);j++)CHECK(clients[c].rx[j]==0);
   for(size_t j=0;j<sizeof(clients[c].tx);j++)CHECK(clients[c].tx[j]==0);
  }
  CHECK(clients_storage_init()&&entry_allocs==1); /* idempotent, no replacement */
  CHECK(!entry_mounts[0]&&!entry_mounts[1]);
  CHECK(tab5_services_line(1,"ota info",true,reply,NULL));CHECK(!strcmp(answer,"OK"));
 }
 reset_entry();entry_ui_error=ESP_FAIL;run_actual_app_startup();CHECK(entry_failed==1&&!test_core_starts&&!entry_tasks);
 CHECK(!entry_allocs);
 reset_entry();entry_alloc_fail=true;run_actual_app_startup();
 CHECK(entry_ui_ready==1&&entry_failed==1&&!entry_tasks&&!entry_unsafe_tasks);
 CHECK(entry_allocs==1&&!entry_frees&&!clients&&!initialized&&!storage_ready&&!test_worker_starts);
 CHECK(!test_usb_starts&&!entry_mounts[0]&&!entry_mounts[1]);
 printf("ENTRY_PSRAM_FAILURE allocs=%u consumers=%u workers=%u PASS\n",entry_allocs,entry_tasks,test_worker_starts);
 entry_alloc_fail=false;entry_ui_ready=0;test_core_starts=0;run_actual_app_startup();
 CHECK(initialized&&storage_ready&&clients&&entry_allocs==2&&entry_tasks==2);
 reset_entry();test_core_start_error=ESP_ERR_NO_MEM;run_actual_app_startup();printf("ENTRY_CORE_FAILURE ui=%u boot_failed=%u consumers=%u unsafe_consumers=%u\n",entry_ui_ready,entry_failed,entry_tasks,entry_unsafe_tasks);CHECK(entry_ui_ready==1&&entry_failed==1&&!entry_tasks&&!entry_unsafe_tasks);
 CHECK(entry_allocs==1&&entry_frees==1&&!clients);
 reset_entry();test_worker_start_error=ESP_ERR_NO_MEM;run_actual_app_startup();CHECK(entry_ui_ready==1&&entry_failed==1&&!entry_tasks&&!entry_unsafe_tasks);
 CHECK(entry_allocs==1&&!entry_frees&&clients); /* potential worker ownership: no unsafe free */
 printf("ENTRY_CHAIN %u checks PASS actual main.c startup prefix + production services; UI before init, locks before consumers, optional failure OTA survives\n",checks);return 0;
}
