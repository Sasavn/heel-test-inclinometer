"""Native regression: actual pinned MSC register/getter/unregister, no USB device."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import os
import sys
import tempfile

ROOT=Path(os.environ.get('TAB5_MSC_TEST_ROOT',Path(__file__).resolve().parents[2]))
OUT=ROOT/'.tools/storage-1.6.15/msc-getter-test'
COMPONENT=ROOT/'tab5/components/usb_host_msc'
UPSTREAM=COMPONENT/'upstream'

COMMON=r'''
#ifndef MSC_TEST_H
#define MSC_TEST_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
typedef int esp_err_t;
enum {ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_ARG=0x102,ESP_ERR_INVALID_STATE=0x103,
 ESP_ERR_NO_MEM=0x101,ESP_ERR_MSC_MOUNT_FAILED=0x7100,ESP_ERR_MSC_FORMAT_FAILED=0x7101,
 FR_OK=0,FR_NO_FILESYSTEM=13,FR_INT_ERR=2,FM_ANY=7,FM_SFD=8,FF_VOLUMES=10};
typedef unsigned char BYTE;
typedef int FRESULT;
typedef struct {int number;} FATFS;
typedef struct {BYTE fmt;unsigned n_fat,align,n_root;size_t au_size;} MKFS_PARM;
typedef struct {bool format_if_mount_failed;int max_files;size_t allocation_unit_size;} esp_vfs_fat_mount_config_t;
typedef struct {const char *base_path;const char *fat_drive;int max_files;} esp_vfs_fat_conf_t;
typedef struct {uint32_t block_size,block_count;} usb_disk_t;
typedef struct {usb_disk_t disk;} msc_device_t;
typedef msc_device_t *msc_host_device_handle_t;
static unsigned next_slot,slot_calls,registers,unregisters,unmounts;
static unsigned registered[10],released[10];
static FATFS filesystems[10];
static bool fail_mount;
static void log_stub(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
#define ESP_LOGE(...) log_stub(__VA_ARGS__)
#define MSC_RETURN_ON_INVALID_ARG(x) do{if(!(x))return ESP_ERR_INVALID_ARG;}while(0)
#define MSC_RETURN_ON_FALSE(x,e) do{if(!(x))return (e);}while(0)
#define MSC_GOTO_ON_ERROR(x) do{esp_err_t error=(x);if(error!=ESP_OK){ret=error;goto fail;}}while(0)
#define MSC_GOTO_ON_FALSE(x,e) do{if(!(x)){ret=(e);goto fail;}}while(0)
#define ESP_IDF_VERSION_VAL(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(5,5,2)
static void *ff_memalloc(size_t n){return malloc(n);}
static FRESULT f_mkfs(const char *drive,const MKFS_PARM *p,void *buffer,size_t n){
 (void)drive;(void)p;(void)buffer;(void)n;assert(!"unexpected format");return 1;
}
static esp_err_t ff_diskio_get_drive(BYTE *pdrv){
 slot_calls++;*pdrv=(BYTE)next_slot;return ESP_OK;
}
static void ff_diskio_register_msc(BYTE pdrv,usb_disk_t *disk){
 assert(disk&&pdrv<FF_VOLUMES);registered[registers++]=pdrv;
}
static void ff_diskio_unregister(BYTE pdrv){released[unregisters++]=pdrv;}
static esp_err_t esp_vfs_fat_register_cfg(const esp_vfs_fat_conf_t *cfg,FATFS **fs){
 unsigned n=(unsigned)(cfg->fat_drive[0]-'0');assert(n<FF_VOLUMES);*fs=&filesystems[n];return ESP_OK;
}
static esp_err_t esp_vfs_fat_unregister_path(const char *path){assert(path);return ESP_OK;}
static FRESULT f_mount(FATFS *fs,const char *drive,int immediate){
 (void)immediate;assert(drive&&drive[1]==':');if(!fs){unmounts++;return FR_OK;}
 return fail_mount?FR_NO_FILESYSTEM:FR_OK;
}
#endif
'''

TEST=r'''
#include "common.h"
#include MSC_ACCESSOR_SOURCE
int main(void){
 uint8_t drive=99;assert(msc_host_vfs_get_drive(NULL,&drive)==ESP_ERR_INVALID_ARG&&drive==0xff);
 msc_device_t a={.disk={512,10000}},b={.disk={512,20000}};
 esp_vfs_fat_mount_config_t cfg={.format_if_mount_failed=false,.max_files=6,.allocation_unit_size=16384};
 msc_host_vfs_handle_t av=NULL,bv=NULL;
 next_slot=4;assert(msc_host_vfs_register(&a,"/usbA",&cfg,&av)==ESP_OK);
 next_slot=1;assert(msc_host_vfs_register(&b,"/usbB",&cfg,&bv)==ESP_OK);
 assert(slot_calls==2&&registers==2&&registered[0]==4&&registered[1]==1);
 next_slot=7; /* Free-slot candidate changes: getters must never consult it. */
 assert(msc_host_vfs_get_drive(av,&drive)==ESP_OK&&drive==4);
 assert(msc_host_vfs_get_drive(bv,&drive)==ESP_OK&&drive==1);
 assert(slot_calls==2&&registers==2&&unregisters==0);
 assert(msc_host_vfs_get_drive(av,NULL)==ESP_ERR_INVALID_ARG);
 BYTE saved=av->pdrv;av->pdrv=0xff;
 assert(msc_host_vfs_get_drive(av,&drive)==ESP_ERR_INVALID_STATE&&drive==0xff);av->pdrv=saved;
 av->drive[0]='8';assert(msc_host_vfs_get_drive(av,&drive)==ESP_ERR_INVALID_STATE&&drive==0xff);av->drive[0]='4';
 assert(msc_host_vfs_unregister(av)==ESP_OK);av=NULL;
 assert(unregisters==1&&released[0]==4);
 assert(msc_host_vfs_get_drive(bv,&drive)==ESP_OK&&drive==1);
 assert(msc_host_vfs_unregister(bv)==ESP_OK);bv=NULL;
 assert(unregisters==2&&released[1]==1&&unmounts==2);
 fail_mount=true;next_slot=6;
 assert(msc_host_vfs_register(&a,"/usbFail",&cfg,&av)==ESP_ERR_MSC_MOUNT_FAILED&&av==NULL);
 assert(unregisters==3&&released[2]==6&&unmounts==3);
 puts("PASS actual MSC register/getter/unregister: pdrv4 and pdrv1 ownership, free-slot changed to7 ignored, invalid args/state fail closed, mount-failure cleanup");
 return 0;
}
'''


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    pins=json.loads((COMPONENT/'upstream.json').read_text())
    for name,digest in pins['files'].items():
        assert hashlib.sha256((UPSTREAM/name).read_bytes()).hexdigest()==digest
    (OUT/'common.h').write_text(COMMON,encoding='utf-8')
    for name in ('msc_common.h','esp_vfs_fat.h','esp_err.h','ff.h','ffconf.h','diskio_impl.h','esp_idf_version.h','usb/msc_host.h'):
        p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "common.h"\n',encoding='utf-8')
    source=OUT/'test.c';source.write_text(TEST,encoding='utf-8');exe=OUT/'test.exe'
    command=[shutil.which('gcc'),'-std=gnu11','-O2','-Wall','-Wextra','-I'+str(OUT),
             '-I'+str(COMPONENT/'include'),'-I'+str(UPSTREAM/'include'),
             '-DTAB5_MSC_VFS_SOURCE="'+(UPSTREAM/'src/msc_host_vfs.c').as_posix()+'"',
             '-DMSC_ACCESSOR_SOURCE="'+(COMPONENT/'msc_vfs_owned.c').as_posix()+'"',str(source),'-o',str(exe)]
    subprocess.run(command,check=True)
    result=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,check=True)
    print(result.stdout,end='')
    (OUT/'result.json').write_text(json.dumps(dict(status='PASS',command=command,stdout=result.stdout,
        getter_sha256=hashlib.sha256((COMPONENT/'msc_vfs_owned.c').read_bytes()).hexdigest()),indent=2),encoding='utf-8')
    if '--isolated-child' not in sys.argv:
        with tempfile.TemporaryDirectory(prefix='clean-',dir=OUT) as tmp:
            clean=Path(tmp);dest=clean/'tab5/components/usb_host_msc'
            shutil.copytree(COMPONENT,dest)
            assert not (clean/'tab5/managed_components').exists()
            env=dict(os.environ,TAB5_MSC_TEST_ROOT=str(clean))
            child=subprocess.run([sys.executable,str(Path(__file__).resolve()),'--isolated-child'],env=env,
                stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,check=True)
            proof=dict(status='PASS',no_managed_components=True,stdout=child.stdout,
                scope='Isolated copied component compiles actual vendored VFS+getter and tests ownership without managed MSC or SDK source paths')
            (OUT/'clean-checkout.json').write_text(json.dumps(proof,indent=2),encoding='utf-8')
            print('PASS isolated clean tree: managed_components absent; vendored source/header/license closure only')


if __name__=='__main__':main()
