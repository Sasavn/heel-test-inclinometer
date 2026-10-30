#include "tab5_backlight.h"
bool tab5_backlight_timeout_valid(uint32_t s){return s==0||s==300||s==1800||s==3600;}
void tab5_backlight_init(tab5_backlight_t *p,uint64_t now){*p=(tab5_backlight_t){.last_ms=now};}
bool tab5_backlight_configure(tab5_backlight_t *p,uint32_t s,uint64_t now){
 if(!tab5_backlight_timeout_valid(s))return false;
 if(now<p->last_ms)now=p->last_ms;
 p->timeout_s=s;p->last_ms=now;p->asleep=false;return true;
}
bool tab5_backlight_step(tab5_backlight_t *p,uint64_t now,tab5_bl_event_t event){
 if(now<p->last_ms)now=p->last_ms;
 if(!p->touch&&p->timeout_s&&now-p->last_ms>=(uint64_t)p->timeout_s*1000)p->asleep=true;
 if(event==TAB5_BL_IDLE)return false;
 if(event==TAB5_BL_TOUCH_UP&&!p->touch)return false;
 if(event==TAB5_BL_TOUCH_UP){bool consume=p->swallow;p->touch=false;p->swallow=false;p->last_ms=now;return consume;}
 if(event==TAB5_BL_TOUCH_DOWN&&p->touch){p->last_ms=now;return p->swallow;}
 bool consume=p->asleep;p->asleep=false;p->last_ms=now;
 if(event==TAB5_BL_TOUCH_DOWN){p->touch=true;p->swallow=consume;}
 return consume;
}
