"""Compile the actual BSP USB source with native driver/task stubs; no hardware.

Generated headers, harness and executable stay under ignored .tools/storage-1.6.15.
This tests ownership/failure paths in production code, not a copied implementation.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / '.tools/storage-1.6.15/bsp-usb-test'
SOURCE = ROOT / 'tab5/components/tab5_board_support/src/bsp_usb.c'

STUBS = r'''
#ifndef USB_TEST_STUBS_H
#define USB_TEST_STUBS_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
typedef int esp_err_t;
typedef void *TaskHandle_t;
typedef int bsp_usb_host_power_mode_t;
enum {ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=0x101,ESP_ERR_INVALID_ARG=0x102,
      ESP_ERR_INVALID_STATE=0x103,ESP_ERR_TIMEOUT=0x107,
      BSP_FEATURE_USB=7,BSP_USB_HOST_POWER_MODE_USB_DEV=0,pdTRUE=1,
      ESP_INTR_FLAG_LOWMED=15,USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS=1,
      USB_HOST_LIB_EVENT_FLAGS_ALL_FREE=2};
#define portMAX_DELAY UINT32_MAX
typedef struct {bool skip_phy_setup;int intr_flags;unsigned peripheral_map;} usb_host_config_t;
static int installs,uninstalls,enables,disables,creates,suspends,deletes;
static esp_err_t install_result,power_result,uninstall_result;
static bool task_success,external_host,power_on;
static void *deleted_handle;
static void (*saved_task_fn)(void *);
static usb_host_config_t config;
static unsigned sequence;
static void log_stub(const char *tag,const char *format,...){(void)tag;(void)format;}
#define ESP_LOGI(...) log_stub(__VA_ARGS__)
#define ESP_LOGE(...) log_stub(__VA_ARGS__)
#define ESP_ERROR_CHECK(expr) assert((expr)==ESP_OK)
#define BSP_ERROR_CHECK_RETURN_ERR(expr) do{esp_err_t result=(expr);if(result!=ESP_OK)return result;}while(0)
static esp_err_t usb_host_install(const usb_host_config_t *c){
    installs++;config=*c;assert(sequence==0);sequence=1;
    if(external_host)return ESP_ERR_INVALID_STATE;
    return install_result;
}
static esp_err_t usb_host_uninstall(void){
    uninstalls++;assert(!external_host);return uninstall_result;
}
static esp_err_t bsp_feature_enable(int feature,bool enable){
    assert(feature==BSP_FEATURE_USB);assert(sequence==1);sequence=2;
    if(enable){enables++;if(power_result==ESP_OK)power_on=true;}
    else{disables++;power_on=false;}
    return power_result;
}
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,
                       unsigned priority,TaskHandle_t *handle){
    (void)name;assert(stack==4096&&priority==10&&arg==NULL);assert(sequence==2);
    sequence=3;creates++;saved_task_fn=fn;
    *handle=(void *)(uintptr_t)0x1234; /* even a failed stub writes a handle */
    return task_success?pdTRUE:0;
}
static void vTaskSuspend(TaskHandle_t h){assert(h==(void *)(uintptr_t)0x1234);suspends++;}
static void vTaskDelete(TaskHandle_t h){deleted_handle=h;deletes++;}
static void usb_host_lib_handle_events(uint32_t ticks,uint32_t *flags){(void)ticks;*flags=0;}
static esp_err_t usb_host_device_free_all(void){return ESP_OK;}
#endif
'''

HARNESS = r'''
#include "usb_test_stubs.h"
#include BSP_USB_SOURCE
static void reset_case(void){
    assert(usb_host_task==NULL);
    installs=uninstalls=enables=disables=creates=suspends=deletes=0;
    install_result=power_result=uninstall_result=ESP_OK;
    task_success=true;external_host=false;power_on=false;sequence=0;
    saved_task_fn=NULL;deleted_handle=NULL;
}
static esp_err_t start(void){return bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV,false);}
static void verify_hs(void){
    assert(config.peripheral_map==1U); /* P4 index0 HS, explicitly NOT BIT1 FS */
    assert(!config.skip_phy_setup&&config.intr_flags==ESP_INTR_FLAG_LOWMED);
}
int main(void){
    reset_case();assert(start()==ESP_OK);verify_hs();
    assert(installs==1&&enables==1&&creates==1&&uninstalls==0&&disables==0);
    assert(saved_task_fn==usb_lib_task&&usb_host_task==(void *)(uintptr_t)0x1234);
    assert(start()==ESP_ERR_INVALID_STATE);assert(installs==1&&enables==1&&creates==1);
    assert(bsp_usb_host_stop()==ESP_OK);assert(uninstalls==1&&suspends==1&&deletes==1);
    assert(deleted_handle==(void *)(uintptr_t)0x1234&&usb_host_task==NULL);

    reset_case();task_success=false;
    assert(start()==ESP_ERR_NO_MEM);verify_hs();
    assert(installs==1&&enables==1&&creates==1&&uninstalls==1);
    assert(usb_host_task==NULL&&disables==0&&deletes==0&&suspends==0&&power_on);
    /* A recoverable task failure leaves no stale handle and allows a retry. */
    sequence=0;task_success=true;assert(start()==ESP_OK);assert(installs==2&&creates==2);
    assert(bsp_usb_host_stop()==ESP_OK);assert(usb_host_task==NULL);

    reset_case();external_host=true;power_on=true;
    assert(start()==ESP_ERR_INVALID_STATE);verify_hs();
    assert(installs==1&&uninstalls==0&&enables==0&&disables==0&&creates==0&&power_on);
    assert(usb_host_task==NULL);

    reset_case();install_result=ESP_ERR_NO_MEM;
    assert(start()==ESP_ERR_NO_MEM);assert(uninstalls==0&&enables==0&&creates==0);

    reset_case();power_result=ESP_FAIL;
    assert(start()==ESP_FAIL);assert(installs==1&&enables==1&&creates==0&&uninstalls==1);
    assert(disables==0&&usb_host_task==NULL);

    reset_case();task_success=false;uninstall_result=ESP_ERR_TIMEOUT;power_on=true;
    assert(start()==ESP_ERR_TIMEOUT);assert(uninstalls==1&&disables==0&&power_on);
    assert(usb_host_task==NULL&&deletes==0&&suspends==0);

    reset_case();power_result=ESP_FAIL;uninstall_result=ESP_ERR_TIMEOUT;
    assert(start()==ESP_ERR_TIMEOUT);assert(uninstalls==1&&creates==0&&disables==0);
    puts("PASS: actual BSP explicit HS/not FS; success/repeat start; task failure/retry; foreign host untouched; install/power/cleanup failures; no abort or VBUS-off on failure");
    return 0;
}
'''


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cc=shutil.which('gcc')
    if not cc:
        raise SystemExit('gcc required')
    (OUT/'usb_test_stubs.h').write_text(STUBS,encoding='utf-8')
    for name in ('esp_log.h','bsp_err_check.h','usb/usb_host.h','bsp/m5stack_tab5.h'):
        path=OUT/name;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text('#include "usb_test_stubs.h"\n',encoding='utf-8')
    source=OUT/'actual_bsp_usb_test.c';source.write_text(HARNESS,encoding='utf-8')
    exe=OUT/'actual_bsp_usb_test.exe'
    command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Wno-unused-parameter','-I'+str(OUT),
             '-DBSP_USB_SOURCE="'+SOURCE.as_posix()+'"',str(source),'-o',str(exe)]
    subprocess.run(command,check=True)
    result=subprocess.run([str(exe)],check=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    print(result.stdout,end='')
    # Validate the pinned target's *real* peripheral table, not just stub names.
    mapping=(ROOT/'.tools/esp-idf/components/soc/esp32p4/usb_dwc_periph.c').read_text(encoding='utf-8')
    hs=mapping.split('[0] = {',1)[1].split('},',1)[0]
    fs=mapping.split('[1] = {',1)[1].split('},',1)[0]
    assert 'USB_PHY_INST_UTMI_0' in hs and 'USB_PHY_INST_FSLS_INTERN_0' in fs
    metadata=dict(status='PASS',command=command,source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                  mapping_sha256=hashlib.sha256(mapping.encode()).hexdigest(),stdout=result.stdout,
                  scope='Actual BSP source + native stubs; no hardware or IDF firmware build')
    (OUT/'result.json').write_text(json.dumps(metadata,indent=2),encoding='utf-8')


if __name__=='__main__':
    main()
