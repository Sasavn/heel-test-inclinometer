/* Actual ESP SD command-budget wrapper; only the transaction I/O is stubbed. */
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <assert.h>
#include <stdio.h>
typedef int esp_err_t;
typedef struct {uint32_t timeout_ms;} sdmmc_command_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_STATE 0x103
static int64_t sd_deadline_us,now_us;
static unsigned calls;
static int driver_error;
static uint32_t observed_timeout;
static int64_t esp_timer_get_time(void){return now_us;}
static esp_err_t fake_transaction(int slot,sdmmc_command_t *cmd){assert(slot==0);calls++;observed_timeout=cmd->timeout_ms;return driver_error;}
static esp_err_t (*sd_transaction)(int,sdmmc_command_t*)=fake_transaction;
#include "services_deadline_generated.h"
int main(void){
 now_us=100000;sd_deadline_us=now_us+SD_MOUNT_MS*1000LL;sdmmc_command_t cmd={0};assert(sd_bounded_transaction(0,&cmd)==ESP_OK&&observed_timeout==SD_COMMAND_MS);
 cmd.timeout_ms=17;assert(sd_bounded_transaction(0,&cmd)==ESP_OK&&observed_timeout==17);
 sd_deadline_us=now_us+123456;cmd.timeout_ms=0;assert(sd_bounded_transaction(0,&cmd)==ESP_OK&&observed_timeout==124);
 sd_deadline_us=now_us+1;cmd.timeout_ms=0;assert(sd_bounded_transaction(0,&cmd)==ESP_OK&&observed_timeout==1);
 unsigned previous=calls;sd_deadline_us=now_us;assert(sd_bounded_transaction(0,&cmd)==ESP_ERR_TIMEOUT&&calls==previous);
 sd_deadline_us=now_us-100000;assert(sd_bounded_transaction(0,&cmd)==ESP_ERR_TIMEOUT&&calls==previous);
 sd_deadline_us=now_us+100000;sd_transaction=NULL;assert(sd_bounded_transaction(0,&cmd)==ESP_ERR_INVALID_STATE&&calls==previous);
 sd_transaction=fake_transaction;driver_error=ESP_FAIL;assert(sd_bounded_transaction(0,&cmd)==ESP_FAIL);
 puts("SD_DEADLINE actual-source wrapper PASS command cap/residual budget/expiry/no I/O/null driver/error");return 0;
}
