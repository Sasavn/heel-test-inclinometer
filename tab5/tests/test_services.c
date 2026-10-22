/* Includes the PRODUCTION service core. Only OS/media/flash boundaries replaced. */
#define TAB5_SERVICES_HOST 1
#include "../main/tab5_services.c"
#undef fwrite
#undef fsync
#include <assert.h>
#include <direct.h>
static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static size_t test_write(const void *p,size_t n,size_t count,FILE *f){for(unsigned i=0;i<2;i++)for(unsigned j=0;j<2;j++)if(f==media[i].file[j] && (test_fault[i] || !test_present[i])){errno=test_fault[i]?test_fault[i]:ENODEV;return 0;}return fwrite(p,n,count,f);}
static int test_sync(int fd){for(unsigned i=0;i<2;i++)for(unsigned j=0;j<2;j++)if(media[i].file[j] && fd==fileno(media[i].file[j]) && (test_fault[i] || !test_present[i])){errno=test_fault[i]?test_fault[i]:ENODEV;return -1;}return _commit(fd);}
static char answer[512];static unsigned replies;
static char info[512];static uint32_t received_crc,received_bytes,final_crc,final_bytes;
static void reply(void *ctx,const char *s){
 (void)ctx;CHECK(!test_lock_depth);snprintf(answer,sizeof(answer),"%s",s);replies++;
 if(*s=='{')snprintf(info,sizeof(info),"%s",s);
 if(!strncmp(s,"G,",2))received_crc=received_bytes=final_crc=final_bytes=0;
 if(!strncmp(s,"D,",2)) {
  const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";uint8_t decoded[256];size_t len=0;unsigned bits=0,value=0;
  for(const char *p=s+2;*p && *p!='=';p++){const char *digit=strchr(alphabet,*p);CHECK(digit!=NULL);value=(value<<6)|(unsigned)(digit-alphabet);bits+=6;if(bits>=8){bits-=8;decoded[len++]=(value>>bits)&255;}}
  received_bytes+=len;received_crc=crc32_update(received_crc,decoded,len);
 }
 if(!strncmp(s,"E,",2)){unsigned long bytes,crc;CHECK(sscanf(s,"E,%lu,%lx",&bytes,&crc)==2);final_bytes=bytes;final_crc=crc;}
}
static void line(unsigned owner,const char *s){CHECK(tab5_services_line(owner,s,true,reply,NULL));}
static uint8_t image[768];static char digest[65];
static void image_hash(void){uint8_t hash[32];mbedtls_sha256_context c;mbedtls_sha256_init(&c);mbedtls_sha256_starts(&c,0);mbedtls_sha256_update(&c,image,sizeof(image));mbedtls_sha256_finish(&c,hash);mbedtls_sha256_free(&c);for(unsigned i=0;i<32;i++)sprintf(digest+i*2,"%02x",hash[i]);}
static void begin(void){char s[200];image_hash();snprintf(s,sizeof(s),"ota begin tab5 esp32p4 2 1.5.0-esp32 %u %s",(unsigned)sizeof(image),digest);line(1,s);CHECK(!strcmp(answer,"OK ota offset=0"));}
static void chunk(unsigned offset,unsigned n){char s[1100];int at=snprintf(s,sizeof(s),"ota chunk %u ",offset);for(unsigned i=0;i<n;i++)sprintf(s+at+i*2,"%02x",image[offset+i]);line(1,s);}
static void upload(void){for(unsigned i=0;i<sizeof(image);i+=256)chunk(i,256);}
static void reset_ota(void){abort_ota();memset(&ota,0,sizeof(ota));test_flash_fail=test_verify_fail=test_boot_fail=0;test_boot_selected=test_restarted=0;}
int main(void){
 _mkdir("test-sd");_mkdir("test-usb");
 tab5_services_config_t cfg={0};CHECK(tab5_services_init(&cfg)==0);
 memset(image,0,sizeof(image));image[0]=0xe9;image[1]=3;image[12]=0x12;image[15]=100;image[17]=0x8f;image[18]=1;uint32_t magic=0xabcd5432,segment_size=256;memcpy(image+28,&segment_size,4);memcpy(image+32,&magic,4);strcpy((char*)image+48,VERSION);strcpy((char*)image+80,"krenomer-esp32");
 tab5_service_sample_t s={.sensor=2,.date="01.06.2026",.time="09:05:07",.raw_x=-.46,.raw_y=1.25,.offset_x=.125,.battery=12.4,.ms=1234};char row[320];
 CHECK(tab5_services_format_csv(row,sizeof(row),&s)>0);CHECK(!strcmp(row,"01.06.2026;09:05:07;-0,46;1,25;0,125;0,000;-0,585;1,250;12,4;1234\n"));
 s.raw_x=-0.0001;s.offset_x=-0.0001;tab5_services_format_csv(row,sizeof(row),&s);CHECK(!strstr(row,"-0,00"));CHECK(to_scaled(.125,2)==12);CHECK(to_scaled(.375,2)==38);
 s.battery=NAN;tab5_services_format_csv(row,sizeof(row),&s);CHECK(strstr(row,";0,0;1234\n")!=NULL);s.battery=12.4;
 CHECK(crc32_update(0,(const uint8_t*)"123456789",9)==0xcbf43926u);
 char encoded[20];b64(encoded,(const uint8_t*)"f",1);CHECK(!strcmp(encoded,"Zg=="));b64(encoded,(const uint8_t*)"fo",2);CHECK(!strcmp(encoded,"Zm8="));
 line(1,"ota info");CHECK(!strcmp(answer,"OK"));CHECK(replies==2);CHECK(strstr(info,"\"boot_confirmed\":false")!=NULL);
 CHECK(!boot_health && test_state_blank);CHECK(tab5_services_confirm_boot()==ESP_OK);CHECK(test_slot_valid[0] && !test_state_blank && boot_health);test_boot_selected=0;
 line(1,"ota begin tab5 esp32s3 2 1.5.0-esp32 768 00");CHECK(!strncmp(answer,"ERR",3));
 line(1,"ota begin tab5 esp32p4 1 1.5.0-esp32 768 00");CHECK(!strncmp(answer,"ERR",3));
 CHECK(!version_ok("1.4.9-esp32"));CHECK(!version_ok("1.5.0-tab5"));CHECK(version_ok(VERSION));
 CHECK(!version_ok("+1.5.0-esp32"));CHECK(!version_ok("99999999999999999999.5.0-esp32"));
 strcpy(test_current_version,"2.0.0-esp32");CHECK(!version_ok("1.99.99-esp32"));CHECK(version_ok("2.0.0-esp32"));
 CHECK(version_ok("2.0.1-esp32"));strcpy(test_current_version,"1.5.0-esp32");
 strcpy(test_current_version,"1.6.0-esp32");CHECK(!version_ok("1.5.0-esp32"));CHECK(version_ok("1.6.0-esp32"));
 strcpy(test_current_version,"1.5.0-esp32");
 begin();CHECK(tab5_services_record_start("blocked")!=0);chunk(0,256);CHECK(ota.offset==256);size_t written=test_flash_offset;chunk(0,256);CHECK(ota.offset==256 && written==test_flash_offset);
 line(2,"ota abort");CHECK(ota.active);tab5_services_disconnect(2);CHECK(ota.active);tab5_services_disconnect(1);CHECK(!ota.active && !test_boot_selected);
 begin();test_now+=30000;maintenance();CHECK(!ota.active && !test_boot_selected);
 begin();chunk(0,256);reset_ota();CHECK(!test_boot_selected); /* power loss before commit */
 begin();chunk(0,255);chunk(255,32);CHECK(!ota.handle_started && ota.offset==287);chunk(287,256);CHECK(ota.handle_started && test_flash_offset==543);chunk(543,225);line(1,"ota end");CHECK(!strcmp(answer,"OK ota committed scheduled reboot"));CHECK(!memcmp(image,test_flash,sizeof(image)));reset_ota();
 unsigned erased=test_erases;image[12]=9;begin();upload();CHECK(!ota.active && !test_boot_selected && test_erases==erased);image[12]=0x12;
 image[17]=199;image[18]=0;begin();upload();CHECK(!ota.active && test_erases==erased);image[17]=0x8f;image[18]=1;
 image[0]=0;begin();upload();CHECK(!ota.active && !test_boot_selected);image[0]=0xe9;
 strcpy((char*)image+80,"krenomer-tab5");begin();upload();CHECK(!ota.active && !test_boot_selected);strcpy((char*)image+80,"krenomer-esp32");
 begin();image[500]^=1;upload();line(1,"ota end");CHECK(!strncmp(answer,"ERR",3) && !test_boot_selected);image[500]^=1;
 begin();test_flash_fail=1;chunk(0,256);chunk(256,256);CHECK(!ota.active);reset_ota();
 begin();upload();test_verify_fail=1;line(1,"ota end");CHECK(!test_boot_selected);reset_ota();
 begin();upload();test_boot_fail=1;line(1,"ota end");CHECK(!test_boot_selected);reset_ota();
 CHECK(tab5_services_record_start("2026-10-21_M001")==0);
 line(1,"ota begin tab5 esp32p4 2 1.5.0-esp32 768 0000000000000000000000000000000000000000000000000000000000000000");CHECK(!strcmp(answer,"ERR ota busy"));
 CHECK(tab5_services_retry(0)!=0);CHECK(tab5_services_submit(&s)==0);media_step(0);media_step(1);CHECK(media[0].status.written==1 && media[1].status.written==1);
 test_lock_busy=1;CHECK(tab5_services_submit(&s)==ESP_ERR_NO_MEM);test_lock_busy=0;CHECK(!media[0].count && !media[1].count);
 test_present[0]=false;CHECK(tab5_services_submit(&s)==0);media_step(0);media_step(1);CHECK(media[0].status.failed && media[1].status.written==2);test_present[0]=true;CHECK(tab5_services_retry(0)!=0);tab5_services_record_stop();CHECK(tab5_services_retry(0)==0);
 CHECK(tab5_services_record_start("2026-10-21_M002")==0);CHECK(!media[0].status.written && !media[1].status.written && !media[0].status.dropped);test_fault[1]=ENOSPC;CHECK(tab5_services_submit(&s)==0);media_step(1);media_step(0);CHECK(media[1].status.failed && !media[0].status.failed);tab5_services_record_stop();test_fault[1]=0;CHECK(tab5_services_retry(1)==0);
 CHECK(tab5_services_record_start("2026-10-21_M003")==0);for(unsigned i=0;i<DEPTH;i++)CHECK(tab5_services_submit(&s)==0);CHECK(tab5_services_submit(&s)!=0);CHECK(media[0].status.dropped==1 && media[1].status.dropped==1);media_step(1);CHECK(tab5_services_submit(&s)==0);CHECK(media[0].status.dropped==2 && media[1].status.dropped==1);tab5_services_record_stop();
 CHECK(tab5_services_record_start("2026-10-21_M001")!=0);CHECK(media[0].status.error==EEXIST);CHECK(tab5_services_retry(0)==0);CHECK(tab5_services_retry(1)==0);
 line(1,"get 2026-10-21_M001_D2.CSV");CHECK(!strncmp(answer,"G,",2));unsigned guard=0;while(files_busy && guard++<100)tab5_services_poll(1,reply,NULL);CHECK(!files_busy && !strcmp(answer,"OK"));
 CHECK(received_bytes==final_bytes && received_crc==final_crc && received_bytes>0);
 FILE *large=fopen("test-sd/large.CSV","wb");CHECK(large!=NULL);uint32_t expected_crc=0;for(unsigned i=0;i<100000;i++){uint8_t byte=i%251;CHECK(fputc(byte,large)!=EOF);expected_crc=crc32_update(expected_crc,&byte,1);}fclose(large);
 line(2,"get large.CSV");guard=0;while(files_busy && guard++<1000){unsigned before=replies;tab5_services_poll(2,reply,NULL);CHECK(replies<=before+1);}CHECK(!files_busy && !strcmp(answer,"OK"));CHECK(received_bytes==100000 && final_bytes==100000 && received_crc==expected_crc && final_crc==expected_crc);
 line(2,"get large.CSV");transfer_cancel(0,0);tab5_services_poll(2,reply,NULL);CHECK(!strcmp(answer,"ERR media removed") && !files_busy);
 line(2,"get large.CSV");test_now+=5001;maintenance();CHECK(!files_busy);tab5_services_poll(2,reply,NULL);CHECK(!strcmp(answer,"ERR file timeout"));
 line(1,"get ../bad");CHECK(!strcmp(answer,"ERR filename"));line(1,"files");tab5_services_disconnect(1);CHECK(!files_busy);
 line(1,"files");test_now+=5001;tab5_services_poll(1,reply,NULL);CHECK(!strcmp(answer,"ERR file timeout") && !files_busy);
 reset_ota();begin();upload();line(1,"ota end");CHECK(!strcmp(answer,"OK ota committed scheduled reboot"));CHECK(test_boot_selected==1);test_now+=10000;maintenance();CHECK(!test_restarted);tab5_services_response_flushed(1);test_now+=499;maintenance();CHECK(!test_restarted);test_now++;maintenance();CHECK(test_restarted==1);CHECK(tab5_services_confirm_boot()==0);
 CHECK(test_selected_index==1 && test_slot_valid[0]);test_running_index=1;test_slot_valid[1]=0; /* boot into pending update */
 if(!test_slot_valid[test_running_index])test_running_index=0; /* bootloader power-loss rollback */
 CHECK(test_running_index==0 && test_slot_valid[0]);
 printf("services: %u checks passed (production core, injected media + flash)\n",checks);return 0;
}
