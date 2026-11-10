#include "../tab5/main/tab5_backlight.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 tab5_backlight_t p;tab5_backlight_init(&p,0);
 tab5_backlight_step(&p,99999999,TAB5_BL_IDLE);assert(!p.asleep);
 uint32_t choices[]={300,1800,3600};
 for(unsigned i=0;i<3;i++){
 tab5_backlight_init(&p,0);assert(tab5_backlight_configure(&p,choices[i],0));
 uint64_t end=(uint64_t)choices[i]*1000;
 tab5_backlight_step(&p,end-1,TAB5_BL_IDLE);assert(!p.asleep);
 tab5_backlight_step(&p,end,TAB5_BL_IDLE);assert(p.asleep);
 assert(tab5_backlight_step(&p,end,TAB5_BL_TOUCH_DOWN));assert(!p.asleep);
 assert(tab5_backlight_step(&p,end+1,TAB5_BL_TOUCH_DOWN));
 assert(tab5_backlight_step(&p,end+2,TAB5_BL_TOUCH_UP));
 assert(!tab5_backlight_step(&p,end+3,TAB5_BL_PRESS));
 tab5_backlight_step(&p,2*end+3,TAB5_BL_IDLE);assert(p.asleep);
 assert(tab5_backlight_step(&p,2*end+4,TAB5_BL_ROTATE));
 assert(!tab5_backlight_step(&p,2*end+5,TAB5_BL_ROTATE));
 tab5_backlight_step(&p,3*end+5,TAB5_BL_IDLE);assert(tab5_backlight_step(&p,3*end+6,TAB5_BL_PRESS));
 assert(!tab5_backlight_configure(&p,1,0));
 assert(tab5_backlight_configure(&p,0,3*end+7));assert(!p.asleep);
 }
 puts("backlight policy OK");return 0;
}
