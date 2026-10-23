/* Synthetic RAM media only. No OS device or path is accepted by this program. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
#include "tab5_services.h"
#include "production_fs_policy.h"
_Static_assert(FF_FS_EXFAT == 1, "project must enable exFAT");
_Static_assert(FF_USE_LFN > 0 && FF_LFN_UNICODE == 2, "project UTF8 LFN required");
_Static_assert(FF_MAX_LFN == 255, "project LFN maximum required");
_Static_assert(sizeof(FSIZE_t) == 8, "64 bit library file size required");
static BYTE *media;
static LBA_t sectors;
static unsigned writes, syncs;
PARTITION VolToPart[FF_VOLUMES] = {{0,0}};
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define OK(x) CHECK((x) == FR_OK)
DSTATUS disk_initialize(BYTE p) { return p ? STA_NOINIT : 0; }
DSTATUS disk_status(BYTE p) { return p ? STA_NOINIT : 0; }
DRESULT disk_read(BYTE p, BYTE *b, LBA_t s, UINT n) {
 if(p || s >= sectors || n > sectors-s) return RES_PARERR;
 memcpy(b,media+(size_t)s*512,(size_t)n*512); return RES_OK;
}
DRESULT disk_write(BYTE p,const BYTE *b,LBA_t s,UINT n) {
 if(p || s >= sectors || n > sectors-s) return RES_PARERR;
 memcpy(media+(size_t)s*512,b,(size_t)n*512); writes++; return RES_OK;
}
DRESULT disk_ioctl(BYTE p,BYTE cmd,void *b) {
 if(p) return RES_PARERR;
 switch(cmd) {
 case CTRL_SYNC: syncs++; return RES_OK;
 case GET_SECTOR_COUNT: *(LBA_t*)b=sectors; return RES_OK;
 case GET_SECTOR_SIZE: *(WORD*)b=512; return RES_OK;
 case GET_BLOCK_SIZE: *(DWORD*)b=1; return RES_OK;
 case CTRL_TRIM: return RES_OK;
 default: return RES_PARERR;
 }
}
void *ff_memalloc(unsigned n) { return malloc(n); }
void ff_memfree(void *p) { free(p); }
DWORD get_fattime(void) { return ((DWORD)(2026-1980)<<25)|(10<<21)|(23<<16); }
/* Single-thread harness; production synchronization is outside this test. */
int ff_mutex_create(int v) { (void)v; return 1; }
void ff_mutex_delete(int v) { (void)v; }
int ff_mutex_take(int v) { (void)v; return 1; }
void ff_mutex_give(int v) { (void)v; }
static void remount(FATFS *fs) { OK(f_mount(NULL,"0:",0)); memset(fs,0,sizeof(*fs)); OK(f_mount(fs,"0:",1)); }
static void read_exact(const char *path,const char *data) {
 FIL f; UINT n; char b[512]; OK(f_open(&f,path,FA_READ));
 CHECK(f_size(&f)==strlen(data)); OK(f_read(&f,b,sizeof b,&n));
 CHECK(n==strlen(data) && !memcmp(b,data,n)); OK(f_close(&f));
}
static void run(BYTE format,LBA_t size,BYTE expected,const char *label) {
 FATFS fs; FIL f; FF_DIR dir; FILINFO info; UINT n; BYTE work[32768];
 const char *name="0:/measurement_\xd0\xb8\xd0\xb7\xd0\xbc\xd0\xb5\xd1\x80\xd0\xb5\xd0\xbd\xd0\xb8\xd1\x8f_long_filename_2026.csv";
 const char *csv="time,angle\r\n0,1.25\r\n";
 MKFS_PARM opt={0}; opt.fmt=format|FM_SFD;
 sectors=size; media=calloc((size_t)size,512); CHECK(media); writes=syncs=0;
 OK(f_mkfs("0:",&opt,work,sizeof work)); remount(&fs); CHECK(fs.fs_type==expected);
 tab5_fs_type_t kind=TAB5_FS_UNKNOWN;
 unsigned policy_writes=writes;
 int accepted=supported_drive(0,&kind);
 CHECK(writes==policy_writes);
 CHECK(accepted==(expected==FS_FAT32 || expected==FS_EXFAT));
 CHECK(kind==(expected==FS_FAT32?TAB5_FS_FAT32:expected==FS_EXFAT?TAB5_FS_EXFAT:TAB5_FS_UNKNOWN));
 OK(f_open(&f,name,FA_CREATE_NEW|FA_WRITE)); OK(f_write(&f,csv,(UINT)strlen(csv),&n)); CHECK(n==strlen(csv));
 unsigned before=syncs; OK(f_sync(&f)); CHECK(syncs>before); OK(f_close(&f)); remount(&fs);
 read_exact(name,csv);
 OK(f_opendir(&dir,"0:/")); int found=0;
 do { OK(f_readdir(&dir,&info)); if(!strcmp(info.fname,name+3)) found++; } while(info.fname[0]);
 OK(f_closedir(&dir)); CHECK(found==1);
 if(expected==FS_FAT12 || expected==FS_FAT16) {
  BYTE *snapshot=malloc((size_t)size*512); CHECK(snapshot); memcpy(snapshot,media,(size_t)size*512);
  before=writes; CHECK(!supported_drive(0,&kind));
  OK(f_opendir(&dir,"0:/")); OK(f_closedir(&dir)); remount(&fs); read_exact(name,csv);
  CHECK(writes==before && !memcmp(snapshot,media,(size_t)size*512)); free(snapshot);
 } else {
  CHECK(f_open(&f,name,FA_CREATE_NEW|FA_WRITE)==FR_EXIST); /* retry must not truncate */
  OK(f_open(&f,name,FA_OPEN_APPEND|FA_WRITE)); CHECK(f_tell(&f)==strlen(csv));
  OK(f_write(&f,"1,2.50\r\n",8,&n)); CHECK(n==8); OK(f_sync(&f)); OK(f_close(&f));
  remount(&fs); read_exact(name,"time,angle\r\n0,1.25\r\n1,2.50\r\n");
  OK(f_open(&f,"0:/probe.tmp",FA_CREATE_NEW|FA_WRITE)); OK(f_sync(&f)); OK(f_close(&f));
  OK(f_unlink("0:/probe.tmp")); remount(&fs); CHECK(f_stat("0:/probe.tmp",&info)==FR_NO_FILE);
 }
 OK(f_mount(NULL,"0:",0));
 printf("{\"case\":\"%s\",\"fs_type\":%u,\"sectors\":%lu,\"writes\":%u,\"syncs\":%u,\"passed\":true}\n",label,expected,(unsigned long)size,writes,syncs);
 free(media); media=NULL;
}
int main(void) {
 printf("{\"abi\":{\"FF_FS_EXFAT\":%d,\"FF_USE_LFN\":%d,\"FF_LFN_UNICODE\":%d,\"FF_MAX_LFN\":%d,\"FSIZE_t_bytes\":%u}}\n",FF_FS_EXFAT,FF_USE_LFN,FF_LFN_UNICODE,FF_MAX_LFN,(unsigned)sizeof(FSIZE_t));
 run(FM_FAT32,262144,FS_FAT32,"fat32");
 run(FM_EXFAT,131072,FS_EXFAT,"exfat");
 run(FM_FAT,2048,FS_FAT12,"fat12_refused_unchanged");
 run(FM_FAT,32768,FS_FAT16,"fat16_refused_unchanged"); return 0;
}
