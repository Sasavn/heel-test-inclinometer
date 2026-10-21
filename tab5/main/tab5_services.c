#include "tab5_services.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <math.h>
#ifdef TAB5_SERVICES_HOST
#include "../tests/test_services_backend.h"
#else
#include "bsp/esp-bsp.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_vfs_fat.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mbedtls/sha256.h"
#endif

#ifdef TAB5_SERVICES_HOST
#define VERSION test_current_version
#else
#define VERSION (esp_app_get_description()->version)
#endif
#define DEPTH 64
#define CLIENTS 4
#define OUTPUT 3072
typedef struct { char text[OUTPUT]; } output_t;
typedef struct {
 tab5_medium_status_t status;
 tab5_service_sample_t queue[DEPTH];
 unsigned head, count;
 FILE *file[2];
 char root[32];
 uint64_t flushed;
} medium_t;
static medium_t media[2];
static bool recording, initialized, storage_ready, files_busy, stopping, boot_health;
static bool usb_driver_ready;
static esp_err_t usb_driver_error;
static bool tcp_started, network_task_failed;
static struct { uint32_t owner,total,crc,size; unsigned medium,stage; FILE *file; DIR *dir; uint64_t progress; } transfer;
static uint32_t transfer_error_owner;
static char transfer_error[40];
static char session_name[64];
static tab5_services_config_t config;
static tab5_network_snapshot_t network_status;
#ifndef TAB5_SERVICES_HOST
static uint32_t network_epoch;
#endif
static struct {
 bool active, header_valid, committed, handle_started;
 uint32_t owner, size, offset, last_offset;
 size_t last_len;
 uint8_t last[256], header[544], expected[32];
 char version[32];
 uint64_t touched, reboot_at;
 esp_ota_handle_t handle;
 const esp_partition_t *partition;
 mbedtls_sha256_context hash;
} ota;
#ifndef TAB5_SERVICES_HOST
static SemaphoreHandle_t state_lock, medium_lock[2], transfer_lock, network_lock;
static QueueHandle_t publish_queue, usb_events;
static msc_host_device_handle_t usb_device;
static msc_host_vfs_handle_t usb_vfs;
static uint8_t usb_address;
static esp_netif_t *ap_netif, *sta_netif;
static bool wifi_initialized, wifi_events_registered, ip_events_registered;
static void lock(void) { xSemaphoreTake(state_lock,portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(state_lock); }
static bool try_lock(void) { return xSemaphoreTake(state_lock,0)==pdTRUE; }
static uint64_t now_ms(void) { return esp_timer_get_time()/1000; }
static void nl(void) { xSemaphoreTake(network_lock,portMAX_DELAY); }
static void nu(void) { xSemaphoreGive(network_lock); }
static void tl(void) { xSemaphoreTake(transfer_lock,portMAX_DELAY); }
static void tu(void) { xSemaphoreGive(transfer_lock); }
static void ml(unsigned i) { xSemaphoreTake(medium_lock[i],portMAX_DELAY); }
static void mu(unsigned i) { xSemaphoreGive(medium_lock[i]); }
#else
static void lock(void) { if(test_lock_depth)abort();test_lock_depth++; }
static void unlock(void) { if(test_lock_depth!=1)abort();test_lock_depth--; }
static bool try_lock(void) { if(test_lock_busy)return false;lock();return true; }
static void nl(void) {}
static void nu(void) {}
static void tl(void) {}
static void tu(void) {}
static void ml(unsigned i) { (void)i; }
static void mu(unsigned i) { (void)i; }
static uint64_t now_ms(void) { return test_now; }
#endif

static bool basename_ok(const char *s) {
 if(!s || !*s || strlen(s)>63 || strstr(s,"..")) return false;
 for(;*s;s++) if(!isalnum((unsigned char)*s) && *s!='_' && *s!='-' && *s!='.') return false;
 return true;
}
static int unhex(const char *s,uint8_t *out,size_t cap) {
 size_t n=strlen(s); if(!n || n%2 || n/2>cap)return -1;
 for(size_t i=0;i<n;i+=2) {
  int a=isdigit((unsigned char)s[i])?s[i]-'0':tolower((unsigned char)s[i])-'a'+10;
  int b=isdigit((unsigned char)s[i+1])?s[i+1]-'0':tolower((unsigned char)s[i+1])-'a'+10;
  if(a<0||a>15||b<0||b>15)return -1;
  out[i/2]=(a<<4)|b;
 } return n/2;
}
static bool parse_version(const char *s,unsigned long v[3]) {
 const char *p=s;char *end;
 for(unsigned i=0;i<3;i++) {
  if(!isdigit((unsigned char)*p))return false;
  errno=0;v[i]=strtoul(p,&end,10);if(errno || v[i]>65535)return false;
  if(i<2){if(*end!='.')return false;p=end+1;}else p=end;
 }
 return !strcmp(p,"-esp32");
}
static bool version_ok(const char *s) {
 unsigned long target[3],running[3];
 if(!parse_version(s,target) || !parse_version(VERSION,running))return false;
 for(unsigned i=0;i<3;i++)if(target[i]!=running[i])return target[i]>running[i];
 return true;
}
static void scaled(char *out,size_t cap,int64_t value,unsigned digits) {
 uint64_t magnitude=value<0?(uint64_t)(-value):(uint64_t)value;
 unsigned scale=digits==1?10:digits==2?100:1000;
 snprintf(out,cap,"%s%llu,%0*llu",value<0?"-":"",(unsigned long long)(magnitude/scale),(int)digits,(unsigned long long)(magnitude%scale));
}
int32_t tab5_services_scaled(double value,unsigned digits) {
 if(!isfinite(value) || digits>3)return 0;
 double a=fabs(value);if(!(a<1e6))a=999999;
 double product=a*(digits==0?1:digits==1?10:digits==2?100:1000);
 int64_t q=(int64_t)product;double fraction=product-q;
 if(fraction>0.5 || (fraction==0.5 && (q&1)))q++;
 return value<0?-q:q;
}
#define to_scaled tab5_services_scaled
int tab5_services_format_csv(char *out,size_t cap,const tab5_service_sample_t *s) {
 if(!out || !s || (s->sensor!=2 && s->sensor!=3) ||
    !memchr(s->date,0,sizeof(s->date)) || !memchr(s->time,0,sizeof(s->time)))return -1;
 double values[]={s->raw_x,s->raw_y,s->offset_x,s->offset_y,s->battery};
 for(unsigned i=0;i<4;i++)if(!isfinite(values[i]) || fabs(values[i])>1000000)return -1;
 int64_t rx=to_scaled(s->raw_x,2),ry=to_scaled(s->raw_y,2),ox=to_scaled(s->offset_x,3),oy=to_scaled(s->offset_y,3);
 char v[7][32];scaled(v[0],32,rx,2);scaled(v[1],32,ry,2);scaled(v[2],32,ox,3);scaled(v[3],32,oy,3);
 scaled(v[4],32,rx*10-ox,3);scaled(v[5],32,ry*10-oy,3);
 scaled(v[6],32,to_scaled(s->battery,1),1);
 int n=snprintf(out,cap,"%s;%s;%s;%s;%s;%s;%s;%s;%s;%lu\n",s->date,s->time,v[0],v[1],v[2],v[3],v[4],v[5],v[6],(unsigned long)s->ms);
 if(n<0 || (size_t)n>=cap)return -1;
 /* Only numeric fields use a decimal comma; preserve the date's dots. */
 char *p=strchr(out,';'); if(p)p=strchr(p+1,';');
 if(p)for(;*p;p++)if(*p=='.')*p=',';
 return n;
}
static void close_medium(medium_t *m) {
 bool bad=false;int error=0;
 for(unsigned j=0;j<2;j++)if(m->file[j]) {
  if(fflush(m->file[j]) || fsync(fileno(m->file[j]))) { bad=true;error=errno?errno:EIO; }
  if(fclose(m->file[j])) { bad=true;error=errno?errno:EIO; }
  m->file[j]=NULL;
 }
 lock();m->status.recording=false;if(bad){m->status.failed=true;m->status.error=error;}unlock();
}
static void fail_medium(medium_t *m,int error) {
 lock();
 m->status.failed=true;m->status.error=error?error:EIO;
 m->status.dropped+=m->count;m->count=0;m->status.queued=0;
 m->status.recording=false;m->status.mounted=false;unlock();close_medium(m);
}
/* One worker per physical medium. No slow I/O occurs under the global lock. */
static void media_step(unsigned i) {
 medium_t *m=&media[i];tab5_service_sample_t s;bool have=false;
 ml(i);
 lock();
 if(m->count && !m->status.failed) {
  s=m->queue[m->head];m->head=(m->head+1)%DEPTH;m->count--;m->status.queued=m->count;have=true;
 }
 unlock();
 if(have) {
  char line[320];int n=tab5_services_format_csv(line,sizeof(line),&s);
  if(n<0 || !m->file[s.sensor-2] || fwrite(line,1,n,m->file[s.sensor-2])!=(size_t)n) {
   lock();m->status.dropped++;unlock();fail_medium(m,errno);
  } else {lock();m->status.written++;unlock();}
 }
 if(now_ms()-m->flushed>=1000) {
  bool bad=false;
  for(unsigned j=0;j<2;j++)if(m->file[j] && (fflush(m->file[j]) || fsync(fileno(m->file[j]))))bad=true;
  if(bad)fail_medium(m,errno);
  m->flushed=now_ms();
 }
 mu(i);
}
static void abort_ota(void) {
 if(ota.active && ota.handle_started)esp_ota_abort(ota.handle);
 ota.handle_started=false;
 ota.active=false;mbedtls_sha256_free(&ota.hash);
}
static void transfer_expire(void);
static void maintenance(void) {
 lock();
 if(ota.active && now_ms()-ota.touched>=30000)abort_ota();
 bool reboot=ota.committed && ota.reboot_at && now_ms()>=ota.reboot_at;
 unlock();transfer_expire();if(reboot)esp_restart();
}
esp_err_t tab5_services_record_start(const char *name) {
 if(!initialized || !basename_ok(name) || strlen(name)>56)return ESP_ERR_INVALID_ARG;
 if(!storage_ready)return ESP_ERR_INVALID_STATE;
 lock();if(recording || stopping || ota.active || ota.committed || files_busy){unlock();return ESP_ERR_INVALID_STATE;}
 recording=true;strcpy(session_name,name);unlock();
 unsigned good=0;
 for(unsigned i=0;i<2;i++) {
  medium_t *m=&media[i];ml(i);
  lock();m->head=0;m->count=0;m->status.queued=0;m->status.written=0;m->status.dropped=0;unlock();
  if(m->status.mounted && !m->status.failed) {
   bool ok=true;
   for(unsigned j=0;j<2;j++) {
    char path[128];snprintf(path,sizeof(path),"%s/%s_D%u.CSV",m->root,name,j+2);
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0666);
    if(fd<0){ok=false;break;}
    m->file[j]=fdopen(fd,"w");if(!m->file[j]){close(fd);ok=false;break;}
    if(fputs(TAB5_SERVICE_CSV_HEADER,m->file[j])<0 || fflush(m->file[j]) || fsync(fd)){ok=false;break;}
   }
   if(ok){lock();m->status.recording=true;m->flushed=now_ms();good++;unlock();}else fail_medium(m,errno);
  }
  mu(i);
 }
 if(!good){lock();recording=false;unlock();return ESP_FAIL;}return ESP_OK;
}
void tab5_services_record_stop(void) {
 if(!initialized)return;
 lock();recording=false;stopping=true;unlock();
 for(unsigned i=0;i<2;i++) {
  while(media[i].count)media_step(i);
  ml(i);close_medium(&media[i]);mu(i);
 }
 lock();stopping=false;unlock();
}
esp_err_t tab5_services_submit(const tab5_service_sample_t *s) {
 char validate[320];if(tab5_services_format_csv(validate,sizeof(validate),s)<0)return ESP_ERR_INVALID_ARG;
 if(!try_lock())return ESP_ERR_NO_MEM;
 if(!recording){unlock();return ESP_ERR_INVALID_STATE;}
 unsigned accepted=0;
 for(unsigned i=0;i<2;i++) {
  medium_t *m=&media[i];if(!m->status.recording || m->status.failed)continue;
  if(m->count==DEPTH){m->status.dropped++;continue;}
  m->queue[(m->head+m->count)%DEPTH]=*s;m->count++;m->status.queued=m->count;accepted++;
 }unlock();return accepted?ESP_OK:ESP_FAIL;
}
void tab5_services_status(tab5_medium_t i,tab5_medium_status_t *out) {
 if(!out || (unsigned)i>TAB5_USB)return;
 if(!initialized){memset(out,0,sizeof(*out));return;}
 lock();*out=media[i].status;unlock();
}
static bool valid_image_header(void) {
 const uint8_t *h=ota.header;
 /* ESP image header (24), first segment header (8), app descriptor (256). */
 uint32_t magic;memcpy(&magic,h+32,4);
 uint16_t chip=(uint16_t)h[12]|((uint16_t)h[13]<<8);
 uint16_t min_rev=(uint16_t)h[15]|((uint16_t)h[16]<<8);
 uint16_t max_rev=(uint16_t)h[17]|((uint16_t)h[18]<<8);
 if(!min_rev)min_rev=h[14]*100;
 esp_chip_info_t actual;esp_chip_info(&actual);
 uint32_t segment_size;memcpy(&segment_size,h+28,4);
 return h[0]==0xe9 && h[1]>0 && h[1]<=16 && chip==0x12 && magic==0xabcd5432 &&
        actual.revision>=min_rev && (!max_rev || max_rev==65535 || actual.revision<=max_rev) &&
        segment_size>=256 && segment_size<=ota.size-32 &&
        memchr(h+48,0,32) && memchr(h+80,0,32) &&
        !strcmp((const char*)h+48,ota.version) && !strcmp((const char*)h+80,"krenomer-esp32");
}
static bool boot_confirmed(void) {
 esp_ota_img_states_t state;
 return boot_health || (esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK && state==ESP_OTA_IMG_VALID);
}
static bool ota_line(uint32_t owner,const char *line,tab5_service_reply_fn reply,void *ctx) {
 char response[512]="ERR ota command";
 lock();
 if(!strcmp(line,"ota info")) {
  const esp_partition_t *p=esp_ota_get_next_update_partition(NULL);
  esp_chip_info_t chip;esp_chip_info(&chip);
  bool confirmed=boot_confirmed();
  snprintf(response,sizeof(response),"{\"board\":\"tab5\",\"chip\":\"esp32p4\",\"proto\":2,\"chip_revision\":%u,\"version\":\"%s\",\"max_size\":%lu,\"recording\":%s,\"boot_confirmed\":%s}",chip.revision,VERSION,(unsigned long)(p?p->size:0),recording?"true":"false",confirmed?"true":"false");
  unlock();reply(ctx,response);reply(ctx,"OK");return true;
 }
 if(!strcmp(line,"ota status"))snprintf(response,sizeof(response),"OK ota offset=%lu active=%u",(unsigned long)ota.offset,ota.active);
 else if(!strcmp(line,"ota abort")) {
  if(ota.active && ota.owner!=owner)strcpy(response,"ERR ota owner");
  else {abort_ota();strcpy(response,"OK ota aborted");}
 } else if(!strncmp(line,"ota begin ",10)) {
  char board[16],chip[16],version[32],sha[65],extra;unsigned proto;unsigned long size;
  if(ota.active || ota.committed || recording || stopping || files_busy)strcpy(response,"ERR ota busy");
  else if(!boot_confirmed())strcpy(response,"ERR ota boot unconfirmed");
  else if(sscanf(line+10,"%15s %15s %u %31s %lu %64s %c",board,chip,&proto,version,&size,sha,&extra)!=6 || strcmp(board,"tab5") || strcmp(chip,"esp32p4") || proto!=2 || !version_ok(version) || strlen(sha)!=64 || unhex(sha,ota.expected,32)!=32)strcpy(response,"ERR ota target/version/hash");
  else {
   ota.partition=esp_ota_get_next_update_partition(NULL);
   if(!ota.partition || size<288 || size>ota.partition->size || ota.partition==esp_ota_get_running_partition())strcpy(response,"ERR ota size/partition");
   else {
    ota.active=true;ota.owner=owner;ota.size=size;ota.offset=0;ota.last_len=0;ota.header_valid=false;ota.handle_started=false;ota.touched=now_ms();strcpy(ota.version,version);
    mbedtls_sha256_init(&ota.hash);mbedtls_sha256_starts(&ota.hash,0);strcpy(response,"OK ota offset=0");
   }
  }
 } else if(!strncmp(line,"ota chunk ",10)) {
  unsigned long off;char hex[513],extra;uint8_t bytes[256];int n;
  if(!ota.active || ota.owner!=owner)strcpy(response,"ERR ota owner/inactive");
  else if(sscanf(line+10,"%lu %512s %c",&off,hex,&extra)!=2 || (n=unhex(hex,bytes,sizeof(bytes)))<1)strcpy(response,"ERR ota chunk");
  else if(ota.last_len && off==ota.last_offset && (size_t)n==ota.last_len && !memcmp(bytes,ota.last,n)) {
   ota.touched=now_ms();snprintf(response,sizeof(response),"OK ota offset=%lu",(unsigned long)ota.offset);
  } else if(off!=ota.offset || (unsigned)n>ota.size-ota.offset)strcpy(response,"ERR ota offset/size");
  else {
   size_t copy=off<sizeof(ota.header)?sizeof(ota.header)-off:0;if(copy>(size_t)n)copy=n;if(copy)memcpy(ota.header+off,bytes,copy);
   bool bad=false;
   if(!ota.header_valid && off+(unsigned)n>=288){ota.header_valid=valid_image_header();bad=!ota.header_valid;}
   esp_err_t e=ESP_OK;
   if(!bad && ota.header_valid && !ota.handle_started) {
    e=esp_ota_begin(ota.partition,ota.size,&ota.handle);
    if(e==ESP_OK){ota.handle_started=true;e=esp_ota_write(ota.handle,ota.header,off+n);}
   } else if(!bad && ota.handle_started)e=esp_ota_write(ota.handle,bytes,n);
   if(bad || e!=ESP_OK){abort_ota();strcpy(response,"ERR ota image/flash");}
   else {
    mbedtls_sha256_update(&ota.hash,bytes,n);memcpy(ota.last,bytes,n);ota.last_len=n;ota.last_offset=off;ota.offset+=n;ota.touched=now_ms();
    snprintf(response,sizeof(response),"OK ota offset=%lu",(unsigned long)ota.offset);
   }
  }
 } else if(!strcmp(line,"ota end")) {
  if(!ota.active || ota.owner!=owner)strcpy(response,"ERR ota owner/inactive");
  else {
   uint8_t hash[32];mbedtls_sha256_finish(&ota.hash,hash);
   if(ota.offset!=ota.size || !ota.header_valid || memcmp(hash,ota.expected,32)){abort_ota();strcpy(response,"ERR ota incomplete/hash");}
   else {
    esp_err_t e=esp_ota_end(ota.handle);ota.active=false;mbedtls_sha256_free(&ota.hash);
    if(e!=ESP_OK)strcpy(response,"ERR ota verify");
    else if(esp_ota_set_boot_partition(ota.partition)!=ESP_OK)strcpy(response,"ERR ota boot");
    else {ota.committed=true;strcpy(response,"OK ota committed scheduled reboot");}
   }
  }
 }
 bool committed=!strcmp(response,"OK ota committed scheduled reboot");
 unlock();reply(ctx,response);
 if(committed){lock();ota.reboot_at=0;unlock();}
 return true;
}
static uint32_t crc32_update(uint32_t c,const uint8_t *p,size_t n) {
 c=~c;while(n--){c^=*p++;for(unsigned b=0;b<8;b++)c=(c>>1)^(0xedb88320u & (0u-(c&1)));}return ~c;
}
static void b64(char *out,const uint8_t *p,size_t n) {
 static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 size_t k=0;for(size_t i=0;i<n;i+=3){uint32_t v=(uint32_t)p[i]<<16;if(i+1<n)v|=(uint32_t)p[i+1]<<8;if(i+2<n)v|=p[i+2];out[k++]=t[v>>18];out[k++]=t[(v>>12)&63];out[k++]=i+1<n?t[(v>>6)&63]:'=';out[k++]=i+2<n?t[v&63]:'=';}out[k]=0;
}
/* transfer_lock serializes metadata and file I/O; never held during callbacks.
 * Lock order: transfer -> medium -> state. Recording uses medium -> state. */
static void transfer_close(void) {
 if(transfer.file)fclose(transfer.file);
 if(transfer.dir)closedir(transfer.dir);
 memset(&transfer,0,sizeof(transfer));lock();files_busy=false;unlock();
}
static bool file_line(uint32_t owner,const char *line,tab5_service_reply_fn reply,void *ctx) {
 char response[512]="";tl();
 if(transfer_error_owner==owner)transfer_error_owner=0;
 if(!strcmp(line,"get abort")) {
  if(transfer.owner && transfer.owner!=owner)strcpy(response,"ERR file owner");
  else {if(transfer.owner)transfer_close();strcpy(response,"OK get aborted");}
 } else {
  lock();bool busy=recording || stopping || ota.active || files_busy;
  if(!busy)files_busy=true;
  unsigned i=media[0].status.mounted&&!media[0].status.failed?0:1;unlock();
  if(busy)strcpy(response,"ERR busy");
  else {
   transfer.owner=owner;transfer.medium=i;transfer.progress=now_ms();ml(i);
   if(!strcmp(line,"files")) {
    transfer.dir=opendir(media[i].root);
    if(!transfer.dir){transfer_close();strcpy(response,"ERR media");}
   } else if(!basename_ok(line+4)){transfer_close();strcpy(response,"ERR filename");}
   else {
    char path[160];snprintf(path,sizeof(path),"%s/%s",media[i].root,line+4);
    transfer.file=fopen(path,"rb");struct stat st;
    if(!transfer.file || fstat(fileno(transfer.file),&st) || !S_ISREG(st.st_mode)){transfer_close();strcpy(response,"ERR file");}
    else {transfer.size=st.st_size;snprintf(response,sizeof(response),"G,%s,%lu,0",line+4,(unsigned long)transfer.size);}
   }
   mu(i);
  }
 }
 tu();if(*response)reply(ctx,response);return true;
}
void tab5_services_poll(uint32_t owner,tab5_service_reply_fn reply,void *ctx) {
 if(!initialized || !owner || !reply)return;
 char response[512]="";tl();
 if(transfer_error_owner==owner){strcpy(response,transfer_error);transfer_error_owner=0;tu();reply(ctx,response);return;}
 if(!transfer.owner || transfer.owner!=owner){tu();return;}
 unsigned i=transfer.medium;ml(i);
 if(now_ms()-transfer.progress>=5000){transfer_close();strcpy(response,"ERR file timeout");}
 else if(transfer.stage==2){transfer_close();strcpy(response,"OK");}
 else if(transfer.dir) {
  struct dirent *e=readdir(transfer.dir);
  if(!e){transfer_close();strcpy(response,"OK");}
  else if(basename_ok(e->d_name) && strstr(e->d_name,".CSV")) {
   char path[160];snprintf(path,sizeof(path),"%s/%.63s",media[i].root,e->d_name);struct stat st;
   if(!stat(path,&st) && S_ISREG(st.st_mode))snprintf(response,sizeof(response),"F,%s,%lu,",e->d_name,(unsigned long)st.st_size);
  }
 } else if(transfer.file) {
  uint8_t data[192];size_t n=fread(data,1,sizeof(data),transfer.file);
  if(n){transfer.crc=crc32_update(transfer.crc,data,n);transfer.total+=n;strcpy(response,"D,");b64(response+2,data,n);}
  else if(ferror(transfer.file) || transfer.total!=transfer.size){transfer_close();strcpy(response,"ERR media read");}
  else {snprintf(response,sizeof(response),"E,%lu,%08lx",(unsigned long)transfer.total,(unsigned long)transfer.crc);transfer.stage=2;}
 }
 if(transfer.owner)transfer.progress=now_ms();
 mu(i);tu();if(*response)reply(ctx,response);
}
static void transfer_expire(void) {
 tl();if(transfer.owner && now_ms()-transfer.progress>=5000) {
  unsigned i=transfer.medium;transfer_error_owner=transfer.owner;strcpy(transfer_error,"ERR file timeout");ml(i);transfer_close();mu(i);
 }tu();
}
static void transfer_cancel(uint32_t owner,int medium) {
 if(!initialized)return;
 tl();if(transfer.owner && ((!owner && medium==(int)transfer.medium) || owner==transfer.owner)) {
  if(!owner){transfer_error_owner=transfer.owner;strcpy(transfer_error,"ERR media removed");}
  unsigned i=transfer.medium;ml(i);transfer_close();mu(i);
 }
 if(owner && transfer_error_owner==owner)transfer_error_owner=0;
 tu();
}
bool tab5_services_line(uint32_t owner,const char *line,bool auth,tab5_service_reply_fn reply,void *ctx) {
 if(!line || !reply)return false;
 bool isota=!strncmp(line,"ota",3) && (!line[3] || line[3]==' ');
 bool isfile=!strcmp(line,"files") || !strncmp(line,"get ",4);
 if(!isota && !isfile)return false;
 if(!initialized || !owner || !auth || strlen(line)>=TAB5_SERVICE_LINE_MAX){reply(ctx,"ERR auth/line");return true;}
 return isota?ota_line(owner,line,reply,ctx):file_line(owner,line,reply,ctx);
}
void tab5_services_disconnect(uint32_t owner) {if(!initialized)return;transfer_cancel(owner,-1);lock();if(ota.active && ota.owner==owner)abort_ota();unlock();}
void tab5_services_response_flushed(uint32_t owner) {
 if(!initialized)return;
 lock();if(ota.committed && ota.owner==owner && !ota.reboot_at)ota.reboot_at=now_ms()+500;unlock();
}
esp_err_t tab5_services_confirm_boot(void) {
 const esp_partition_t *running=esp_ota_get_running_partition();esp_ota_img_states_t state;
 esp_err_t e=esp_ota_get_state_partition(running,&state);
 if(e==ESP_ERR_NOT_FOUND) {e=esp_ota_set_boot_partition(running);if(e!=ESP_OK)return e;}
 else if(e!=ESP_OK)return e;
 e=esp_ota_mark_app_valid_cancel_rollback();
 if(e==ESP_OK){lock();boot_health=true;unlock();}return e;
}
void tab5_services_network_snapshot(tab5_network_snapshot_t *out) {if(out){if(!initialized){memset(out,0,sizeof(*out));return;}lock();*out=network_status;unlock();}}

#ifndef TAB5_SERVICES_HOST
static bool fat32_drive(BYTE drive_number) {
 if(drive_number>=10)return false;
 char drive[3]={(char)('0'+drive_number),':',0};FATFS *fs=NULL;DWORD clusters;
 return f_getfree(drive,&clusters,&fs)==FR_OK && fs && fs->fs_type==FS_FAT32;
}
static esp_err_t mount_sd(void) {
 esp_vfs_fat_sdmmc_mount_config_t mount={.format_if_mount_failed=false,.max_files=6,.allocation_unit_size=16384};
 sdmmc_host_t host;sdmmc_slot_config_t slot;bsp_sdcard_get_sdmmc_host(SDMMC_HOST_SLOT_0,&host);bsp_sdcard_sdmmc_get_slot(SDMMC_HOST_SLOT_0,&slot);
 bsp_sdcard_cfg_t cfg={.mount=&mount,.host=&host,.slot.sdmmc=&slot};
 esp_err_t e=bsp_sdcard_sdmmc_mount(&cfg);
 if(e==ESP_OK && !fat32_drive(ff_diskio_get_pdrv_card(bsp_sdcard_get_handle()))){bsp_sdcard_unmount();return ESP_FAIL;}
 return e;
}
static void usb_event(const msc_host_event_t *event,void *ctx) {(void)ctx;xQueueSend(usb_events,event,0);}
static void media_task(void *arg) {
 unsigned i=(uintptr_t)arg;uint64_t retried=0;
 for(;;){
  if(i==1) {
   msc_host_event_t e;
   while(xQueueReceive(usb_events,&e,0)==pdTRUE) {
    if(e.event==MSC_DEVICE_CONNECTED)usb_address=e.device.address;
    if(e.event==MSC_DEVICE_DISCONNECTED && e.device.handle==usb_device) {
     transfer_cancel(0,i);ml(i);fail_medium(&media[i],ENODEV);lock();media[i].status.mounted=false;unlock();
     if(usb_vfs)msc_host_vfs_unregister(usb_vfs);
     usb_vfs=NULL;
     msc_host_uninstall_device(usb_device);usb_device=NULL;usb_address=0;mu(i);
    }
   }
  }
  lock();bool retry=!recording && !stopping && !files_busy && !media[i].status.mounted && now_ms()-retried>=2000;unlock();
  if(retry){retried=now_ms();tab5_services_retry(i);}
  media_step(i);if(i==0)maintenance();vTaskDelay(pdMS_TO_TICKS(10));
 }
}
esp_err_t tab5_services_retry(tab5_medium_t i) {
 if(!initialized)return ESP_ERR_INVALID_STATE;
 if((unsigned)i>TAB5_USB)return ESP_ERR_INVALID_ARG;
 lock();if(recording || stopping || files_busy){unlock();return ESP_ERR_INVALID_STATE;}
 if(i==TAB5_USB && !usb_driver_ready){esp_err_t e=usb_driver_error;unlock();return e;}
 files_busy=true;unlock();
 ml(i);esp_err_t e;
 if(i==0){if(bsp_sdcard_get_handle())bsp_sdcard_unmount();e=mount_sd();}
 else {
  if(usb_vfs){msc_host_vfs_unregister(usb_vfs);usb_vfs=NULL;}
  if(usb_device){msc_host_uninstall_device(usb_device);usb_device=NULL;}
  e=usb_address?msc_host_install_device(usb_address,&usb_device):ESP_ERR_NOT_FOUND;
  esp_vfs_fat_mount_config_t m={.format_if_mount_failed=false,.max_files=6,.allocation_unit_size=16384};
  BYTE drive=0xff;
  if(e==ESP_OK)e=ff_diskio_get_drive(&drive);
  if(e==ESP_OK)e=msc_host_vfs_register(usb_device,media[i].root,&m,&usb_vfs);
  if(e==ESP_OK && !fat32_drive(drive)){msc_host_vfs_unregister(usb_vfs);usb_vfs=NULL;e=ESP_FAIL;}
 }
 lock();media[i].status.mounted=e==ESP_OK;media[i].status.failed=e!=ESP_OK;media[i].status.error=e;files_busy=false;unlock();mu(i);return e;
}
typedef struct {int fd;uint32_t owner,epoch;bool auth,overflow,dead;size_t used;char rx[TAB5_SERVICE_LINE_MAX];char tx[OUTPUT*8];size_t tx_len,tx_sent;uint64_t connected;} client_t;
static client_t clients[CLIENTS];
static void tcp_reply(void *ctx,const char *line) {
 client_t *c=ctx;size_t n=strlen(line);
 if(c->dead || n>=OUTPUT || c->tx_len+n+1>sizeof(c->tx)){c->dead=true;return;}
 memcpy(c->tx+c->tx_len,line,n);c->tx_len+=n;c->tx[c->tx_len++]='\n';
 /* File transfers can exceed the queue: bounded socket drain in this task only. */
 if(c->tx_len>OUTPUT*6) {
  int sent=send(c->fd,c->tx+c->tx_sent,c->tx_len-c->tx_sent,0);
  if(sent>0)c->tx_sent+=sent;
  if(c->tx_sent==c->tx_len)c->tx_len=c->tx_sent=0;
 }
}
static void drop_client(client_t *c) {
 tab5_services_disconnect(c->owner);close(c->fd);c->fd=-1;c->auth=false;
}
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
 (void)arg;lock();
 if(base==WIFI_EVENT && id==WIFI_EVENT_AP_START){network_status.ap_started=true;strcpy(network_status.ap_ip,"192.168.4.1");}
 if(base==WIFI_EVENT && id==WIFI_EVENT_AP_STOP)network_status.ap_started=false;
 if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){network_status.sta_connected=false;network_status.sta_ip[0]=0;}
 if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP){ip_event_got_ip_t *e=data;network_status.sta_connected=true;snprintf(network_status.sta_ip,16,IPSTR,IP2STR(&e->ip_info.ip));}
 bool connect=base==WIFI_EVENT && (id==WIFI_EVENT_STA_START || id==WIFI_EVENT_STA_DISCONNECTED) && config.network.sta_enabled;
 unlock();if(connect)esp_wifi_connect();
}
static esp_err_t configure_wifi(const tab5_network_config_t *cfg) {
 esp_err_t e;
 if(!wifi_initialized) {
  if((e=bsp_feature_enable(BSP_FEATURE_WIFI,true))!=ESP_OK)return e;
  e=esp_netif_init();if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE)return e;
  e=esp_event_loop_create_default();if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE)return e;
  if(!ap_netif)ap_netif=esp_netif_create_default_wifi_ap();
  if(!sta_netif)sta_netif=esp_netif_create_default_wifi_sta();
  if(!ap_netif||!sta_netif)return ESP_ERR_NO_MEM;
  wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();if((e=esp_wifi_init(&init))!=ESP_OK)return e;
  wifi_initialized=true;
 }
 if(!wifi_events_registered){e=esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL);if(e!=ESP_OK)return e;wifi_events_registered=true;}
 if(!ip_events_registered){e=esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL);if(e!=ESP_OK)return e;ip_events_registered=true;}
 e=esp_wifi_stop();if(e!=ESP_OK && e!=ESP_ERR_WIFI_NOT_STARTED)return e;
 wifi_mode_t mode=cfg->ap_enabled?(cfg->sta_enabled?WIFI_MODE_APSTA:WIFI_MODE_AP):WIFI_MODE_STA;
 if((e=esp_wifi_set_mode(mode))!=ESP_OK)return e;
 if(cfg->ap_enabled){wifi_config_t w={0};memcpy(w.ap.ssid,cfg->ap_ssid,strlen(cfg->ap_ssid));strcpy((char*)w.ap.password,cfg->ap_password);w.ap.ssid_len=strlen(cfg->ap_ssid);w.ap.channel=1;w.ap.max_connection=4;w.ap.authmode=WIFI_AUTH_WPA2_PSK;if((e=esp_wifi_set_config(WIFI_IF_AP,&w))!=ESP_OK)return e;}
 if(cfg->sta_enabled){wifi_config_t w={0};memcpy(w.sta.ssid,cfg->sta_ssid,strlen(cfg->sta_ssid));memcpy(w.sta.password,cfg->sta_password,strlen(cfg->sta_password));if((e=esp_wifi_set_config(WIFI_IF_STA,&w))!=ESP_OK)return e;}
 return esp_wifi_start();
}
static int start_listener(void) {
 int listener=socket(AF_INET,SOCK_STREAM,IPPROTO_IP);if(listener<0)return -1;
 int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
 struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(5000),.sin_addr.s_addr=htonl(INADDR_ANY)};
 if(bind(listener,(struct sockaddr*)&addr,sizeof(addr)) || listen(listener,4) || fcntl(listener,F_SETFL,O_NONBLOCK)<0){int e=errno;close(listener);errno=e;return -1;}
 return listener;
}
static void network_task(void *arg) {
 int listener=(int)(intptr_t)arg;
 for(unsigned i=0;i<CLIENTS;i++)clients[i].fd=-1;
 uint32_t next_owner=2;
 for(;;) {
  int fd=accept(listener,NULL,NULL);if(fd>=0){unsigned i;for(i=0;i<CLIENTS;i++)if(clients[i].fd<0)break;if(i==CLIENTS)close(fd);else {fcntl(fd,F_SETFL,O_NONBLOCK);memset(&clients[i],0,sizeof(client_t));clients[i].fd=fd;clients[i].owner=next_owner++;if(next_owner<2)next_owner=2;clients[i].connected=now_ms();}}
  output_t item;unsigned budget=8;
  while(budget-- && xQueueReceive(publish_queue,&item,0)==pdTRUE)for(unsigned i=0;i<CLIENTS;i++)if(clients[i].fd>=0 && clients[i].auth)tcp_reply(&clients[i],item.text);
  unsigned connected=0;
  for(unsigned i=0;i<CLIENTS;i++) {
   client_t *c=&clients[i];if(c->fd<0)continue;
   lock();bool stale=c->auth && c->epoch!=network_epoch;unlock();
   if(stale){drop_client(c);continue;}
   char input[256];int n=recv(c->fd,input,sizeof(input),0);
   if(n==0 || (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK))c->dead=true;
   for(int j=0;j<n && !c->dead;j++) {
    char ch=input[j];if(ch=='\r')continue;
    if(ch=='\n') {
     c->rx[c->used]=0;
     if(c->overflow)tcp_reply(c,"ERR line too long");
     else if(!c->auth) {
      lock();bool ok=!strncmp(c->rx,"auth ",5) && !strcmp(c->rx+5,config.network.auth_token);c->epoch=network_epoch;unlock();
      if(ok){c->auth=true;tcp_reply(c,"OK");}else{tcp_reply(c,"ERR auth");c->dead=true;}
     } else if(!tab5_services_line(c->owner,c->rx,true,tcp_reply,c) && config.command)config.command(config.command_ctx,c->rx,tcp_reply,c);
     c->used=0;c->overflow=false;
    } else if(ch==0)c->overflow=true;
    else if(c->used<sizeof(c->rx)-1)c->rx[c->used++]=ch;else c->overflow=true;
   }
   if(c->auth && !c->dead && c->tx_len<OUTPUT*2)tab5_services_poll(c->owner,tcp_reply,c);
   if(c->tx_len>c->tx_sent){int sent=send(c->fd,c->tx+c->tx_sent,c->tx_len-c->tx_sent,0);if(sent>0)c->tx_sent+=sent;else if(sent<0 && errno!=EWOULDBLOCK && errno!=EAGAIN)c->dead=true;}
   if(c->tx_len==c->tx_sent){c->tx_len=c->tx_sent=0;tab5_services_response_flushed(c->owner);}
   if(!c->auth && now_ms()-c->connected>10000)c->dead=true;
   if(c->dead)drop_client(c);else if(c->auth)connected++;
  }
  lock();network_status.clients=connected;unlock();vTaskDelay(pdMS_TO_TICKS(10));
 }
}
esp_err_t tab5_services_publish(const char *line) {
 if(!initialized || !line || strlen(line)>=OUTPUT)return ESP_ERR_INVALID_ARG;
 output_t out;strcpy(out.text,line);return xQueueSend(publish_queue,&out,0)==pdTRUE?ESP_OK:ESP_ERR_NO_MEM;
}
#else
esp_err_t tab5_services_retry(tab5_medium_t i) {if((unsigned)i>TAB5_USB)return ESP_ERR_INVALID_ARG;if(recording)return ESP_ERR_INVALID_STATE;if(i==TAB5_USB && !usb_driver_ready)return usb_driver_error;media[i].status.mounted=test_present[i];media[i].status.failed=!test_present[i];return test_present[i]?ESP_OK:ESP_FAIL;}
esp_err_t tab5_services_publish(const char *s) {return s && strlen(s)<OUTPUT?ESP_OK:ESP_ERR_INVALID_ARG;}
static esp_err_t configure_wifi(const tab5_network_config_t *c) {(void)c;test_wifi_starts++;return test_wifi_start_error;}
#endif
/* Startup orchestration below is shared verbatim by ESP and host regression
 * tests. Only the platform allocation/driver/socket/task boundaries differ. */
static esp_err_t startup_core(void) {
#ifndef TAB5_SERVICES_HOST
 network_lock=xSemaphoreCreateMutex();transfer_lock=xSemaphoreCreateMutex();state_lock=xSemaphoreCreateMutex();medium_lock[0]=xSemaphoreCreateMutex();medium_lock[1]=xSemaphoreCreateMutex();publish_queue=xQueueCreate(16,sizeof(output_t));usb_events=xQueueCreate(8,sizeof(msc_host_event_t));
 if(!network_lock||!transfer_lock||!state_lock||!medium_lock[0]||!medium_lock[1]||!publish_queue||!usb_events)return ESP_ERR_NO_MEM;
 strcpy(media[0].root,BSP_SD_MOUNT_POINT);strcpy(media[1].root,"/usb");
#else
 test_core_starts++;if(test_core_start_error)return test_core_start_error;
 strcpy(media[0].root,"test-sd");strcpy(media[1].root,"test-usb");
#endif
 return ESP_OK;
}
static esp_err_t startup_usb(void) {
#ifndef TAB5_SERVICES_HOST
 esp_err_t e=bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV,false);
 msc_host_driver_config_t m={.create_backround_task=true,.task_priority=5,.stack_size=4096,.core_id=tskNO_AFFINITY,.callback=usb_event};
 if(e==ESP_OK)e=msc_host_install(&m);
 return e;
#else
 test_usb_starts++;return test_usb_start_error;
#endif
}
static esp_err_t startup_workers(void) {
#ifndef TAB5_SERVICES_HOST
 if(xTaskCreate(media_task,"sd_mirror",6144,(void*)0,4,NULL)!=pdPASS || xTaskCreate(media_task,"usb_mirror",6144,(void*)1,4,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
#else
 test_worker_starts++;if(test_worker_start_error)return test_worker_start_error;
#endif
 return ESP_OK;
}
static esp_err_t startup_netif(void) {
#ifndef TAB5_SERVICES_HOST
 esp_err_t e=esp_netif_init();return e==ESP_ERR_INVALID_STATE?ESP_OK:e;
#else
 test_netif_starts++;return test_netif_start_error;
#endif
}
static int startup_listener(void) {
#ifndef TAB5_SERVICES_HOST
 return start_listener();
#else
 test_listener_starts++;if(test_listener_start_error){errno=test_listener_start_error;return -1;}return 42;
#endif
}
static esp_err_t startup_network_task(int listener) {
#ifndef TAB5_SERVICES_HOST
 if(xTaskCreate(network_task,"tcp_service",16384,(void*)(intptr_t)listener,4,NULL)!=pdPASS){close(listener);return ESP_ERR_NO_MEM;}
#else
 (void)listener;test_network_task_starts++;if(test_network_task_start_error)return test_network_task_start_error;
#endif
 return ESP_OK;
}
/* Serialized by network_lock, never by the state lock during I/O. */
static esp_err_t ensure_tcp(void) {
 if(tcp_started)return ESP_OK;
 int listener=startup_listener();if(listener<0)return errno?errno:ESP_FAIL;
 esp_err_t e=startup_network_task(listener);
 if(e!=ESP_OK){network_task_failed=true;return e;}
 tcp_started=true;lock();network_status.tcp_listening=true;unlock();return ESP_OK;
}
static bool network_config_valid(const tab5_network_config_t *cfg) {
 return cfg && memchr(cfg->ap_ssid,0,33) && memchr(cfg->ap_password,0,65) &&
  memchr(cfg->sta_ssid,0,33) && memchr(cfg->sta_password,0,65) && memchr(cfg->auth_token,0,65) &&
  strlen(cfg->auth_token)>=8 && !strcmp(cfg->auth_token,cfg->ap_password) &&
  strlen(cfg->ap_password)<=63 && (!cfg->tcp_port || cfg->tcp_port==5000);
}
esp_err_t tab5_services_network_configure(const tab5_network_config_t *cfg) {
 if(!initialized){network_status.last_error=ESP_ERR_INVALID_STATE;return ESP_ERR_INVALID_STATE;}
 nl();network_task_failed=false;
 bool disabled=cfg && !cfg->ap_enabled && !cfg->sta_enabled;
 esp_err_t e=ESP_OK;
 if(!cfg || (!disabled && !network_config_valid(cfg)))e=ESP_ERR_INVALID_ARG;
 else {
  lock();config.network=*cfg;config.network.tcp_port=5000;
#ifndef TAB5_SERVICES_HOST
  network_epoch++;
#endif
  unlock();
  if(disabled) {
#ifndef TAB5_SERVICES_HOST
   if(wifi_initialized){e=esp_wifi_stop();if(e==ESP_ERR_WIFI_NOT_STARTED)e=ESP_OK;}
#endif
   lock();network_status.initialized=false;network_status.ap_started=false;network_status.sta_connected=false;unlock();
  } else {
   e=startup_netif();if(e==ESP_OK)e=configure_wifi(cfg);
   lock();network_status.initialized=e==ESP_OK;unlock();
   if(e==ESP_OK)e=ensure_tcp();
  }
 }
 /* Includes argument, BSP, netif, event loop, esp_wifi and socket failures. */
 lock();network_status.last_error=e;unlock();nu();return e;
}
static esp_err_t startup_network(const tab5_network_config_t *cfg) {
 esp_err_t e=tab5_services_network_configure(cfg);
 return network_task_failed?e:ESP_OK;
}
esp_err_t tab5_services_init(const tab5_services_config_t *cfg) {
 if(!cfg || initialized)return ESP_ERR_INVALID_STATE;
 config=*cfg;
 esp_err_t e=startup_core();if(e!=ESP_OK)return e;
 initialized=true;
 tab5_services_retry(TAB5_SD); /* Missing/failed storage is optional. */
 e=startup_usb();
 lock();usb_driver_error=e;usb_driver_ready=e==ESP_OK;
 media[TAB5_USB].status.error=e;media[TAB5_USB].status.failed=e!=ESP_OK;unlock();
#ifdef TAB5_SERVICES_HOST
 if(usb_driver_ready)tab5_services_retry(TAB5_USB);
#endif
 e=startup_workers();if(e!=ESP_OK)return e;
 storage_ready=true;
 return startup_network(&cfg->network);
}
