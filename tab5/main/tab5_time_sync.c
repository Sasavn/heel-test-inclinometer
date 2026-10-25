#include "tab5_time_sync.h"
#include <string.h>
#define SYNC_BUDGET_US 8000000LL
#define NTP_UNIX_DELTA 2208988800LL
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static bool ntp_epoch(const uint8_t *p,int64_t *epoch){
    uint32_t seconds=be32(p);
    /* Exactly one era maps into the supported 2000..2099 UTC interval.
     * Era 0 until 2036-02-07; era 1 thereafter. Never use signed 32-bit time. */
    int64_t v=(int64_t)seconds-NTP_UNIX_DELTA;
    if(v<946684800LL)v+=4294967296LL;
    if(v<946684800LL||v>=4102444800LL)return false;
    *epoch=v;return true;
}
tab5_time_sync_result_t tab5_time_sync_fetch(const tab5_time_sync_transport_t *t,bool sta){
    tab5_time_sync_result_t r={.code=TAB5_TIME_SYNC_NO_STA};
    if(!sta)return r;
    r.code=TAB5_TIME_SYNC_RESOURCE;
    if(!t||!t->now_us||!t->nonce||!t->exchange)return r;
    uint8_t request[48]={0},response[512];request[0]=0x23; /* v4 client */
    if(!t->nonce(t->ctx,request+40))return r;
    /* A nonzero randomized originate token also bootstraps an unset clock. */
    request[40]|=1;
    int64_t start=t->now_us(t->ctx),received=0;size_t len=0;
    r.code=t->exchange(t->ctx,request,response,sizeof(response),&len,start+SYNC_BUDGET_US,&received);
    if(r.code!=TAB5_TIME_SYNC_OK)return r;
    int64_t finish=t->now_us(t->ctx);
    if(finish-start>SYNC_BUDGET_US||received<start||received>finish){r.code=TAB5_TIME_SYNC_TIMEOUT;return r;}
    r.code=TAB5_TIME_SYNC_PACKET;
    if(len<48||len>sizeof(response)||(response[0]>>6)==3||
       (((response[0]>>3)&7)!=3&&((response[0]>>3)&7)!=4)||
       (response[0]&7)!=4||response[1]==0||response[1]>15||memcmp(response+24,request+40,8))return r;
    uint8_t zero[16]={0};if(!memcmp(response+32,zero,16))return r;
    int64_t tx,rx;
    if(!ntp_epoch(response+40,&tx)||!ntp_epoch(response+32,&rx)){r.code=TAB5_TIME_SYNC_RANGE;return r;}
    if(tx<rx||tx-rx>8)return r;
    r.code=TAB5_TIME_SYNC_OK;r.utc_seconds=tx;r.received_us=received;return r;
}
const char *tab5_time_sync_message(tab5_time_sync_code_t c){
    switch(c){
    case TAB5_TIME_SYNC_OK:return "NTP: получено время UTC";
    case TAB5_TIME_SYNC_NO_STA:return "NTP: подключите Wi-Fi STA; одной точки AP недостаточно";
    case TAB5_TIME_SYNC_BUSY:return "NTP: синхронизация уже выполняется";
    case TAB5_TIME_SYNC_DNS:return "NTP: не удалось разрешить pool.ntp.org";
    case TAB5_TIME_SYNC_TIMEOUT:return "NTP: истекло время ожидания (8 с)";
    case TAB5_TIME_SYNC_IO:return "NTP: ошибка сети";
    case TAB5_TIME_SYNC_PACKET:return "NTP: некорректный или несинхронный ответ";
    case TAB5_TIME_SYNC_RANGE:return "NTP: дата вне диапазона 2000–2099";
    default:return "NTP: недостаточно ресурсов";
    }
}
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"
#include "lwip/sockets.h"
#include <stdlib.h>
#include <errno.h>
typedef struct {bool ok;ip_addr_t address;} dns_answer_t;
typedef struct {QueueHandle_t queue;unsigned refs;} dns_context_t;
static void dns_release(dns_context_t *c){if(__atomic_sub_fetch(&c->refs,1,__ATOMIC_ACQ_REL)==0){vQueueDelete(c->queue);free(c);}}
static void dns_found(const char *name,const ip_addr_t *addr,void *ctx){
    (void)name;dns_context_t *c=ctx;dns_answer_t a={.ok=addr&&IP_IS_V4(addr)};
    if(a.ok)a.address=*addr;
    xQueueSend(c->queue,&a,0);dns_release(c);
}
static void dns_begin(void *ctx){
    dns_context_t *c=ctx;ip_addr_t address;
    err_t e=dns_gethostbyname_addrtype("pool.ntp.org",&address,dns_found,c,LWIP_DNS_ADDRTYPE_IPV4);
    if(e==ERR_OK)dns_found(NULL,&address,c);
    else if(e!=ERR_INPROGRESS)dns_found(NULL,NULL,c);
}
static int64_t transport_now(void *ctx){(void)ctx;return esp_timer_get_time();}
static bool transport_nonce(void *ctx,uint8_t out[8]){(void)ctx;esp_fill_random(out,8);return true;}
static TickType_t wait_ticks(int64_t us){TickType_t n=pdMS_TO_TICKS((us+999)/1000);return n?n:1;}
static tab5_time_sync_code_t udp_exchange(void *unused,const uint8_t request[48],uint8_t *response,
        size_t capacity,size_t *length,int64_t deadline,int64_t *received){
    (void)unused;
    dns_context_t *c=calloc(1,sizeof(*c));if(!c)return TAB5_TIME_SYNC_RESOURCE;
    c->queue=xQueueCreate(1,sizeof(dns_answer_t));if(!c->queue){free(c);return TAB5_TIME_SYNC_RESOURCE;}
    c->refs=2;
    /* Only TCPIP thread uses raw DNS API. Context is heap-owned with two refs:
     * worker and resolver. A late callback after timeout cannot touch a stack
     * or deleted queue. At most lwIP's bounded DNS table can retain callbacks. */
    if(tcpip_try_callback(dns_begin,c)!=ERR_OK){dns_release(c);dns_release(c);return TAB5_TIME_SYNC_DNS;}
    dns_answer_t a;int64_t remain=deadline-esp_timer_get_time();
    if(remain>4000000)remain=4000000;
    bool got=remain>0&&xQueueReceive(c->queue,&a,wait_ticks(remain))==pdTRUE;
    dns_release(c);
    if(!got)return TAB5_TIME_SYNC_TIMEOUT;
    if(!a.ok)return TAB5_TIME_SYNC_DNS;
    remain=deadline-esp_timer_get_time();if(remain<=0)return TAB5_TIME_SYNC_TIMEOUT;
    int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(fd<0)return TAB5_TIME_SYNC_IO;
    struct timeval timeout={.tv_sec=(long)(remain/1000000),.tv_usec=(long)(remain%1000000)};
    struct sockaddr_in remote={.sin_family=AF_INET,.sin_port=htons(123)};
    remote.sin_addr.s_addr=ip4_addr_get_u32(ip_2_ip4(&a.address));
    tab5_time_sync_code_t result=TAB5_TIME_SYNC_IO;
    if(setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))!=0)goto done;
    if(connect(fd,(struct sockaddr*)&remote,sizeof(remote))!=0)goto done;
    if(send(fd,request,48,0)!=48)goto done;
    remain=deadline-esp_timer_get_time();if(remain<=0){result=TAB5_TIME_SYNC_TIMEOUT;goto done;}
    timeout=(struct timeval){.tv_sec=(long)(remain/1000000),.tv_usec=(long)(remain%1000000)};
    if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))!=0)goto done;
    int n=recv(fd,response,capacity,0);*received=esp_timer_get_time();
    if(n<0){result=(errno==EAGAIN||errno==EWOULDBLOCK||errno==ETIMEDOUT)?TAB5_TIME_SYNC_TIMEOUT:TAB5_TIME_SYNC_IO;goto done;}
    *length=(size_t)n;result=TAB5_TIME_SYNC_OK;
done:
    close(fd);return result;
}
static QueueHandle_t completion;
static bool in_flight;
static void sync_worker(void *ctx){
    (void)ctx;tab5_time_sync_transport_t t={.now_us=transport_now,.nonce=transport_nonce,.exchange=udp_exchange};
    tab5_time_sync_result_t r=tab5_time_sync_fetch(&t,true);
    xQueueSend(completion,&r,0);vTaskDelete(NULL);
}
bool tab5_time_sync_start(bool sta){
    if(!sta||in_flight)return false;
    if(!completion)completion=xQueueCreate(1,sizeof(tab5_time_sync_result_t));
    if(!completion)return false;
    in_flight=true;
    if(xTaskCreate(sync_worker,"time_ntp",4096,NULL,3,NULL)!=pdPASS){in_flight=false;return false;}
    return true;
}
bool tab5_time_sync_busy(void){return in_flight;}
bool tab5_time_sync_poll(tab5_time_sync_result_t *out){
    if(!out||!in_flight||xQueueReceive(completion,out,0)!=pdTRUE)return false;
    in_flight=false;return true;
}
#endif
