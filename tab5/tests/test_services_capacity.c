/* Production worker/cache/geometry; only f_getfree and device I/O are fake. */
#define main service_core_suite_main
#include "test_services.c"
#undef main

static FATFS capacity_fs[2];
static DWORD capacity_free[2];
static unsigned capacity_calls[2];
static int capacity_error;
static bool capacity_null;
static int capacity_mount(unsigned i){mounted_drive[i]=i?7:3;return ESP_OK;}
static int getfree_probe(const char *path,DWORD *available,FATFS **fs){
 CHECK(!test_lock_depth); /* Scanning never holds the service-global mutex. */
 unsigned i=path[0]=='3'?0:1;
 CHECK(!strcmp(path,i?"7:/":"3:/"));capacity_calls[i]++;
 *available=capacity_free[i];*fs=capacity_null?NULL:&capacity_fs[i];
 /* A UI/status poll remains a cache read while FatFs is active. */
 unsigned before=capacity_calls[i];tab5_medium_status_t snapshot;
 tab5_services_status((tab5_medium_t)i,&snapshot);CHECK(capacity_calls[i]==before);
 return capacity_error;
}
static void valid_geometry(unsigned i){
 capacity_fs[i]=(FATFS){.fs_type=i?FS_EXFAT:FS_FAT32,.pdrv=i?7:3,
                       .csize=128,.n_fatent=1048578};
#if FF_MAX_SS != FF_MIN_SS
 capacity_fs[i].ssize=i?4096:512;
#endif
 capacity_free[i]=262144;
}
static void check_status(unsigned i,bool known){
 tab5_medium_status_t s;tab5_services_status((tab5_medium_t)i,&s);
 CHECK(s.known&&s.mounted&&!s.failed&&s.error==ESP_OK);
 CHECK(s.capacity_known==known);
 if(known){
#if FF_MAX_SS != FF_MIN_SS
  uint64_t sector=capacity_fs[i].ssize;
#else
  uint64_t sector=FF_MAX_SS;
#endif
  uint64_t cluster=sector*capacity_fs[i].csize;
  CHECK(s.total_bytes==((uint64_t)capacity_fs[i].n_fatent-2)*cluster);
  CHECK(s.free_bytes==(uint64_t)capacity_free[i]*cluster);
 }else CHECK(!s.total_bytes&&!s.free_bytes);
}
int main(void){
 _mkdir("test-sd");_mkdir("test-usb");
 test_getfree_hook=getfree_probe;test_mount_hook=capacity_mount;
 valid_geometry(0);valid_geometry(1);
 tab5_services_config_t cfg={0};CHECK(tab5_services_init(&cfg)==ESP_OK);
 CHECK(!capacity_calls[0]&&!capacity_calls[1]); /* no init-time scan */
 test_now=2000;storage_worker_step(0);storage_worker_step(1);
 CHECK(capacity_calls[0]==1&&capacity_calls[1]==1);
 check_status(0,true);check_status(1,true);
 CHECK(media[0].status.total_bytes>UINT32_MAX); /* no 32-bit intermediate */
 for(unsigned n=0;n<1000;n++){
  tab5_medium_status_t s;tab5_services_status(TAB5_SD,&s);
  storage_worker_step(0);
 }
 CHECK(capacity_calls[0]==1);
 test_lock_busy=1;tab5_medium_status_t cached;
 tab5_services_status(TAB5_SD,&cached);test_lock_busy=0;
 CHECK(cached.capacity_known&&cached.total_bytes==media[0].status.total_bytes);
 test_now=21999;storage_worker_step(0);CHECK(capacity_calls[0]==1);
 test_now=22000;recording=true;storage_worker_step(0);CHECK(capacity_calls[0]==1);
 recording=false;stopping=true;capacity_worker_step(0);CHECK(capacity_calls[0]==1);
 stopping=false;files_busy=true;storage_worker_step(0);CHECK(capacity_calls[0]==1);
 files_busy=false;storage_worker_step(0);CHECK(capacity_calls[0]==2);
 capacity_free[0]=0;test_now+=20000;storage_worker_step(0);check_status(0,true);
 CHECK(media[0].status.free_bytes==0); /* full disk is known, not unknown */
 capacity_free[0]=capacity_fs[0].n_fatent-2;test_now+=20000;storage_worker_step(0);
 check_status(0,true);CHECK(media[0].status.free_bytes==media[0].status.total_bytes);
 capacity_error=FR_DISK_ERR;test_now+=20000;storage_worker_step(0);check_status(0,false);
 unsigned calls=capacity_calls[0];storage_worker_step(0);CHECK(capacity_calls[0]==calls);
 capacity_error=FR_OK;capacity_null=true;test_now+=20000;storage_worker_step(0);check_status(0,false);
 capacity_null=false;
 uint64_t total=0,available=0;
 CHECK(!capacity_read_drive(255,&total,&available));CHECK(capacity_calls[0]==calls+1);
 valid_geometry(0);capacity_fs[0].pdrv=7;CHECK(!capacity_read_drive(3,&total,&available));
 valid_geometry(0);capacity_fs[0].fs_type=2;CHECK(!capacity_read_drive(3,&total,&available));
 valid_geometry(0);capacity_fs[0].csize=0;CHECK(!capacity_read_drive(3,&total,&available));
 capacity_fs[0].csize=3;CHECK(!capacity_read_drive(3,&total,&available));
 valid_geometry(0);capacity_fs[0].n_fatent=2;CHECK(!capacity_read_drive(3,&total,&available));
 valid_geometry(0);capacity_free[0]=capacity_fs[0].n_fatent-1;CHECK(!capacity_read_drive(3,&total,&available));
#if FF_MAX_SS != FF_MIN_SS
 const unsigned invalid[]={0,511,513,8192};
 for(unsigned k=0;k<4;k++){valid_geometry(0);capacity_fs[0].ssize=invalid[k];CHECK(!capacity_read_drive(3,&total,&available));}
#endif
 valid_geometry(0);capacity_fs[0].n_fatent=UINT32_MAX;capacity_fs[0].csize=32768;
 capacity_free[0]=UINT32_MAX-2;CHECK(capacity_read_drive(3,&total,&available));
 CHECK(total>UINT32_MAX&&total==available);
 /* Retry clears old capacity before publishing new mounted geometry. */
 valid_geometry(0);CHECK(tab5_services_retry(TAB5_SD)==ESP_OK);
 storage_worker_step(0);check_status(0,true);
 fail_medium(&media[0],ENODEV);
 CHECK(!media[0].status.mounted&&!media[0].status.capacity_known);
 CHECK(!media[0].status.total_bytes&&!media[0].status.free_bytes);
 calls=capacity_calls[0];CHECK(tab5_services_set_auto_retry(TAB5_SD,false)==ESP_OK);
 test_now+=60000;storage_worker_step(0);CHECK(capacity_calls[0]==calls);
 puts("capacity PASS: real worker, mount/20s cache, no global lock/init/status I/O, recording deferred, FAT32/exFAT, 64-bit/bounds, failure/unmount clears");
 return 0;
}
