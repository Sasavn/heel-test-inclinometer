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
  CHECK(!entry_mounts[0]&&!entry_mounts[1]);
  CHECK(tab5_services_line(1,"ota info",true,reply,NULL));CHECK(!strcmp(answer,"OK"));
 }
 reset_entry();entry_ui_error=ESP_FAIL;run_actual_app_startup();CHECK(entry_failed==1&&!test_core_starts&&!entry_tasks);
 reset_entry();test_core_start_error=ESP_ERR_NO_MEM;run_actual_app_startup();printf("ENTRY_CORE_FAILURE ui=%u boot_failed=%u consumers=%u unsafe_consumers=%u\n",entry_ui_ready,entry_failed,entry_tasks,entry_unsafe_tasks);CHECK(entry_ui_ready==1&&entry_failed==1&&!entry_tasks&&!entry_unsafe_tasks);
 reset_entry();test_worker_start_error=ESP_ERR_NO_MEM;run_actual_app_startup();CHECK(entry_ui_ready==1&&entry_failed==1&&!entry_tasks&&!entry_unsafe_tasks);
 printf("ENTRY_CHAIN %u checks PASS actual main.c startup prefix + production services; UI before init, locks before consumers, optional failure OTA survives\n",checks);return 0;
}
