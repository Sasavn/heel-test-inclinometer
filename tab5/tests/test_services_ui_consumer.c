/* Execute the real main.c snapshot-to-UI block, not a copied expectation. */
#include "tab5_services.h"
#include "tab5_ui.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
static tab5_medium_status_t input_sd,input_usb;
static tab5_network_snapshot_t input_net;
static tab5_network_config_t network={.tcp_port=5000};
static char notice[160]="consumer test";
static void *app_mutex;
void tab5_services_status(tab5_medium_t m,tab5_medium_status_t *out){*out=m==TAB5_SD?input_sd:input_usb;}
void tab5_services_network_snapshot(tab5_network_snapshot_t *out){*out=input_net;}
static const char *esp_err_to_name(int e){return e?"test-error":"ESP_OK";}
static void xSemaphoreGive(void *mutex){(void)mutex;}
#include "services_consumer_generated.h"
int main(void){
 unsigned count=0;
 for(unsigned known=0;known<8;known++)for(unsigned mounted=0;mounted<4;mounted++)for(unsigned fs=0;fs<4;fs++){
  input_sd=(tab5_medium_status_t){.known=(known&1)!=0,.mounted=(mounted&1)!=0,.recording=true,.written=7,.fs_type=fs&1?TAB5_FS_EXFAT:TAB5_FS_FAT32};
  input_usb=(tab5_medium_status_t){.known=(known&2)!=0,.mounted=(mounted&2)!=0,.recording=true,.written=9,.fs_type=fs&2?TAB5_FS_EXFAT:TAB5_FS_FAT32};
  input_net=(tab5_network_snapshot_t){.known=(known&4)!=0,.ap_started=true,.sta_connected=true,.clients=2};
  strcpy(input_net.ap_ip,"192.168.4.1");strcpy(input_net.sta_ip,"192.168.1.2");
  tab5_ui_state_t ui={0};strcpy(ui.capacity[0].filesystem,"unavailable");strcpy(ui.capacity[1].filesystem,"unavailable");
  apply_actual_main_snapshots(&ui);
  assert(ui.sd.known==input_sd.known&&ui.usb.known==input_usb.known);
  assert(ui.capacity[0].details_known==(input_sd.known&&input_sd.mounted));assert(ui.capacity[1].details_known==(input_usb.known&&input_usb.mounted));
  assert(ui.link.known==input_net.known&&ui.tcp_clients_known==input_net.known);
  for(unsigned i=0;i<2;i++)if(ui.capacity[i].details_known)assert(!strcmp(ui.capacity[i].filesystem,(fs&(1u<<i))?"exFAT":"FAT32"));else assert(!strcmp(ui.capacity[i].filesystem,"unavailable"));
  count++;
 }printf("MAIN_UI_CONSUMER %u actual-source cases PASS known/mounted gates + real FAT32/exFAT labels + network/client known\n",count);return 0;
}
