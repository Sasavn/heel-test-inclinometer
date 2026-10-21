/* Optional integration bridge: production firmware parser + test flash boundary.
   Built only by run-service-integration.cmd; no device or GUI access. */
#define TAB5_SERVICES_HOST 1
#include "../../tab5/main/tab5_services.c"
#undef fwrite
#undef fsync
static size_t test_write(const void *p,size_t n,size_t count,FILE *f){return fwrite(p,n,count,f);}
static int test_sync(int fd){return _commit(fd);}
__declspec(dllexport) int bridge_init(void){tab5_services_config_t c={0};int e=tab5_services_init(&c);return e?e:tab5_services_confirm_boot();}
__declspec(dllexport) void bridge_line(const char *s,tab5_service_reply_fn fn,void *ctx){tab5_services_line(1,s,true,fn,ctx);tab5_services_response_flushed(1);}
__declspec(dllexport) unsigned bridge_tick(void){test_now+=10;maintenance();return test_restarted;}
__declspec(dllexport) void bridge_boot(void){memset(&ota,0,sizeof(ota));tab5_services_confirm_boot();}
