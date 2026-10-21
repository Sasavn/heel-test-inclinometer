#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <io.h>
#define ESP_OK 0
static char test_current_version[32]="1.5.0-esp32";
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_FOUND 0x105
#define fsync test_sync
#define fwrite test_write
static size_t test_write(const void *,size_t,size_t,FILE *);
static int test_sync(int);
typedef unsigned esp_ota_handle_t;
typedef struct {uint32_t size;} esp_partition_t;
typedef int esp_ota_img_states_t;
#define ESP_OTA_IMG_VALID 2
#define ESP_OTA_IMG_UNDEFINED -1
typedef struct {unsigned revision;} esp_chip_info_t;
typedef struct {BCRYPT_ALG_HANDLE alg;BCRYPT_HASH_HANDLE handle;} mbedtls_sha256_context;
static uint64_t test_now;
static unsigned test_lock_depth,test_lock_busy;
static int test_core_start_error,test_usb_start_error,test_worker_start_error;
static int test_netif_start_error,test_wifi_start_error,test_listener_start_error,test_network_task_start_error;
static unsigned test_core_starts,test_usb_starts,test_worker_starts;
static unsigned test_netif_starts,test_wifi_starts,test_listener_starts,test_network_task_starts;
static bool test_present[2]={true,true};
static int test_fault[2],test_flash_fail,test_verify_fail,test_boot_fail;
static unsigned test_boot_selected,test_aborted,test_restarted,test_confirmed;
static unsigned test_erases,test_state_blank=1,test_running_index,test_selected_index,test_slot_valid[2];
static unsigned test_chip_revision=301;
static uint8_t test_flash[8192];static size_t test_flash_offset;
static const esp_partition_t test_running={8192},test_inactive={8192};
static const esp_partition_t *esp_ota_get_next_update_partition(const void *p){(void)p;return &test_inactive;}
static const esp_partition_t *esp_ota_get_running_partition(void){return &test_running;}
static int esp_ota_get_state_partition(const esp_partition_t *p,int *s){(void)p;if(test_state_blank)return ESP_ERR_NOT_FOUND;*s=test_slot_valid[test_running_index]?2:1;return 0;}
static void esp_chip_info(esp_chip_info_t *c){c->revision=test_chip_revision;}
static int esp_ota_begin(const esp_partition_t *p,size_t n,unsigned *h){(void)p;(void)n;*h=1;if(test_flash_fail)return -1;test_erases++;test_flash_offset=0;memset(test_flash,255,sizeof(test_flash));return 0;}
static int esp_ota_write(unsigned h,const void *p,size_t n){(void)h;if(test_flash_fail)return -1;memcpy(test_flash+test_flash_offset,p,n);test_flash_offset+=n;return 0;}
static int esp_ota_abort(unsigned h){(void)h;test_aborted++;return 0;}
static int esp_ota_end(unsigned h){(void)h;return test_verify_fail?-1:0;}
static int esp_ota_set_boot_partition(const esp_partition_t *p){if(test_boot_fail)return -1;test_boot_selected++;test_state_blank=0;test_selected_index=p==&test_running?test_running_index:1-test_running_index;return 0;}
static int esp_ota_mark_app_valid_cancel_rollback(void){if(test_state_blank)return ESP_FAIL;test_slot_valid[test_running_index]=1;test_confirmed++;return 0;}
static void esp_restart(void){test_restarted++;}
static void mbedtls_sha256_init(mbedtls_sha256_context *c){memset(c,0,sizeof(*c));}
static int mbedtls_sha256_starts(mbedtls_sha256_context *c,int is224){(void)is224;if(BCryptOpenAlgorithmProvider(&c->alg,BCRYPT_SHA256_ALGORITHM,NULL,0))abort();if(BCryptCreateHash(c->alg,&c->handle,NULL,0,NULL,0,0))abort();return 0;}
static int mbedtls_sha256_update(mbedtls_sha256_context *c,const void *p,size_t n){return BCryptHashData(c->handle,(PUCHAR)p,n,0);}
static int mbedtls_sha256_finish(mbedtls_sha256_context *c,uint8_t *p){return BCryptFinishHash(c->handle,p,32,0);}
static void mbedtls_sha256_free(mbedtls_sha256_context *c){if(c->handle)BCryptDestroyHash(c->handle);if(c->alg)BCryptCloseAlgorithmProvider(c->alg,0);memset(c,0,sizeof(*c));}
