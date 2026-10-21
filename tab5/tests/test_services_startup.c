/* Reuse the real core and injected I/O boundaries from the existing suite. */
#define main service_core_suite_main
#include "test_services.c"
#undef main

static unsigned ui_ready,command_calls,boot_failed;
static void caller_command(void *ctx,const char *command,tab5_service_reply_fn emit,void *reply_ctx) {
 (void)ctx;CHECK(!test_lock_depth);CHECK(!strcmp(command,"status"));command_calls++;emit(reply_ctx,"OK caller status");
}
static esp_err_t caller_start(bool network) {
 tab5_services_config_t cfg={.command=caller_command};
 cfg.network.ap_enabled=network;
 strcpy(cfg.network.ap_ssid,"startup-test");strcpy(cfg.network.ap_password,"test-password");strcpy(cfg.network.auth_token,"test-password");
 esp_err_t e=tab5_services_init(&cfg);
 if(e!=ESP_OK){boot_failed++;return e;}
 /* Mirrors the main integration boundary: initialize UI/commands only after
  * successful core init. No GUI or bootloader process is launched by this test. */
 ui_ready++;
 if(!tab5_services_line(1,"status",true,reply,NULL))config.command(config.command_ctx,"status",reply,NULL);
 return e;
}
static void reset_startup(void) {
 memset(media,0,sizeof(media));memset(&network_status,0,sizeof(network_status));
 initialized=storage_ready=recording=stopping=files_busy=boot_health=usb_driver_ready=false;
 tcp_started=network_task_failed=false;
 usb_driver_error=0;ui_ready=command_calls=boot_failed=0;
 test_core_start_error=test_usb_start_error=test_worker_start_error=0;
 test_netif_start_error=test_wifi_start_error=test_listener_start_error=test_network_task_start_error=0;
 test_core_starts=test_usb_starts=test_worker_starts=0;
 test_netif_starts=test_wifi_starts=test_listener_starts=test_network_task_starts=0;
 test_present[0]=test_present[1]=true;
}
int main(void) {
 _mkdir("test-sd");_mkdir("test-usb");reset_startup();
 /* Regression: the ORIGINAL USB driver error must not reach main's boot_failed
  * path or be replaced with a generic no-device error on periodic retry. */
 test_usb_start_error=0x1703;
 CHECK(caller_start(false)==ESP_OK);CHECK(ui_ready==1 && command_calls==1 && !boot_failed);
 CHECK(initialized && storage_ready && !usb_driver_ready);
 CHECK(test_core_starts==1 && test_usb_starts==1 && test_worker_starts==1);
 CHECK(!test_netif_starts && !test_wifi_starts && !test_listener_starts && !test_network_task_starts);
 tab5_medium_status_t usb;tab5_services_status(TAB5_USB,&usb);CHECK(usb.failed && usb.error==0x1703 && !usb.mounted);
 CHECK(tab5_services_retry(TAB5_USB)==0x1703);tab5_services_status(TAB5_USB,&usb);CHECK(usb.error==0x1703);
 CHECK(tab5_services_record_start("startup_sd_survives")==ESP_OK);
 tab5_service_sample_t sample={.sensor=2,.date="21.10.2026",.time="12:00:00",.raw_x=1.25,.battery=NAN};
 CHECK(tab5_services_submit(&sample)==ESP_OK);media_step(TAB5_SD);tab5_services_record_stop();
 CHECK(media[TAB5_SD].status.written==1 && !media[TAB5_SD].status.failed);
 FILE *f=fopen("test-sd/startup_sd_survives_D2.CSV","rb");CHECK(f!=NULL);char content[512];size_t n=fread(content,1,sizeof(content)-1,f);content[n]=0;fclose(f);
 /* Windows host stdio translates LF on disk; firmware VFS does not. The pure
  * formatter's byte-exact LF contract is checked by the core suite. */
 CHECK(!strncmp(content,TAB5_SERVICE_CSV_HEADER,strlen(TAB5_SERVICE_CSV_HEADER)-1) && strstr(content,"21.10.2026;12:00:00;1,25;")!=NULL);
 CHECK(!test_boot_selected && !boot_failed); /* optional failure did not request rollback */
 reset_startup();test_netif_start_error=0x778;CHECK(caller_start(true)==ESP_OK);CHECK(ui_ready && !boot_failed && storage_ready);CHECK(network_status.last_error==0x778 && !network_status.tcp_listening);CHECK(!test_wifi_starts && !test_listener_starts);
 reset_startup();test_wifi_start_error=0x779;CHECK(caller_start(true)==ESP_OK);CHECK(ui_ready && !boot_failed);CHECK(network_status.last_error==0x779 && test_wifi_starts==1 && !test_listener_starts);
 reset_startup();test_listener_start_error=EADDRINUSE;CHECK(caller_start(true)==ESP_OK);CHECK(ui_ready && !boot_failed);CHECK(network_status.last_error==EADDRINUSE && !network_status.tcp_listening && !test_network_task_starts);
 reset_startup();CHECK(caller_start(true)==ESP_OK);CHECK(ui_ready && network_status.tcp_listening && test_network_task_starts==1);
 reset_startup();test_core_start_error=ESP_ERR_NO_MEM;CHECK(caller_start(false)==ESP_ERR_NO_MEM);CHECK(boot_failed==1 && !ui_ready && !initialized && !test_usb_starts);
 reset_startup();test_worker_start_error=ESP_ERR_NO_MEM;CHECK(caller_start(false)==ESP_ERR_NO_MEM);CHECK(boot_failed==1 && !ui_ready && !storage_ready);
 reset_startup();test_network_task_start_error=ESP_ERR_NO_MEM;CHECK(caller_start(true)==ESP_ERR_NO_MEM);CHECK(boot_failed==1 && !ui_ready && network_status.last_error==ESP_ERR_NO_MEM);
 /* Runtime configure executes the same production path as initial startup. */
 reset_startup();CHECK(caller_start(false)==ESP_OK);
 tab5_network_config_t on=config.network;on.ap_enabled=true;
 CHECK(!test_netif_starts && !test_wifi_starts && !test_listener_starts);
 CHECK(tab5_services_network_configure(&on)==ESP_OK);
 CHECK(network_status.tcp_listening && test_listener_starts==1 && test_network_task_starts==1);
 CHECK(tab5_services_network_configure(&on)==ESP_OK);
 CHECK(test_listener_starts==1 && test_network_task_starts==1 && !network_status.last_error);
 tab5_network_config_t off=on;off.ap_enabled=false;
 CHECK(tab5_services_network_configure(&off)==ESP_OK);CHECK(tab5_services_network_configure(&on)==ESP_OK);
 CHECK(test_listener_starts==1 && test_network_task_starts==1);
 CHECK(tab5_services_network_configure(NULL)==ESP_ERR_INVALID_ARG);CHECK(network_status.last_error==ESP_ERR_INVALID_ARG);
 reset_startup();test_wifi_start_error=0x779;CHECK(caller_start(true)==ESP_OK);
 CHECK(!test_listener_starts && !test_network_task_starts && network_status.last_error==0x779);
 on=config.network;test_wifi_start_error=0;CHECK(tab5_services_network_configure(&on)==ESP_OK);
 CHECK(network_status.tcp_listening && !network_status.last_error && test_listener_starts==1 && test_network_task_starts==1);
 CHECK(tab5_services_network_configure(&on)==ESP_OK);CHECK(test_listener_starts==1 && test_network_task_starts==1);
 reset_startup();test_netif_start_error=0x778;CHECK(caller_start(true)==ESP_OK);on=config.network;
 test_netif_start_error=0;CHECK(tab5_services_network_configure(&on)==ESP_OK);CHECK(test_listener_starts==1 && test_network_task_starts==1);
 reset_startup();test_listener_start_error=EADDRINUSE;CHECK(caller_start(true)==ESP_OK);on=config.network;
 test_listener_start_error=0;CHECK(tab5_services_network_configure(&on)==ESP_OK);CHECK(test_listener_starts==2 && test_network_task_starts==1 && !network_status.last_error);
 test_wifi_start_error=0x780;CHECK(tab5_services_network_configure(&on)==0x780);CHECK(network_status.last_error==0x780);
 test_wifi_start_error=0;CHECK(tab5_services_network_configure(&on)==ESP_OK);CHECK(test_listener_starts==2 && test_network_task_starts==1 && !network_status.last_error);
 printf("startup: %u checks passed (shared production startup, injected peripheral failures)\n",checks);
 return 0;
}
