/* Independent retained renderer, written for the Tab5 UI contract. */
#include "native_ui.h"
#include "native_gradient.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#define PI 3.14159265358979323846
#define MAX_DIRTY 48
#define MAX_KEYS 96
#if NUI_PROFILE_FINE_COUNTERS
#define FINE_COUNT(statement) do { statement; } while(0)
#else
#define FINE_COUNT(statement) do { } while(0)
#endif
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
struct nui_style_entry {nui_style_prop_t prop;nui_style_selector_t selector;nui_style_value_t value;struct nui_style_entry *next;};
typedef struct style_ref {const nui_style_t *style;nui_style_selector_t selector;struct style_ref *next;} style_ref;
typedef struct handler {nui_event_cb_t cb;nui_event_code_t code;void *user;struct handler *next;} handler;
struct nui_obj {
    uint64_t coords_epoch;uint64_t coords_style_epoch;nui_area_t coords_cache;
    uint64_t cache_epoch;uint32_t cache_state,cache_part;uint64_t cache_valid;nui_style_value_t main_cache[34];
    const nui_obj_class_t *class_p;struct nui_obj *parent,*first,*last,*next,*prev,*garbage_next;
    int32_t x,y,w,h,ext;bool center_x,center_y,deleted;uint32_t flags,state;
    nui_style_entry_t *local;style_ref *styles;handler *handlers;
    char *text,*accepted,*display_text;uint32_t max_length,cursor;bool password,one_line;int long_mode;
    uint32_t text_epoch,blink_epoch,key_repeat_due;bool key_on_press;
    int key_mode,key_pressed;const char *const *maps[4];const nui_buttonmatrix_ctrl_t *ctrl[4];nui_obj_t *textarea;
    const nui_image_dsc_t *image;int32_t bar_min,bar_max,bar_value;
    uint32_t point_count;int32_t ymin,ymax;uint8_t hdiv,vdiv;nui_chart_series_t *series;
};
struct nui_timer {uint32_t period,due;void (*cb)(nui_timer_t*);void *user;bool deleted;struct nui_timer *next;};
const nui_obj_class_t nui_obj_class={0},nui_label_class={1},nui_button_class={2},nui_switch_class={3},nui_textarea_class={4},nui_keyboard_class={5},nui_chart_class={6},nui_image_class={7},nui_bar_class={8};
static struct {
    int width,height;uint16_t *fb;bool own_fb;uint32_t ticks;unsigned props,event_depth;bool rendering;
    nui_obj_t *root,*capture,*focus,*garbage;nui_indev_t input;nui_timer_t *timers;
    nui_area_t dirty[MAX_DIRTY],rendered[MAX_DIRTY];size_t dirty_count,rendered_count;
    nui_native_flush_cb_t flush;void *flush_user;uint64_t (*clock_us)(void);nui_native_stats_t stats;
} n;
typedef union {max_align_t alignment;size_t size;} allocation_header;
static void *mem_alloc(size_t size){allocation_header *h=calloc(1,sizeof(*h)+size);if(!h){n.stats.allocation_failures++;return NULL;}h->size=size;n.stats.allocation_bytes+=size;n.stats.allocation_count++;if(n.stats.allocation_bytes>n.stats.allocation_peak)n.stats.allocation_peak=n.stats.allocation_bytes;return h+1;}
static void mem_free(void *p){if(p){allocation_header *h=(allocation_header*)p-1;n.stats.allocation_bytes-=h->size;n.stats.allocation_count--;free(h);}}
static char *str_copy(const char *s){if(!s)s="";size_t len=strlen(s)+1;char *r=mem_alloc(len);if(r)memcpy(r,s,len);return r;}
static uint64_t now_us(void){
    if(n.clock_us)return n.clock_us();
#ifdef _WIN32
    LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (uint64_t)(t.QuadPart/(f.QuadPart/1000000.0));
#elif defined(TIME_UTC)
    struct timespec t;timespec_get(&t,TIME_UTC);return (uint64_t)t.tv_sec*1000000u+t.tv_nsec/1000;
#else
    return (uint64_t)clock()*1000000u/CLOCKS_PER_SEC;
#endif
}
static nui_obj_t *draw_source;
static void event_send(nui_obj_t*,nui_event_code_t,void*,nui_layer_t*);
static uint64_t profile_begin(nui_primitive_t type){
    FINE_COUNT(n.stats.primitive[type].calls++);
    if(draw_source&&(draw_source->flags&NUI_OBJ_FLAG_SEND_DRAW_TASK_EVENTS)){
        static const nui_draw_task_type_t types[]={NUI_DRAW_TASK_TYPE_FILL,NUI_DRAW_TASK_TYPE_BORDER,NUI_DRAW_TASK_TYPE_LABEL,NUI_DRAW_TASK_TYPE_IMAGE,NUI_DRAW_TASK_TYPE_LINE,NUI_DRAW_TASK_TYPE_ARC,NUI_DRAW_TASK_TYPE_TRIANGLE,NUI_DRAW_TASK_TYPE_BOX_SHADOW};
        nui_draw_task_t task={types[type]};event_send(draw_source,NUI_EVENT_DRAW_TASK_ADDED,&task,NULL);
    }
#if NUI_PROFILE_PRIMITIVE_TIMING
    return now_us();
#else
    return 0;
#endif
}
static void profile_end(nui_primitive_t type,uint64_t start){
#if NUI_PROFILE_PRIMITIVE_TIMING
    uint64_t end=now_us();if(end>=start)n.stats.primitive[type].us+=end-start;
#else
    (void)type;(void)start;
#endif
}
static bool area_intersect(nui_area_t *out,nui_area_t a,nui_area_t b){*out=(nui_area_t){MAX(a.x1,b.x1),MAX(a.y1,b.y1),MIN(a.x2,b.x2),MIN(a.y2,b.y2)};return out->x1<=out->x2&&out->y1<=out->y2;}
static bool area_contains(nui_area_t a,nui_area_t b){return a.x1<=b.x1&&a.y1<=b.y1&&a.x2>=b.x2&&a.y2>=b.y2;}
int32_t nui_area_get_width(const nui_area_t *a){return a->x2-a->x1+1;}
int32_t nui_area_get_height(const nui_area_t *a){return a->y2-a->y1+1;}
static uint64_t style_epoch=1,layout_epoch=1;
/* Cache payload is embedded in each retained object and already included in allocations. */
#define OBJECT_CACHE_BYTES (offsetof(nui_obj_t,class_p))
static size_t cache_objects,cache_objects_peak;
static bool inherited(nui_style_prop_t p){return p==NUI_STYLE_TEXT_COLOR||p==NUI_STYLE_TEXT_FONT||p==NUI_STYLE_TEXT_LETTER_SPACE||p==NUI_STYLE_TEXT_LINE_SPACE||p==NUI_STYLE_TEXT_ALIGN;}
static nui_style_value_t default_prop(nui_style_prop_t p){nui_style_value_t v={0};if(p==NUI_STYLE_TEXT_FONT)v.ptr=NUI_FONT_DEFAULT;else if(p==NUI_STYLE_TEXT_COLOR||p==NUI_STYLE_LINE_COLOR)v.color=0xffffff;else if(p==NUI_STYLE_OPA||p==NUI_STYLE_IMAGE_OPA||p==NUI_STYLE_LINE_OPA||p==NUI_STYLE_SHADOW_OPA||p==NUI_STYLE_OUTLINE_OPA)v.num=255;else if(p==NUI_STYLE_LINE_WIDTH)v.num=1;return v;}
static bool entry_find(nui_style_entry_t *e,nui_style_prop_t p,nui_style_selector_t sel,nui_style_value_t *out,bool exact){int score=-1;bool found=false;for(;e;e=e->next){if(e->prop!=p)continue;if(exact){if(e->selector!=sel)continue;}else if((e->selector&0xffff0000)!=(sel&0xffff0000)||(e->selector&0xffff&~sel))continue;unsigned bits=e->selector&0xffff;int s=0;while(bits){s+=bits&1;bits>>=1;}if(s>score){*out=e->value;score=s;found=true;}}return found;}
static bool prop_find(const nui_obj_t *o,nui_style_prop_t p,nui_style_selector_t part,nui_style_value_t *v){if(!o)return false;nui_style_selector_t sel=(part&0xffff0000)|o->state|(part&0xffff);if(entry_find(o->local,p,sel,v,false))return true;int best=-1;bool found=false;for(style_ref *r=o->styles;r;r=r->next){if((r->selector&0xffff0000)!=(sel&0xffff0000)||(r->selector&0xffff&~sel))continue;int score=0;unsigned bits=r->selector&0xffff;while(bits){score+=bits&1;bits>>=1;}nui_style_value_t value;if(score>best&&entry_find(r->style->entries,p,0,&value,true)){*v=value;best=score;found=true;}}if(found)return true;if(inherited(p)&&o->parent)return prop_find(o->parent,p,NUI_PART_MAIN,v);return false;}
nui_style_value_t nui_obj_get_style_prop(const nui_obj_t *o,nui_style_selector_t part,nui_style_prop_t p){
    nui_obj_t *mutable=(nui_obj_t*)o;
    if(o&&p>0&&p<34){
        if(o->cache_epoch!=style_epoch||o->cache_state!=o->state||o->cache_part!=part){if(o->cache_valid)FINE_COUNT(n.stats.style_cache_evictions++);mutable->cache_epoch=style_epoch;mutable->cache_state=o->state;mutable->cache_part=part;mutable->cache_valid=0;}
        if(o->cache_valid&(UINT64_C(1)<<p)){FINE_COUNT(n.stats.style_cache_hits++);return o->main_cache[p];}
        nui_style_value_t v;if(!prop_find(o,p,part,&v))v=default_prop(p);
        mutable->main_cache[p]=v;mutable->cache_valid|=UINT64_C(1)<<p;FINE_COUNT(n.stats.style_cache_misses++);return v;
    }
    nui_style_value_t v;if(!prop_find(o,p,part,&v))v=default_prop(p);return v;
}
static int32_t num(const nui_obj_t *o,nui_style_prop_t p,unsigned part){return nui_obj_get_style_prop(o,part,p).num;}
static nui_color_t color(const nui_obj_t *o,nui_style_prop_t p,unsigned part){return nui_obj_get_style_prop(o,part,p).color;}
static const nui_font_t *font(const nui_obj_t *o,unsigned part){const nui_font_t *f=nui_obj_get_style_prop(o,part,NUI_STYLE_TEXT_FONT).ptr;return f?f:NUI_FONT_DEFAULT;}
static void entries_free(nui_style_entry_t *e){while(e){nui_style_entry_t *next=e->next;mem_free(e);e=next;}}
static bool entry_set(nui_style_entry_t **head,nui_style_prop_t p,nui_style_selector_t sel,nui_style_value_t v){for(nui_style_entry_t *e=*head;e;e=e->next)if(e->prop==p&&e->selector==sel){if(!memcmp(&e->value,&v,sizeof(v)))return false;e->value=v;style_epoch++;return true;}nui_style_entry_t *e=mem_alloc(sizeof(*e));if(!e)return false;*e=(nui_style_entry_t){p,sel,v,*head};*head=e;style_epoch++;return true;}
void nui_style_init(nui_style_t *s){s->entries=NULL;}
void nui_style_reset(nui_style_t *s){if(s){entries_free(s->entries);s->entries=NULL;style_epoch++;}}
void nui_style_set_prop(nui_style_t *s,nui_style_prop_t p,nui_style_value_t v){if(s)entry_set(&s->entries,p,0,v);}
nui_style_prop_t nui_style_register_prop(unsigned flags){(void)flags;return 1024+(++n.props);}
unsigned nui_style_get_num_custom_props(void){return n.props;}
int nui_obj_get_local_style_prop(const nui_obj_t *o,nui_style_prop_t p,nui_style_value_t *v,nui_style_selector_t sel){return o&&entry_find(o->local,p,sel,v,true)?NUI_STYLE_RES_FOUND:NUI_STYLE_RES_NOT_FOUND;}
void nui_obj_set_local_style_prop(nui_obj_t *o,nui_style_prop_t p,nui_style_value_t v,nui_style_selector_t sel){if(o&&!o->deleted&&entry_set(&o->local,p,sel,v)){nui_obj_invalidate(o);}}
void nui_obj_add_style(nui_obj_t *o,const nui_style_t *s,nui_style_selector_t sel){if(!o||!s)return;for(style_ref *r=o->styles;r;r=r->next)if(r->style==s&&r->selector==sel)return;style_ref *r=mem_alloc(sizeof(*r));if(r){*r=(style_ref){s,sel,o->styles};o->styles=r;style_epoch++;nui_obj_invalidate(o);}}
void nui_obj_remove_style(nui_obj_t *o,const nui_style_t *s,nui_style_selector_t sel){if(!o)return;style_ref **r=&o->styles;while(*r){if((!s||(*r)->style==s)&&(*r)->selector==sel){style_ref *dead=*r;*r=dead->next;mem_free(dead);style_epoch++;nui_obj_invalidate(o);}else r=&(*r)->next;}}
void nui_obj_remove_style_all(nui_obj_t *o){if(!o)return;entries_free(o->local);o->local=NULL;while(o->styles){style_ref *r=o->styles;o->styles=r->next;mem_free(r);}style_epoch++;nui_obj_invalidate(o);}
void nui_obj_report_style_change(const nui_style_t *s){(void)s;style_epoch++;if(n.root)nui_obj_invalidate(n.root);}
void nui_style_set_bg_color(nui_style_t *s,nui_color_t v){nui_style_value_t val={0};val.color=v;nui_style_set_prop(s,NUI_STYLE_BG_COLOR,val);}
void nui_obj_set_style_bg_color(nui_obj_t *o,nui_color_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.color=v;nui_obj_set_local_style_prop(o,NUI_STYLE_BG_COLOR,val,sel);}
nui_color_t nui_obj_get_style_bg_color(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_BG_COLOR).color;}
void nui_style_set_bg_opa(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_BG_OPA,val);}
void nui_obj_set_style_bg_opa(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_BG_OPA,val,sel);}
int32_t nui_obj_get_style_bg_opa(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_BG_OPA).num;}
void nui_style_set_bg_grad_color(nui_style_t *s,nui_color_t v){nui_style_value_t val={0};val.color=v;nui_style_set_prop(s,NUI_STYLE_BG_GRAD_COLOR,val);}
void nui_obj_set_style_bg_grad_color(nui_obj_t *o,nui_color_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.color=v;nui_obj_set_local_style_prop(o,NUI_STYLE_BG_GRAD_COLOR,val,sel);}
nui_color_t nui_obj_get_style_bg_grad_color(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_BG_GRAD_COLOR).color;}
void nui_style_set_bg_grad_dir(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_BG_GRAD_DIR,val);}
void nui_obj_set_style_bg_grad_dir(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_BG_GRAD_DIR,val,sel);}
int32_t nui_obj_get_style_bg_grad_dir(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_BG_GRAD_DIR).num;}
void nui_style_set_border_color(nui_style_t *s,nui_color_t v){nui_style_value_t val={0};val.color=v;nui_style_set_prop(s,NUI_STYLE_BORDER_COLOR,val);}
void nui_obj_set_style_border_color(nui_obj_t *o,nui_color_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.color=v;nui_obj_set_local_style_prop(o,NUI_STYLE_BORDER_COLOR,val,sel);}
nui_color_t nui_obj_get_style_border_color(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_BORDER_COLOR).color;}
void nui_style_set_border_width(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_BORDER_WIDTH,val);}
void nui_obj_set_style_border_width(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_BORDER_WIDTH,val,sel);}
int32_t nui_obj_get_style_border_width(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_BORDER_WIDTH).num;}
void nui_style_set_radius(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_RADIUS,val);}
void nui_obj_set_style_radius(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_RADIUS,val,sel);}
int32_t nui_obj_get_style_radius(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_RADIUS).num;}
void nui_style_set_pad_top(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_PAD_TOP,val);}
void nui_obj_set_style_pad_top(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_PAD_TOP,val,sel);}
int32_t nui_obj_get_style_pad_top(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_PAD_TOP).num;}
void nui_style_set_pad_bottom(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_PAD_BOTTOM,val);}
void nui_obj_set_style_pad_bottom(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_PAD_BOTTOM,val,sel);}
int32_t nui_obj_get_style_pad_bottom(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_PAD_BOTTOM).num;}
void nui_style_set_pad_left(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_PAD_LEFT,val);}
void nui_obj_set_style_pad_left(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_PAD_LEFT,val,sel);}
int32_t nui_obj_get_style_pad_left(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_PAD_LEFT).num;}
void nui_style_set_pad_right(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_PAD_RIGHT,val);}
void nui_obj_set_style_pad_right(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_PAD_RIGHT,val,sel);}
int32_t nui_obj_get_style_pad_right(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_PAD_RIGHT).num;}
void nui_style_set_pad_row(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_PAD_ROW,val);}
void nui_obj_set_style_pad_row(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_PAD_ROW,val,sel);}
int32_t nui_obj_get_style_pad_row(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_PAD_ROW).num;}
void nui_style_set_pad_column(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_PAD_COLUMN,val);}
void nui_obj_set_style_pad_column(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_PAD_COLUMN,val,sel);}
int32_t nui_obj_get_style_pad_column(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_PAD_COLUMN).num;}
void nui_style_set_text_color(nui_style_t *s,nui_color_t v){nui_style_value_t val={0};val.color=v;nui_style_set_prop(s,NUI_STYLE_TEXT_COLOR,val);}
void nui_obj_set_style_text_color(nui_obj_t *o,nui_color_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.color=v;nui_obj_set_local_style_prop(o,NUI_STYLE_TEXT_COLOR,val,sel);}
nui_color_t nui_obj_get_style_text_color(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_TEXT_COLOR).color;}
void nui_style_set_text_font(nui_style_t *s,const nui_font_t* v){nui_style_value_t val={0};val.ptr=v;nui_style_set_prop(s,NUI_STYLE_TEXT_FONT,val);}
void nui_obj_set_style_text_font(nui_obj_t *o,const nui_font_t* v,nui_style_selector_t sel){nui_style_value_t val={0};val.ptr=v;nui_obj_set_local_style_prop(o,NUI_STYLE_TEXT_FONT,val,sel);}
const nui_font_t* nui_obj_get_style_text_font(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_TEXT_FONT).ptr;}
void nui_style_set_text_letter_space(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_TEXT_LETTER_SPACE,val);}
void nui_obj_set_style_text_letter_space(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_TEXT_LETTER_SPACE,val,sel);}
int32_t nui_obj_get_style_text_letter_space(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_TEXT_LETTER_SPACE).num;}
void nui_style_set_text_align(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_TEXT_ALIGN,val);}
void nui_obj_set_style_text_align(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_TEXT_ALIGN,val,sel);}
int32_t nui_obj_get_style_text_align(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_TEXT_ALIGN).num;}
void nui_style_set_text_line_space(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_TEXT_LINE_SPACE,val);}
void nui_obj_set_style_text_line_space(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_TEXT_LINE_SPACE,val,sel);}
int32_t nui_obj_get_style_text_line_space(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_TEXT_LINE_SPACE).num;}
void nui_style_set_shadow_color(nui_style_t *s,nui_color_t v){nui_style_value_t val={0};val.color=v;nui_style_set_prop(s,NUI_STYLE_SHADOW_COLOR,val);}
void nui_obj_set_style_shadow_color(nui_obj_t *o,nui_color_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.color=v;nui_obj_set_local_style_prop(o,NUI_STYLE_SHADOW_COLOR,val,sel);}
nui_color_t nui_obj_get_style_shadow_color(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_SHADOW_COLOR).color;}
void nui_style_set_shadow_width(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_SHADOW_WIDTH,val);}
void nui_obj_set_style_shadow_width(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_SHADOW_WIDTH,val,sel);}
int32_t nui_obj_get_style_shadow_width(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_SHADOW_WIDTH).num;}
void nui_style_set_shadow_offset_y(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_SHADOW_OFFSET_Y,val);}
void nui_obj_set_style_shadow_offset_y(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_SHADOW_OFFSET_Y,val,sel);}
int32_t nui_obj_get_style_shadow_offset_y(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_SHADOW_OFFSET_Y).num;}
void nui_style_set_shadow_opa(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_SHADOW_OPA,val);}
void nui_obj_set_style_shadow_opa(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_SHADOW_OPA,val,sel);}
int32_t nui_obj_get_style_shadow_opa(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_SHADOW_OPA).num;}
void nui_style_set_opa(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_OPA,val);}
void nui_obj_set_style_opa(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_OPA,val,sel);}
int32_t nui_obj_get_style_opa(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_OPA).num;}
void nui_style_set_line_color(nui_style_t *s,nui_color_t v){nui_style_value_t val={0};val.color=v;nui_style_set_prop(s,NUI_STYLE_LINE_COLOR,val);}
void nui_obj_set_style_line_color(nui_obj_t *o,nui_color_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.color=v;nui_obj_set_local_style_prop(o,NUI_STYLE_LINE_COLOR,val,sel);}
nui_color_t nui_obj_get_style_line_color(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_LINE_COLOR).color;}
void nui_style_set_line_width(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_LINE_WIDTH,val);}
void nui_obj_set_style_line_width(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_LINE_WIDTH,val,sel);}
int32_t nui_obj_get_style_line_width(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_LINE_WIDTH).num;}
void nui_style_set_line_opa(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_LINE_OPA,val);}
void nui_obj_set_style_line_opa(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_LINE_OPA,val,sel);}
int32_t nui_obj_get_style_line_opa(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_LINE_OPA).num;}
void nui_style_set_outline_color(nui_style_t *s,nui_color_t v){nui_style_value_t val={0};val.color=v;nui_style_set_prop(s,NUI_STYLE_OUTLINE_COLOR,val);}
void nui_obj_set_style_outline_color(nui_obj_t *o,nui_color_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.color=v;nui_obj_set_local_style_prop(o,NUI_STYLE_OUTLINE_COLOR,val,sel);}
nui_color_t nui_obj_get_style_outline_color(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_OUTLINE_COLOR).color;}
void nui_style_set_outline_width(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_OUTLINE_WIDTH,val);}
void nui_obj_set_style_outline_width(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_OUTLINE_WIDTH,val,sel);}
int32_t nui_obj_get_style_outline_width(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_OUTLINE_WIDTH).num;}
void nui_style_set_outline_opa(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_OUTLINE_OPA,val);}
void nui_obj_set_style_outline_opa(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_OUTLINE_OPA,val,sel);}
int32_t nui_obj_get_style_outline_opa(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_OUTLINE_OPA).num;}
void nui_style_set_outline_pad(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_OUTLINE_PAD,val);}
void nui_obj_set_style_outline_pad(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_OUTLINE_PAD,val,sel);}
int32_t nui_obj_get_style_outline_pad(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_OUTLINE_PAD).num;}
void nui_style_set_width(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_WIDTH,val);}
void nui_obj_set_style_width(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_WIDTH,val,sel);}
int32_t nui_obj_get_style_width(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_WIDTH).num;}
void nui_style_set_height(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_HEIGHT,val);}
void nui_obj_set_style_height(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_HEIGHT,val,sel);}
int32_t nui_obj_get_style_height(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_HEIGHT).num;}
void nui_style_set_image_opa(nui_style_t *s,int32_t v){nui_style_value_t val={0};val.num=v;nui_style_set_prop(s,NUI_STYLE_IMAGE_OPA,val);}
void nui_obj_set_style_image_opa(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_style_value_t val={0};val.num=v;nui_obj_set_local_style_prop(o,NUI_STYLE_IMAGE_OPA,val,sel);}
int32_t nui_obj_get_style_image_opa(const nui_obj_t *o,nui_style_selector_t sel){return nui_obj_get_style_prop(o,sel,NUI_STYLE_IMAGE_OPA).num;}

void nui_style_set_pad_all(nui_style_t *s,int32_t v){nui_style_set_pad_top(s,v);nui_style_set_pad_bottom(s,v);nui_style_set_pad_left(s,v);nui_style_set_pad_right(s,v);}
void nui_style_set_pad_hor(nui_style_t *s,int32_t v){nui_style_set_pad_left(s,v);nui_style_set_pad_right(s,v);}
void nui_style_set_pad_ver(nui_style_t *s,int32_t v){nui_style_set_pad_top(s,v);nui_style_set_pad_bottom(s,v);}
void nui_obj_set_style_pad_all(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_obj_set_style_pad_top(o,v,sel);nui_obj_set_style_pad_bottom(o,v,sel);nui_obj_set_style_pad_left(o,v,sel);nui_obj_set_style_pad_right(o,v,sel);}
void nui_obj_set_style_pad_hor(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_obj_set_style_pad_left(o,v,sel);nui_obj_set_style_pad_right(o,v,sel);}
void nui_obj_set_style_pad_ver(nui_obj_t *o,int32_t v,nui_style_selector_t sel){nui_obj_set_style_pad_top(o,v,sel);nui_obj_set_style_pad_bottom(o,v,sel);}
void nui_obj_set_style_size(nui_obj_t *o,int32_t w,int32_t h,nui_style_selector_t sel){nui_obj_set_style_width(o,w,sel);nui_obj_set_style_height(o,h,sel);}
nui_color_t nui_color_hex(uint32_t c){return c&0xffffff;}
nui_color_t nui_color_white(void){return 0xffffff;}
bool nui_color_eq(nui_color_t a,nui_color_t b){return a==b;}
uint32_t nui_color_to_u32(nui_color_t c){return c;}
uint16_t nui_color_to_u16(nui_color_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
nui_color_t nui_color_mix(nui_color_t a,nui_color_t b,uint8_t mix){unsigned r=(((a>>16)&255)*mix+((b>>16)&255)*(255-mix)+127)/255,g=(((a>>8)&255)*mix+((b>>8)&255)*(255-mix)+127)/255,bl=((a&255)*mix+(b&255)*(255-mix)+127)/255;return (r<<16)|(g<<8)|bl;}
uint32_t nui_text_utf8_next(const char *s,uint32_t *index){uint32_t i=*index,c=(uint8_t)s[i];if(!c)return 0;i++;if(c>=0xc2&&c<=0xf4){unsigned len=c<0xe0?1:c<0xf0?2:3;c&=len==1?31:len==2?15:7;for(unsigned j=0;j<len;j++){unsigned b=(uint8_t)s[i];if((b&0xc0)!=0x80){c=0xfffd;break;}c=(c<<6)|(b&63);i++;}}else if(c>=0x80)c=0xfffd;*index=i;return c;}
static uint32_t glyph_id(const nui_font_t *f,uint32_t cp){const nui_font_fmt_txt_dsc_t *d=f?f->dsc:NULL;if(!d)return 0;for(unsigned i=0;i<d->cmap_num;i++){const nui_font_fmt_txt_cmap_t *m=&d->cmaps[i];if(cp<m->range_start||cp-m->range_start>=m->range_length)continue;uint32_t off=cp-m->range_start;if(m->type==NUI_FONT_FMT_TXT_CMAP_FORMAT0_TINY)return m->glyph_id_start+off;if(m->type==NUI_FONT_FMT_TXT_CMAP_FORMAT0_FULL)return m->glyph_id_start+((const uint8_t*)m->glyph_id_ofs_list)[off];unsigned lo=0,hi=m->list_length;while(lo<hi){unsigned mid=(lo+hi)/2;if(m->unicode_list[mid]<off)lo=mid+1;else hi=mid;}if(lo<m->list_length&&m->unicode_list[lo]==off)return m->glyph_id_start+(m->type==NUI_FONT_FMT_TXT_CMAP_SPARSE_TINY?lo:((const uint16_t*)m->glyph_id_ofs_list)[lo]);}return 0;}
bool nui_font_get_glyph_dsc_fmt_txt(const nui_font_t *f,nui_font_glyph_dsc_t *out,uint32_t cp,uint32_t next){uint32_t gid=glyph_id(f,cp);if(!gid)return false;const nui_font_fmt_txt_dsc_t *d=f->dsc;const nui_font_fmt_txt_glyph_dsc_t *g=&d->glyph_dsc[gid];int adv=g->adv_w;if(d->kern_classes&&d->kern_dsc&&next){const nui_font_fmt_txt_kern_classes_t *k=d->kern_dsc;unsigned ng=glyph_id(f,next),lc=k->left_class_mapping[gid],rc=ng?k->right_class_mapping[ng]:0;if(lc&&rc)adv+=(k->class_pair_values[(lc-1)*k->right_class_cnt+rc-1]*d->kern_scale)>>4;}*out=(nui_font_glyph_dsc_t){.adv_w=(uint32_t)MAX(0,(adv+8)>>4),.box_w=g->box_w,.box_h=g->box_h,.ofs_x=g->ofs_x,.ofs_y=g->ofs_y,.bpp=d->bpp,.gid=gid,.resolved_font=f};return true;}
const uint8_t *nui_font_get_bitmap_fmt_txt(const nui_font_t *f,uint32_t cp){uint32_t gid=glyph_id(f,cp);const nui_font_fmt_txt_dsc_t *d=f?f->dsc:NULL;return gid&&d?d->glyph_bitmap+d->glyph_dsc[gid].bitmap_index:NULL;}
bool nui_font_get_glyph_dsc(const nui_font_t *f,nui_font_glyph_dsc_t *d,uint32_t cp,uint32_t next){for(unsigned depth=0;f&&depth<8;depth++,f=f->fallback)if(f->get_glyph_dsc&&f->get_glyph_dsc(f,d,cp,next)){if(!d->resolved_font)d->resolved_font=f;return true;}memset(d,0,sizeof(*d));return false;}
static int glyph_advance(const nui_font_t *f,uint32_t cp,uint32_t next){nui_font_glyph_dsc_t d;return nui_font_get_glyph_dsc(f,&d,cp,next)?(int)d.adv_w:0;}
void nui_text_get_size(nui_point_t *out,const char *text,const nui_font_t *f,int32_t letter,int32_t line,int32_t maxw,unsigned flags){if(!out)return;if(!text)text="";if(!f)f=NUI_FONT_DEFAULT;int x=0,w=0,rows=1;bool any=false;uint32_t i=0;while(text[i]){uint32_t cp=nui_text_utf8_next(text,&i),peek=i,next=nui_text_utf8_next(text,&peek);if(cp=='\n'){w=MAX(w,MAX(0,x-letter));x=0;rows++;any=false;continue;}int adv=glyph_advance(f,cp,next);if(!(flags&NUI_TEXT_FLAG_EXPAND)&&maxw>0&&maxw!=NUI_COORD_MAX&&any&&x+adv>maxw){w=MAX(w,MAX(0,x-letter));x=0;rows++;}x+=adv+letter;any=true;}out->x=MAX(w,MAX(0,x-(any?letter:0)));out->y=rows*f->line_height+(rows-1)*line;}

static bool is_kind(const nui_obj_t *o,unsigned kind){return o&&!o->deleted&&o->class_p->kind==kind;}
static void content_size(const nui_obj_t *o,int *w,int *h){nui_point_t size={0,0};if(is_kind(o,1)||is_kind(o,4)){nui_text_get_size(&size,o->text?o->text:"",font(o,0),num(o,NUI_STYLE_TEXT_LETTER_SPACE,0),num(o,NUI_STYLE_TEXT_LINE_SPACE,0),o->w>0?MAX(1,o->w-num(o,NUI_STYLE_PAD_LEFT,0)-num(o,NUI_STYLE_PAD_RIGHT,0)):NUI_COORD_MAX,(o->w==NUI_SIZE_CONTENT||o->one_line)?NUI_TEXT_FLAG_EXPAND:0);}else if(is_kind(o,7)&&o->image){size.x=o->image->header.w;size.y=o->image->header.h;}else{for(nui_obj_t *c=o->first;c;c=c->next){size.x=MAX(size.x,c->x+nui_obj_get_width(c));size.y=MAX(size.y,c->y+nui_obj_get_height(c));}}*w=size.x+num(o,NUI_STYLE_PAD_LEFT,0)+num(o,NUI_STYLE_PAD_RIGHT,0)+2*num(o,NUI_STYLE_BORDER_WIDTH,0);*h=size.y+num(o,NUI_STYLE_PAD_TOP,0)+num(o,NUI_STYLE_PAD_BOTTOM,0)+2*num(o,NUI_STYLE_BORDER_WIDTH,0);}
int32_t nui_obj_get_width(const nui_obj_t *o){if(!o||o->deleted)return 0;if(o->w!=NUI_SIZE_CONTENT)return MAX(0,o->w);int w,h;content_size(o,&w,&h);return w;}
int32_t nui_obj_get_height(const nui_obj_t *o){if(!o||o->deleted)return 0;if(o->h!=NUI_SIZE_CONTENT)return MAX(0,o->h);int w,h;content_size(o,&w,&h);return h;}
void nui_obj_get_coords(const nui_obj_t *o,nui_area_t *a){if(!a)return;if(!o||o->deleted){*a=(nui_area_t){0,0,-1,-1};return;}if(o->coords_epoch==layout_epoch&&o->coords_style_epoch==style_epoch){*a=o->coords_cache;FINE_COUNT(n.stats.layout_cache_hits++);return;}if(o->coords_epoch)FINE_COUNT(n.stats.layout_cache_evictions++);int w=nui_obj_get_width(o),h=nui_obj_get_height(o),x=o->x,y=o->y;if(o->parent){nui_area_t p;nui_obj_get_coords(o->parent,&p);int border=num(o->parent,NUI_STYLE_BORDER_WIDTH,0),left=num(o->parent,NUI_STYLE_PAD_LEFT,0),top=num(o->parent,NUI_STYLE_PAD_TOP,0);
if(o->center_x)x=(nui_obj_get_width(o->parent)-2*border-left-num(o->parent,NUI_STYLE_PAD_RIGHT,0)-w)/2;
if(o->center_y)y=(nui_obj_get_height(o->parent)-2*border-top-num(o->parent,NUI_STYLE_PAD_BOTTOM,0)-h)/2;
x+=p.x1+border+left;y+=p.y1+border+top;}*a=(nui_area_t){x,y,x+w-1,y+h-1};((nui_obj_t*)o)->coords_cache=*a;((nui_obj_t*)o)->coords_epoch=layout_epoch;((nui_obj_t*)o)->coords_style_epoch=style_epoch;FINE_COUNT(n.stats.layout_cache_misses++);}
bool nui_obj_is_visible(const nui_obj_t *o){if(!o)return false;for(;o;o=o->parent)if(o->deleted||(o->flags&NUI_OBJ_FLAG_HIDDEN))return false;return true;}
static nui_area_t object_visual(const nui_obj_t *o){nui_area_t a;nui_obj_get_coords(o,&a);int ext=MAX(num(o,NUI_STYLE_SHADOW_WIDTH,0)+abs(num(o,NUI_STYLE_SHADOW_OFFSET_Y,0)),num(o,NUI_STYLE_OUTLINE_WIDTH,0)+num(o,NUI_STYLE_OUTLINE_PAD,0));a.x1-=ext;a.y1-=ext;a.x2+=ext;a.y2+=ext;return a;}
void nui_native_invalidate(const nui_area_t *area){if(!n.fb||!area)return;nui_area_t a;if(!area_intersect(&a,*area,(nui_area_t){0,0,n.width-1,n.height-1}))return;for(size_t i=0;i<n.dirty_count;){nui_area_t existing=n.dirty[i],isect;if(area_contains(existing,a))return;if(area_intersect(&isect,a,existing)||area_contains(a,existing)){a=(nui_area_t){MIN(a.x1,existing.x1),MIN(a.y1,existing.y1),MAX(a.x2,existing.x2),MAX(a.y2,existing.y2)};n.dirty[i]=n.dirty[--n.dirty_count];i=0;}else i++;}if(n.dirty_count==MAX_DIRTY){n.dirty_count=1;n.dirty[0]=(nui_area_t){0,0,n.width-1,n.height-1};}else n.dirty[n.dirty_count++]=a;}
void nui_obj_invalidate(nui_obj_t *o){if(!nui_obj_is_visible(o))return;nui_area_t a=object_visual(o);for(nui_obj_t *p=o->parent;p;p=p->parent){nui_area_t bounds;nui_obj_get_coords(p,&bounds);if(!area_intersect(&a,a,bounds))return;}nui_native_invalidate(&a);}
static nui_obj_t *object_new(nui_obj_t *parent,const nui_obj_class_t *cls){nui_obj_t *o=mem_alloc(sizeof(*o));if(!o)return NULL;cache_objects++;if(cache_objects>cache_objects_peak)cache_objects_peak=cache_objects;o->class_p=cls;o->text_epoch=o->blink_epoch=n.ticks;o->w=100;o->h=50;o->max_length=1024;o->point_count=2;o->bar_max=100;o->ymax=100;o->key_pressed=-1;o->flags=NUI_OBJ_FLAG_CLICKABLE;if(cls==&nui_label_class||cls==&nui_image_class)o->flags=0;if(cls==&nui_label_class){o->w=o->h=NUI_SIZE_CONTENT;}if(cls==&nui_switch_class){o->w=72;o->h=40;}if(cls==&nui_keyboard_class){o->w=1280;o->h=360;}layout_epoch++;o->parent=parent;if(parent){o->prev=parent->last;if(parent->last)parent->last->next=o;else parent->first=o;parent->last=o;}nui_obj_invalidate(o);return o;}
nui_obj_t *nui_obj_create(nui_obj_t *p){return object_new(p,&nui_obj_class);}
nui_obj_t *nui_label_create(nui_obj_t *p){return object_new(p,&nui_label_class);}
nui_obj_t *nui_button_create(nui_obj_t *p){return object_new(p,&nui_button_class);}
nui_obj_t *nui_switch_create(nui_obj_t *p){return object_new(p,&nui_switch_class);}
nui_obj_t *nui_textarea_create(nui_obj_t *p){
    nui_obj_t *o=object_new(p,&nui_textarea_class);if(!o)return NULL;
    nui_obj_set_style_bg_opa(o,255,0);nui_obj_set_style_bg_color(o,0xffffff,0);
    nui_obj_set_style_border_width(o,2,0);nui_obj_set_style_border_color(o,0xE0E0E0,0);
    nui_obj_set_style_radius(o,10,0);nui_obj_set_style_pad_all(o,11,0);
    return o;
}
nui_obj_t *nui_keyboard_create(nui_obj_t *p){
    nui_obj_t *o=object_new(p,&nui_keyboard_class);if(!o)return NULL;
    nui_obj_set_style_bg_opa(o,255,0);nui_obj_set_style_bg_opa(o,255,NUI_PART_ITEMS);
    return o;
}
nui_obj_t *nui_chart_create(nui_obj_t *p){return object_new(p,&nui_chart_class);}
nui_obj_t *nui_image_create(nui_obj_t *p){return object_new(p,&nui_image_class);}
nui_obj_t *nui_bar_create(nui_obj_t *p){return object_new(p,&nui_bar_class);}
static bool descends_from(nui_obj_t *o,nui_obj_t *ancestor){for(;o;o=o->parent)if(o==ancestor)return true;return false;}
static void object_release(nui_obj_t *o){while(o->handlers){handler *h=o->handlers;o->handlers=h->next;mem_free(h);}entries_free(o->local);while(o->styles){style_ref *r=o->styles;o->styles=r->next;mem_free(r);}while(o->series){nui_chart_series_t *s=o->series;o->series=s->next;if(s->owned)mem_free(s->y_points);mem_free(s);}mem_free(o->text);mem_free(o->accepted);mem_free(o->display_text);cache_objects--;mem_free(o);}
static void garbage_collect(void){if(n.event_depth||n.rendering)return;while(n.garbage){nui_obj_t *o=n.garbage;n.garbage=o->garbage_next;object_release(o);}}
void nui_obj_delete(nui_obj_t *o){if(!o||o->deleted)return;nui_obj_invalidate(o);if(descends_from(n.capture,o))n.capture=NULL;if(descends_from(n.focus,o))n.focus=NULL;while(o->first)nui_obj_delete(o->first);if(o->parent){if(o->prev)o->prev->next=o->next;else o->parent->first=o->next;if(o->next)o->next->prev=o->prev;else o->parent->last=o->prev;}if(n.root==o)n.root=NULL;layout_epoch++;o->deleted=true;o->parent=o->next=o->prev=NULL;o->garbage_next=n.garbage;n.garbage=o;garbage_collect();}
void nui_obj_set_pos(nui_obj_t *o,int32_t x,int32_t y){if(!o||o->deleted||(o->x==x&&o->y==y&&!o->center_x&&!o->center_y))return;nui_obj_invalidate(o);layout_epoch++;o->x=x;o->y=y;o->center_x=o->center_y=false;nui_obj_invalidate(o);}
void nui_obj_set_size(nui_obj_t *o,int32_t w,int32_t h){if(!o||o->deleted||(o->w==w&&o->h==h))return;nui_obj_invalidate(o);layout_epoch++;o->w=w;o->h=h;nui_obj_invalidate(o);}
void nui_obj_set_width(nui_obj_t *o,int32_t w){if(o)nui_obj_set_size(o,w,o->h);}
void nui_obj_set_height(nui_obj_t *o,int32_t h){if(o)nui_obj_set_size(o,o->w,h);}
void nui_obj_set_x(nui_obj_t *o,int32_t x){if(o)nui_obj_set_pos(o,x,o->y);}
void nui_obj_set_y(nui_obj_t *o,int32_t y){if(o)nui_obj_set_pos(o,o->x,y);}
void nui_obj_set_align(nui_obj_t *o,int align){if(!o)return;nui_obj_invalidate(o);layout_epoch++;o->center_x=o->center_y=align==NUI_ALIGN_CENTER;nui_obj_invalidate(o);}
void nui_obj_center(nui_obj_t *o){nui_obj_set_align(o,NUI_ALIGN_CENTER);}
void nui_obj_update_layout(nui_obj_t *o){/* Geometry is resolved lazily; querying it does not dirty pixels. */if(o){nui_area_t a;nui_obj_get_coords(o,&a);}}
nui_obj_t *nui_obj_get_parent(const nui_obj_t *o){return o?o->parent:NULL;}
uint32_t nui_obj_get_child_count(const nui_obj_t *o){unsigned count=0;for(nui_obj_t *c=o?o->first:NULL;c;c=c->next)count++;return count;}
nui_obj_t *nui_obj_get_child(const nui_obj_t *o,int32_t i){if(!o)return NULL;nui_obj_t *c;if(i<0){c=o->last;for(;c&&i< -1;i++)c=c->prev;}else{c=o->first;for(;c&&i>0;i--)c=c->next;}return c;}
bool nui_obj_check_type(const nui_obj_t *o,const nui_obj_class_t *cls){return o&&!o->deleted&&o->class_p==cls;}
void nui_obj_move_foreground(nui_obj_t *o){if(!o||!o->parent||o->parent->last==o)return;nui_obj_t *p=o->parent;nui_obj_invalidate(o);if(o->prev)o->prev->next=o->next;else p->first=o->next;if(o->next)o->next->prev=o->prev;o->prev=p->last;o->next=NULL;p->last->next=o;p->last=o;nui_obj_invalidate(o);}
void nui_obj_add_flag(nui_obj_t *o,uint32_t flags){if(!o||((o->flags&flags)==flags))return;nui_obj_invalidate(o);o->flags|=flags;nui_obj_invalidate(o);}
static void reset_text_epochs(nui_obj_t *o){o->text_epoch=n.ticks;for(nui_obj_t *c=o->first;c;c=c->next)reset_text_epochs(c);}
void nui_obj_remove_flag(nui_obj_t *o,uint32_t flags){if(!o||!(o->flags&flags))return;nui_obj_invalidate(o);o->flags&=~flags;if(flags&NUI_OBJ_FLAG_HIDDEN)reset_text_epochs(o);nui_obj_invalidate(o);}
bool nui_obj_has_flag(const nui_obj_t *o,uint32_t flags){return o&&(o->flags&flags)==flags;}
static bool subtree_custom_paint(const nui_obj_t *o){for(handler *h=o->handlers;h;h=h->next)if(h->code==NUI_EVENT_DRAW_MAIN||h->code==NUI_EVENT_DRAW_POST||h->code==NUI_EVENT_ALL)return true;for(nui_obj_t *c=o->first;c;c=c->next)if(subtree_custom_paint(c))return true;return false;}
static bool pressed_paint_inert(const nui_obj_t *o,uint32_t state){
    if(state!=NUI_STATE_PRESSED||!is_kind(o,2)||(!(o->flags&NUI_OBJ_FLAG_PRESSED_PAINT_INERT)&&subtree_custom_paint(o)))return false;
    for(nui_style_entry_t *e=o->local;e;e=e->next)if(e->selector&NUI_STATE_PRESSED)return false;
    for(style_ref *r=o->styles;r;r=r->next)if(r->selector&NUI_STATE_PRESSED)return false;
    return true;
}
void nui_obj_add_state(nui_obj_t *o,uint32_t state){if(o&&(o->state&state)!=state){bool skip=pressed_paint_inert(o,state);if(!skip)nui_obj_invalidate(o);o->state|=state;if(!skip)style_epoch++;if(!skip)nui_obj_invalidate(o);else FINE_COUNT(n.stats.skipped_state_invalidations++);}}
void nui_obj_remove_state(nui_obj_t *o,uint32_t state){if(o&&(o->state&state)){bool skip=pressed_paint_inert(o,state);if(!skip)nui_obj_invalidate(o);o->state&=~state;if(!skip)style_epoch++;if(!skip)nui_obj_invalidate(o);else FINE_COUNT(n.stats.skipped_state_invalidations++);}}

bool nui_obj_has_state(const nui_obj_t *o,uint32_t state){return o&&(o->state&state)==state;}
void nui_obj_set_ext_click_area(nui_obj_t *o,int32_t v){if(o)o->ext=v;}
void nui_obj_add_event_cb(nui_obj_t *o,nui_event_cb_t cb,nui_event_code_t code,void *user){if(!o||!cb)return;handler *h=mem_alloc(sizeof(*h));if(!h)return;*h=(handler){cb,code,user,NULL};handler **tail=&o->handlers;while(*tail)tail=&(*tail)->next;*tail=h;}
static void event_send(nui_obj_t *o,nui_event_code_t code,void *param,nui_layer_t *layer){if(!o||o->deleted)return;n.event_depth++;for(handler *h=o->handlers;h&&!o->deleted;){handler *next=h->next;if(h->code==NUI_EVENT_ALL||h->code==code){nui_event_t e={o,h->user,param,layer,code};h->cb(&e);}h=next;}n.event_depth--;garbage_collect();}
void nui_obj_send_event(nui_obj_t *o,nui_event_code_t code,void *param){event_send(o,code,param,NULL);}
nui_event_code_t nui_event_get_code(const nui_event_t *e){return e?e->code:NUI_EVENT_ALL;}
nui_obj_t *nui_event_get_target(const nui_event_t *e){return e?e->target:NULL;}
void *nui_event_get_user_data(const nui_event_t *e){return e?e->user_data:NULL;}
void *nui_event_get_param(const nui_event_t *e){return e?e->param:NULL;}
nui_layer_t *nui_event_get_layer(const nui_event_t *e){return e?e->layer:NULL;}
nui_draw_task_t *nui_event_get_draw_task(const nui_event_t *e){return e?e->param:NULL;}
nui_draw_task_type_t nui_draw_task_get_type(const nui_draw_task_t *t){return t?t->type:NUI_DRAW_TASK_TYPE_NONE;}
void nui_label_set_text(nui_obj_t *o,const char *text){if(!o||o->deleted)return;if(!text)text="";if(o->text&&!strcmp(o->text,text))return;char *copy=str_copy(text);if(!copy)return;nui_obj_invalidate(o);mem_free(o->text);mem_free(o->display_text);o->display_text=NULL;o->text=copy;if(o->w==NUI_SIZE_CONTENT||o->h==NUI_SIZE_CONTENT)layout_epoch++;o->text_epoch=n.ticks;nui_obj_invalidate(o);}
const char *nui_label_get_text(const nui_obj_t *o){return o?(o->display_text?o->display_text:o->text?o->text:""):"";}
void nui_label_set_long_mode(nui_obj_t *o,int mode){if(o&&o->long_mode!=mode){o->long_mode=mode;nui_obj_invalidate(o);}}
void nui_textarea_set_text(nui_obj_t *o,const char *s){if(!o||o->deleted)return;bool changed=strcmp(nui_label_get_text(o),s?s:"")!=0;nui_label_set_text(o,s);o->cursor=(uint32_t)strlen(nui_label_get_text(o));if(changed){o->blink_epoch=n.ticks;nui_obj_send_event(o,NUI_EVENT_VALUE_CHANGED,NULL);}}
const char *nui_textarea_get_text(const nui_obj_t *o){return nui_label_get_text(o);}
void nui_textarea_set_one_line(nui_obj_t *o,bool v){if(o&&o->one_line!=v){nui_obj_invalidate(o);layout_epoch++;o->one_line=v;if(v)o->h=NUI_SIZE_CONTENT;nui_obj_invalidate(o);}}
bool nui_native_textarea_get_text_bounds(const nui_obj_t *o,nui_area_t *out){
    if(!is_kind(o,4)||!out)return false;
    nui_area_t a;nui_obj_get_coords(o,&a);int bw=num(o,NUI_STYLE_BORDER_WIDTH,0);
    *out=(nui_area_t){a.x1+bw+num(o,NUI_STYLE_PAD_LEFT,0),a.y1+bw+num(o,NUI_STYLE_PAD_TOP,0),a.x2-bw-num(o,NUI_STYLE_PAD_RIGHT,0),a.y2-bw-num(o,NUI_STYLE_PAD_BOTTOM,0)};
    if(o->one_line)out->y2=out->y1+font(o,0)->line_height-1;
    return true;
}
void nui_textarea_set_password_mode(nui_obj_t *o,bool v){if(o&&o->password!=v){o->password=v;nui_obj_invalidate(o);}}
void nui_textarea_set_max_length(nui_obj_t *o,uint32_t v){if(o)o->max_length=v;}
void nui_textarea_set_accepted_chars(nui_obj_t *o,const char *s){if(!o)return;char *copy=str_copy(s?s:"");if(copy){mem_free(o->accepted);o->accepted=copy;}}
static unsigned utf8_count(const char *s){uint32_t i=0,count=0;while(s&&s[i]){nui_text_utf8_next(s,&i);count++;}return count;}
static bool accepted_codepoint(const nui_obj_t *o,uint32_t cp){if(!o->accepted||!*o->accepted)return true;uint32_t i=0;while(o->accepted[i])if(nui_text_utf8_next(o->accepted,&i)==cp)return true;return false;}
void nui_textarea_add_text(nui_obj_t *o,const char *s){if(!o||!s)return;const char *old=nui_textarea_get_text(o);size_t len=strlen(old),add=strlen(s);char *buf=mem_alloc(len+add+1);if(!buf)return;uint32_t cursor=MIN(o->cursor,len);memcpy(buf,old,cursor);size_t used=cursor;uint32_t i=0;unsigned count=utf8_count(old);while(s[i]&&count<o->max_length){uint32_t start=i,cp=nui_text_utf8_next(s,&i);if((cp=='\n'&&o->one_line)||!accepted_codepoint(o,cp))continue;memcpy(buf+used,s+start,i-start);used+=i-start;count++;}memcpy(buf+used,old+cursor,len-cursor+1);uint32_t new_cursor=(uint32_t)used;n.event_depth++;nui_textarea_set_text(o,buf);if(!o->deleted)o->cursor=new_cursor;n.event_depth--;garbage_collect();mem_free(buf);}
void nui_textarea_delete_char(nui_obj_t *o){if(!o)return;const char *old=nui_textarea_get_text(o);size_t len=strlen(old);uint32_t cursor=MIN(o->cursor,len);if(!cursor)return;uint32_t start=cursor-1;while(start&&((uint8_t)old[start]&0xc0)==0x80)start--;char *buf=str_copy(old);if(!buf)return;memmove(buf+start,buf+cursor,len-cursor+1);n.event_depth++;nui_textarea_set_text(o,buf);if(!o->deleted)o->cursor=start;n.event_depth--;garbage_collect();mem_free(buf);}
void nui_textarea_set_cursor_pos(nui_obj_t *o,int32_t pos){if(!o)return;const char *s=nui_textarea_get_text(o);if(pos<0){o->cursor=(uint32_t)strlen(s);return;}uint32_t i=0;while(s[i]&&pos-->0)nui_text_utf8_next(s,&i);o->cursor=i;nui_obj_invalidate(o);}
void nui_image_set_src(nui_obj_t *o,const void *src){if(!o)return;nui_obj_invalidate(o);o->image=src;layout_epoch++;if(src){o->w=o->image->header.w;o->h=o->image->header.h;}nui_obj_invalidate(o);}
const void *nui_image_get_src(const nui_obj_t *o){return o?o->image:NULL;}
void nui_image_cache_drop(const void *src){/* RGB565 source is borrowed and read directly on every invalidated pass.
 * No decoded copy/cache exists to drop. */(void)src;}
void nui_bar_set_range(nui_obj_t *o,int32_t min,int32_t max){if(o&&(o->bar_min!=min||o->bar_max!=max)){o->bar_min=min;o->bar_max=max>min?max:min+1;nui_obj_invalidate(o);}}
void nui_bar_set_value(nui_obj_t *o,int32_t value,int anim){(void)anim;if(o){value=MAX(o->bar_min,MIN(o->bar_max,value));if(o->bar_value!=value){o->bar_value=value;nui_obj_invalidate(o);}}}
int32_t nui_bar_get_value(const nui_obj_t *o){return o?o->bar_value:0;}

static nui_primitive_t active_primitive;
static uint16_t blend565(uint16_t dst,uint16_t src,unsigned alpha){
    if(alpha>=255||dst==src)return src;
    if(!alpha)return dst;
    /* RGB565 coverage is quantized to 1/32; rounding only the opacity matches
     * the source framebuffer's integer channel blend, including modal veils. */
    unsigned mix=(alpha+4)>>3,inv=32-mix;
    /* Separate packed red/blue lanes cannot carry into one another: weights
     * sum to 32, so the blue numerator is at most 31*32 < 2^10. */
    unsigned rb=(((src&0xf81fu)*mix+(dst&0xf81fu)*inv)>>5)&0xf81fu;
    unsigned g=(((src&0x07e0u)*mix+(dst&0x07e0u)*inv)>>5)&0x07e0u;
    return (uint16_t)(rb|g);
}
static void pixel(nui_layer_t *l,int x,int y,uint16_t c,unsigned alpha){if(!n.fb||x<l->clip.x1||x>l->clip.x2||y<l->clip.y1||y>l->clip.y2)return;alpha=alpha*l->opa/255;if(!alpha)return;uint16_t *p=&n.fb[(size_t)y*n.width+x];*p=blend565(*p,c,alpha);FINE_COUNT(n.stats.primitive[active_primitive].pixels++);}
static void span(nui_layer_t *l,int x1,int x2,int y,uint16_t c,unsigned alpha){if(y<l->clip.y1||y>l->clip.y2)return;x1=MAX(x1,l->clip.x1);x2=MIN(x2,l->clip.x2);if(x1>x2)return;alpha=alpha*l->opa/255;if(!alpha)return;uint16_t *row=n.fb+(size_t)y*n.width+x1;if(alpha==255){if((c&255u)==(c>>8))memset(row,c&255u,(size_t)(x2-x1+1)*sizeof(*row));else for(int x=x1;x<=x2;x++)*row++=c;}else for(int x=x1;x<=x2;x++){*row=blend565(*row,c,alpha);row++;}FINE_COUNT(n.stats.primitive[active_primitive].pixels+=(uint64_t)(x2-x1+1));}
/* Coverage at 2x2 integer subpixels only in rounded corners. The central
 * spans use direct RGB565 stores; no mask/layer/framebuffer allocations. */
static unsigned round_coverage(nui_area_t a,int r,int x,int y){if(r<=0)return 255;int cx=x<a.x1+r?a.x1+r:x>a.x2-r?a.x2-r:x,cy=y<a.y1+r?a.y1+r:y>a.y2-r?a.y2-r:y;if(cx==x||cy==y)return 255;unsigned hit=0;int64_t rr=(int64_t)(r*4)*(r*4);for(int sy=1;sy<=3;sy+=2)for(int sx=1;sx<=3;sx+=2){int64_t dx=4*(x-cx)+sx-2,dy=4*(y-cy)+sy-2;if(dx*dx+dy*dy<=rr)hit++;}return hit*255/4;}
static void rectangle(nui_layer_t *l,nui_area_t a,int radius,uint16_t c,unsigned alpha){nui_area_t clip;if(!area_intersect(&clip,a,l->clip))return;radius=MAX(0,MIN(radius,MIN(nui_area_get_width(&a),nui_area_get_height(&a))/2));for(int y=clip.y1;y<=clip.y2;y++){if(!radius||(y>=a.y1+radius&&y<=a.y2-radius)){
    int last=radius?MIN(clip.y2,a.y2-radius):clip.y2;
    if(alpha*l->opa/255==255&&last>y){
        size_t count=(size_t)(clip.x2-clip.x1+1);uint16_t *first=n.fb+(size_t)y*n.width+clip.x1;
        if((c&255u)==(c>>8))memset(first,c&255u,count*sizeof(*first));else for(size_t x=0;x<count;x++)first[x]=c;
        for(int py=y+1;py<=last;py++)memcpy(n.fb+(size_t)py*n.width+clip.x1,first,count*sizeof(*first));
        FINE_COUNT(n.stats.primitive[active_primitive].pixels+=(uint64_t)count*(last-y+1));y=last;
    }else span(l,clip.x1,clip.x2,y,c,alpha);
    continue;}int left=MAX(clip.x1,a.x1+radius),right=MIN(clip.x2,a.x2-radius);if(left<=right)span(l,left,right,y,c,alpha);for(int x=clip.x1;x<left;x++)pixel(l,x,y,c,round_coverage(a,radius,x,y)*alpha/255);for(int x=MAX(clip.x1,right+1);x<=clip.x2;x++)pixel(l,x,y,c,round_coverage(a,radius,x,y)*alpha/255);}}
static void border_pixel(nui_layer_t *l,nui_area_t a,nui_area_t inner,int radius,int width,int x,int y,uint16_t c,unsigned alpha){unsigned outer=round_coverage(a,radius,x,y),inside=0;if(x>=inner.x1&&x<=inner.x2&&y>=inner.y1&&y<=inner.y2)inside=round_coverage(inner,MAX(0,radius-width),x,y);if(outer>inside)pixel(l,x,y,c,(outer-inside)*alpha/255);}
static void border(nui_layer_t *l,nui_area_t a,int radius,int width,uint16_t c,unsigned alpha){
    if(width<=0)return;
    nui_area_t clip;if(!area_intersect(&clip,a,l->clip))return;
    radius=MAX(0,MIN(radius,MIN(nui_area_get_width(&a),nui_area_get_height(&a))/2));
    nui_area_t inner={a.x1+width,a.y1+width,a.x2-width,a.y2-width};
    int edge=MAX(radius,width);
    for(int y=clip.y1;y<=clip.y2;y++){
        if(y<a.y1+edge||y>a.y2-edge){
            if(y<a.y1+width||y>a.y2-width){
                int left=MAX(clip.x1,a.x1+radius),right=MIN(clip.x2,a.x2-radius);
                if(left<=right)span(l,left,right,y,c,alpha);
                for(int x=clip.x1;x<left;x++)border_pixel(l,a,inner,radius,width,x,y,c,alpha);
                for(int x=MAX(right+1,clip.x1);x<=clip.x2;x++)border_pixel(l,a,inner,radius,width,x,y,c,alpha);
            }else{
                for(int x=clip.x1;x<=MIN(clip.x2,a.x1+edge);x++)border_pixel(l,a,inner,radius,width,x,y,c,alpha);
                for(int x=MAX(clip.x1,a.x2-edge);x<=clip.x2;x++)border_pixel(l,a,inner,radius,width,x,y,c,alpha);
            }
        }else{
            span(l,clip.x1,MIN(clip.x2,a.x1+width-1),y,c,alpha);
            span(l,MAX(clip.x1,a.x2-width+1),clip.x2,y,c,alpha);
        }
    }
}
void nui_draw_rect_dsc_init(nui_draw_rect_dsc_t *d){memset(d,0,sizeof(*d));d->bg_opa=d->border_opa=255;}
void nui_draw_line_dsc_init(nui_draw_line_dsc_t *d){memset(d,0,sizeof(*d));d->opa=255;d->width=1;}
void nui_draw_arc_dsc_init(nui_draw_arc_dsc_t *d){memset(d,0,sizeof(*d));d->opa=255;d->width=1;}
void nui_draw_triangle_dsc_init(nui_draw_triangle_dsc_t *d){memset(d,0,sizeof(*d));d->opa=255;}
void nui_draw_rect(nui_layer_t *l,const nui_draw_rect_dsc_t *d,const nui_area_t *a){if(!l||!d||!a)return;nui_primitive_t prev=active_primitive;if(d->bg_opa){active_primitive=NUI_PRIM_FILL;uint64_t t=profile_begin(active_primitive);rectangle(l,*a,d->radius,nui_color_to_u16(d->bg_color),d->bg_opa);profile_end(active_primitive,t);}if(d->border_width&&d->border_opa){active_primitive=NUI_PRIM_BORDER;uint64_t t=profile_begin(active_primitive);border(l,*a,d->radius,d->border_width,nui_color_to_u16(d->border_color),d->border_opa);profile_end(active_primitive,t);}active_primitive=prev;}
/* Integer distance to the finite segment. Four subpixels anti-alias edges. */
static void line_precise(nui_layer_t *l,float fx1,float fy1,float fx2,float fy2,int width,int dash,int gap,uint16_t c,unsigned opa){
    if(width<=0)return;
    int x1=(int)lroundf(fx1*16),y1=(int)lroundf(fy1*16),x2=(int)lroundf(fx2*16),y2=(int)lroundf(fy2*16);
    int pad=(width+2)/2;nui_area_t a={(int)floorf(MIN(fx1,fx2))-pad,(int)floorf(MIN(fy1,fy2))-pad,(int)ceilf(MAX(fx1,fx2))+pad,(int)ceilf(MAX(fy1,fy2))+pad},clip;
    if(!area_intersect(&clip,a,l->clip))return;
    int64_t dx=x2-x1,dy=y2-y1,len2=dx*dx+dy*dy,rr=(int64_t)width*width*64;
    int length=(int)(sqrt((double)len2)/16);
    for(int y=clip.y1;y<=clip.y2;y++)for(int x=clip.x1;x<=clip.x2;x++){
        if(dash>0&&length){int64_t dot=((int64_t)x*16-x1)*dx+((int64_t)y*16-y1)*dy;int dist=(int)(MAX(0,MIN(len2,dot))*length/(len2?len2:1));if(dist%(dash+MAX(1,gap))>=dash)continue;}
        unsigned hit=0;
        for(int sy=-6;sy<=6;sy+=4)for(int sx=-6;sx<=6;sx+=4){
            int64_t px=(int64_t)x*16+sx-x1,py=(int64_t)y*16+sy-y1,dot=px*dx+py*dy;
            if(!len2||dot<=0){if(px*px+py*py<=rr)hit++;}
            else if(dot>=len2){px-=dx;py-=dy;if(px*px+py*py<=rr)hit++;}
            else{int64_t cross=px*dy-py*dx;if(cross*cross<=rr*len2)hit++;}
        }
        if(hit)pixel(l,x,y,c,opa*hit/16);
    }
}


void nui_draw_line(nui_layer_t *l,const nui_draw_line_dsc_t *d){if(!l||!d||!d->opa)return;nui_primitive_t prev=active_primitive;active_primitive=NUI_PRIM_LINE;uint64_t t=profile_begin(active_primitive);line_precise(l,d->p1.x,d->p1.y,d->p2.x,d->p2.y,d->width,d->dash_width,d->dash_gap,nui_color_to_u16(d->color),d->opa);profile_end(active_primitive,t);active_primitive=prev;}
static bool arc_inside(int64_t x,int64_t y,int64_t sx,int64_t sy,int64_t ex,int64_t ey,int sweep){int64_t from=sx*y-sy*x,to=x*ey-y*ex;return sweep>=360||(sweep<=180?(from>=0&&to>=0):(from>=0||to>=0));}
void nui_draw_arc(nui_layer_t *l,const nui_draw_arc_dsc_t *d){
    if(!l||!d||!d->opa||d->radius<=0||d->width<=0)return;
    nui_primitive_t prev=active_primitive;active_primitive=NUI_PRIM_ARC;uint64_t t=profile_begin(active_primitive);
    int begin=d->start_angle,end=d->end_angle;while(end<begin)end+=360;int sweep=MIN(360,end-begin);
    int64_t sx=(int64_t)lround(cos(begin*PI/180)*32768),sy=(int64_t)lround(sin(begin*PI/180)*32768),ex=(int64_t)lround(cos(end*PI/180)*32768),ey=(int64_t)lround(sin(end*PI/180)*32768);
    int outer=d->radius,inner=MAX(0,outer-d->width);int64_t outer2=(int64_t)outer*outer*256,inner2=(int64_t)inner*inner*256;
    nui_area_t a={d->center.x-outer-1,d->center.y-outer-1,d->center.x+outer+1,d->center.y+outer+1},clip;
    if(area_intersect(&clip,a,l->clip))for(int y=clip.y1;y<=clip.y2;y++){
        int64_t dy=(int64_t)y-d->center.y;int outer_half=(int)sqrt(MAX(0.0,(double)outer*outer-(double)dy*dy))+1;
        int inner_half=(int64_t)inner*inner>dy*dy?(int)sqrt((double)inner*inner-(double)dy*dy)-1:0;
        int left=MAX(clip.x1,d->center.x-outer_half),right=MIN(clip.x2,d->center.x+outer_half);
        for(int x=left;x<=right;x++){
            if(inner_half>0&&x>d->center.x-inner_half&&x<d->center.x+inner_half){x=d->center.x+inner_half-1;continue;}
            unsigned hit=0;for(int oy=-6;oy<=6;oy+=4)for(int ox=-6;ox<=6;ox+=4){int64_t px=((int64_t)x-d->center.x)*16+ox,py=dy*16+oy,rr=px*px+py*py;if(rr<=outer2&&rr>=inner2&&arc_inside(px,py,sx,sy,ex,ey,sweep))hit++;}
            if(hit)pixel(l,x,y,nui_color_to_u16(d->color),d->opa*hit/16);
        }
    }
    profile_end(active_primitive,t);active_primitive=prev;
}

static int64_t cross_edge(int ax,int ay,int bx,int by,int x,int y){return (int64_t)(x-ax)*(by-ay)-(int64_t)(y-ay)*(bx-ax);}
void nui_draw_triangle(nui_layer_t *l,const nui_draw_triangle_dsc_t *d){if(!l||!d)return;nui_primitive_t prev=active_primitive;active_primitive=NUI_PRIM_TRIANGLE;uint64_t t=profile_begin(active_primitive);int x[3],y[3];for(unsigned i=0;i<3;i++){x[i]=(int)lroundf(d->p[i].x*4);y[i]=(int)lroundf(d->p[i].y*4);}nui_area_t a={MIN(x[0],MIN(x[1],x[2]))/4-1,MIN(y[0],MIN(y[1],y[2]))/4-1,MAX(x[0],MAX(x[1],x[2]))/4+1,MAX(y[0],MAX(y[1],y[2]))/4+1},clip;if(area_intersect(&clip,a,l->clip)){uint16_t c=nui_color_to_u16(d->color);for(int py=clip.y1;py<=clip.y2;py++)for(int px=clip.x1;px<=clip.x2;px++){unsigned hit=0;for(int sy=1;sy<=3;sy+=2)for(int sx=1;sx<=3;sx+=2){int64_t e0=cross_edge(x[0],y[0],x[1],y[1],px*4+sx,py*4+sy),e1=cross_edge(x[1],y[1],x[2],y[2],px*4+sx,py*4+sy),e2=cross_edge(x[2],y[2],x[0],y[0],px*4+sx,py*4+sy);if((e0>=0&&e1>=0&&e2>=0)||(e0<=0&&e1<=0&&e2<=0))hit++;}if(hit)pixel(l,px,py,c,d->opa*hit/4);}}profile_end(active_primitive,t);active_primitive=prev;}
typedef struct {uint32_t rb[16],g[16];uint8_t inv[16],alpha[16];} glyph_palette_t;
static void glyph_palette_init(glyph_palette_t *p,uint16_t c,unsigned opa,unsigned layer_opa){
    for(unsigned i=0;i<16;i++){unsigned a=(opa*i/15)*layer_opa/255,mix=(a+4)>>3;p->alpha[i]=(uint8_t)a;p->inv[i]=(uint8_t)(32-mix);p->rb[i]=(c&0xf81fu)*mix;p->g[i]=(c&0x07e0u)*mix;}
}
static void glyph_draw(nui_layer_t *l,const nui_font_t *f,uint32_t cp,uint32_t next,int x,int y,nui_color_t fg,unsigned opa,const glyph_palette_t *palette){nui_font_glyph_dsc_t d;if(!nui_font_get_glyph_dsc(f,&d,cp,next))return;const nui_font_t *primary=f;f=d.resolved_font;const uint8_t *bitmap=NULL;if(f->get_glyph_bitmap==nui_font_get_bitmap_fmt_txt&&f->get_glyph_dsc==nui_font_get_glyph_dsc_fmt_txt){const nui_font_fmt_txt_dsc_t *fd=f->dsc;if(fd&&fd->bitmap_format==0)bitmap=fd->glyph_bitmap+fd->glyph_dsc[d.gid].bitmap_index;else bitmap=f->get_glyph_bitmap(f,cp);}else if(f->get_glyph_bitmap)bitmap=f->get_glyph_bitmap(f,cp);if(!bitmap)return;int gx=x+d.ofs_x,gy=y+primary->line_height-primary->base_line-d.box_h-d.ofs_y;uint16_t c=nui_color_to_u16(fg);for(int row=0;row<d.box_h;row++){int py=gy+row;if(py<l->clip.y1||py>l->clip.y2)continue;int lo=MAX(0,l->clip.x1-gx),hi=MIN((int)d.box_w-1,l->clip.x2-gx);if(lo>hi)continue;if(d.bpp==4){
    uint16_t *dst=n.fb+(size_t)py*n.width+gx+lo;
    for(int col=lo;col<=hi;col++,dst++){
        size_t nibble=(size_t)row*d.box_w+col;unsigned val=(bitmap[nibble/2]>>(nibble&1?0:4))&15;
        unsigned alpha=palette->alpha[val];if(!alpha)continue;
        if(alpha==255)*dst=c;
        else if(*dst!=c){unsigned inv=palette->inv[val];*dst=(uint16_t)((((palette->rb[val]+(*dst&0xf81fu)*inv)>>5)&0xf81fu)|(((palette->g[val]+(*dst&0x07e0u)*inv)>>5)&0x07e0u));}
        FINE_COUNT(n.stats.primitive[active_primitive].pixels++);
    }
    continue;
}for(int col=lo;col<=hi;col++){size_t bit=((size_t)row*d.box_w+col)*d.bpp;unsigned val=(bitmap[bit/8]>>(8-d.bpp-(bit%8)))&((1u<<d.bpp)-1);if(val)pixel(l,gx+col,py,c,opa*val/((1u<<d.bpp)-1));}}}
static int text_range_width(const char *s,uint32_t begin,uint32_t end,const nui_font_t *f,int letter){int w=0;bool any=false;uint32_t i=begin;while(i<end&&s[i]){uint32_t cp=nui_text_utf8_next(s,&i),peek=i,next=i<end?nui_text_utf8_next(s,&peek):0;w+=glyph_advance(f,cp,next)+letter;any=true;}return MAX(0,w-(any?letter:0));}
static void draw_text_range(nui_layer_t *l,const char *s,uint32_t begin,uint32_t end,const nui_font_t *f,int letter,int x,int y,nui_color_t fg,unsigned opa){glyph_palette_t palette;glyph_palette_init(&palette,nui_color_to_u16(fg),opa,l->opa);uint32_t i=begin;while(i<end&&s[i]){uint32_t cp=nui_text_utf8_next(s,&i),peek=i,next=i<end?nui_text_utf8_next(s,&peek):0;glyph_draw(l,f,cp,next,x,y,fg,opa,&palette);x+=glyph_advance(f,cp,next)+letter;}}
static uint32_t text_line_end(const char *s,uint32_t start,const nui_font_t *f,int letter,int width,bool wrap,uint32_t *next){uint32_t i=start,last_space=UINT32_MAX;int w=0;while(s[i]){uint32_t prev=i,cp=nui_text_utf8_next(s,&i),peek=i,nc=nui_text_utf8_next(s,&peek);if(cp=='\n'){*next=i;return prev;}int adv=glyph_advance(f,cp,nc);if(wrap&&w+adv>width&&prev>start){if(last_space!=UINT32_MAX&&last_space>start){*next=last_space+1;return last_space;}*next=prev;return prev;}if(cp==' ')last_space=prev;w+=adv+letter;}*next=i;return i;}
static void text_render(nui_obj_t *o,nui_layer_t *layer,nui_area_t a,unsigned part){const nui_font_t *f=font(o,part);const char *source=o->text?o->text:"";char *masked=NULL;if(o->password){unsigned count=utf8_count(source);masked=mem_alloc(count*3+1);if(masked){for(unsigned i=0;i<count;i++)memcpy(masked+i*3,"•",3);source=masked;}}
    int bw=num(o,NUI_STYLE_BORDER_WIDTH,part);
    nui_area_t text_area={a.x1+bw+num(o,NUI_STYLE_PAD_LEFT,part),a.y1+bw+num(o,NUI_STYLE_PAD_TOP,part),a.x2-bw-num(o,NUI_STYLE_PAD_RIGHT,part),a.y2-bw-num(o,NUI_STYLE_PAD_BOTTOM,part)};nui_layer_t l=*layer;if(!area_intersect(&l.clip,text_area,l.clip)){mem_free(masked);return;}int width=nui_area_get_width(&text_area),height=nui_area_get_height(&text_area),letter=num(o,NUI_STYLE_TEXT_LETTER_SPACE,part),line=num(o,NUI_STYLE_TEXT_LINE_SPACE,part),align=num(o,NUI_STYLE_TEXT_ALIGN,part);nui_color_t fg=color(o,NUI_STYLE_TEXT_COLOR,part);uint64_t t=profile_begin(NUI_PRIM_LABEL);nui_primitive_t prev=active_primitive;active_primitive=NUI_PRIM_LABEL;
    if(o->long_mode==NUI_LABEL_LONG_SCROLL_CIRCULAR&&!is_kind(o,4)){int total=text_range_width(source,0,(uint32_t)strlen(source),f,letter);if(total>width){uint32_t age=n.ticks-o->text_epoch;int offset=age<=1000?0:(int)(((uint64_t)(age-1000)*40/1000)%(unsigned)(total+40));draw_text_range(&l,source,0,(uint32_t)strlen(source),f,letter,text_area.x1-offset,text_area.y1,fg,255);draw_text_range(&l,source,0,(uint32_t)strlen(source),f,letter,text_area.x1-offset+total+40,text_area.y1,fg,255);goto text_done;}}
    uint32_t start=0;int rows=MAX(1,(height+line)/(f->line_height+line));if(o->one_line)rows=1;for(int row=0;row<rows&&source[start];row++){uint32_t next,end=text_line_end(source,start,f,letter,width,!o->one_line&&!(row==rows-1&&o->long_mode==NUI_LABEL_LONG_DOT),&next);int w=text_range_width(source,start,end,f,letter),dots=0;bool truncated=row==rows-1&&(source[next]||w>width)&&o->long_mode==NUI_LABEL_LONG_DOT&&!is_kind(o,4);if(o->one_line&&w>width&&!is_kind(o,4)&&o->long_mode==NUI_LABEL_LONG_DOT)truncated=true;if(truncated){dots=text_range_width("...",0,3,f,letter);while(end>start&&w+dots>width){end--;while(end>start&&((uint8_t)source[end]&0xc0)==0x80)end--;w=text_range_width(source,start,end,f,letter);}}if(truncated&&!is_kind(o,4)){
            char *display=mem_alloc((size_t)end+4);if(display){memcpy(display,source,end);memcpy(display+end,"...",4);if(!o->display_text||strcmp(o->display_text,display)){mem_free(o->display_text);o->display_text=display;}else mem_free(display);}
        }else if(row==rows-1&&!is_kind(o,4)){mem_free(o->display_text);o->display_text=NULL;}
        int x=text_area.x1;if(align==NUI_TEXT_ALIGN_CENTER)x+=(width-w-dots)/2;else if(align==NUI_TEXT_ALIGN_RIGHT)x+=width-w-dots;
        if(is_kind(o,4)&&o->one_line){int cursor_width=text_range_width(source,0,MIN((uint32_t)strlen(source),o->cursor),f,letter);if(cursor_width>width-4)x-=cursor_width-width+4;}
        draw_text_range(&l,source,start,end,f,letter,x,text_area.y1+row*(f->line_height+line),fg,255);if(dots)draw_text_range(&l,"...",0,3,f,letter,x+w,text_area.y1+row*(f->line_height+line),fg,255);if(next<=start)break;start=next;}
    if(n.focus==o&&is_kind(o,4)&&(((n.ticks-o->blink_epoch)/400)%2)==0){int cw=text_range_width(source,0,MIN((uint32_t)strlen(source),o->cursor),f,letter);int x=text_area.x1+MIN(width-2,cw);span(&l,x,x,text_area.y1,nui_color_to_u16(fg),255);for(int y=text_area.y1;y<text_area.y1+f->line_height;y++)pixel(&l,x,y,nui_color_to_u16(fg),255);}
text_done:profile_end(NUI_PRIM_LABEL,t);active_primitive=prev;mem_free(masked);}

#define KEYBOARD_SYMBOL "\xEF\x84\x9C"
static const char *const lower_map[]={"1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", NUI_SYMBOL_BACKSPACE, "\n",
                                                 "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", NUI_SYMBOL_NEW_LINE, "\n",
                                                 "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
                                                 NUI_SYMBOL_KEYBOARD,

                                                 NUI_SYMBOL_LEFT, " ", NUI_SYMBOL_RIGHT, NUI_SYMBOL_OK, ""
                                                };
static const char *const upper_map[]={"1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", NUI_SYMBOL_BACKSPACE, "\n",
                                                 "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", NUI_SYMBOL_NEW_LINE, "\n",
                                                 "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
                                                 NUI_SYMBOL_CLOSE,

                                                 NUI_SYMBOL_LEFT, " ", NUI_SYMBOL_RIGHT, NUI_SYMBOL_OK, ""
                                                };
static const char *const symbols_map[]={"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", NUI_SYMBOL_BACKSPACE, "\n",
                                                   "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
                                                   "\\",  "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
                                                   NUI_SYMBOL_KEYBOARD,

                                                   NUI_SYMBOL_LEFT, " ", NUI_SYMBOL_RIGHT, NUI_SYMBOL_OK, ""
                                                  };
static const char *const number_map[]={"1", "2", "3", NUI_SYMBOL_KEYBOARD, "\n",
                                                  "4", "5", "6", NUI_SYMBOL_OK, "\n",
                                                  "7", "8", "9", NUI_SYMBOL_BACKSPACE, "\n",
                                                  "+/-", "0", ".", NUI_SYMBOL_LEFT, NUI_SYMBOL_RIGHT, ""
                                                 };
#define C 0x0100u
#define T 0x0320u
#define P 0x0400u
static const nui_buttonmatrix_ctrl_t alpha_ctrl[]={5|T,4|P,4|P,4|P,4|P,4|P,4|P,4|P,4|P,4|P,4|P,7|C,6|T,3|P,3|P,3|P,3|P,3|P,3|P,3|P,3|P,3|P,7|C,1|C|P,1|C|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|C|P,1|C|P,1|C|P,2|T,2|C,6,2|C,2|T};
static const nui_buttonmatrix_ctrl_t number_ctrl[]={1,1,1,2|T,1,1,1,2|T,1,1,1,2,1,1,1,1,1};
static const nui_buttonmatrix_ctrl_t symbols_ctrl[]={1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,2|C,2|T,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,1|P,2|T,2|C,6,2|C,2|T};
#undef C
#undef T
#undef P
static const nui_buttonmatrix_ctrl_t *keyboard_controls(nui_obj_t *o){if(o->key_mode>=0&&o->key_mode<4&&o->ctrl[o->key_mode])return o->ctrl[o->key_mode];return o->key_mode==NUI_KEYBOARD_MODE_NUMBER?number_ctrl:o->key_mode==4?symbols_ctrl:alpha_ctrl;}
static const char *const *keyboard_map(nui_obj_t *o){if(o->key_mode>=0&&o->key_mode<4&&o->maps[o->key_mode])return o->maps[o->key_mode];if(o->key_mode==NUI_KEYBOARD_MODE_NUMBER)return number_map;if(o->key_mode==NUI_KEYBOARD_MODE_TEXT_UPPER)return upper_map;if(o->key_mode==4)return symbols_map;return lower_map;}
static size_t keyboard_geometry(nui_obj_t *o,nui_area_t *areas,const char **texts,size_t cap){const char *const *map=keyboard_map(o);unsigned rows=1,count=0;for(unsigned i=0;map[i]&&*map[i]&&i<MAX_KEYS+8;i++){if(!strcmp(map[i],"\n"))rows++;else count++;}nui_area_t a;nui_obj_get_coords(o,&a);int pl=num(o,NUI_STYLE_PAD_LEFT,0),pr=num(o,NUI_STYLE_PAD_RIGHT,0),pt=num(o,NUI_STYLE_PAD_TOP,0),pb=num(o,NUI_STYLE_PAD_BOTTOM,0),gx=num(o,NUI_STYLE_PAD_COLUMN,0),gy=num(o,NUI_STYLE_PAD_ROW,0);int usable_height=MAX(1,nui_area_get_height(&a)-pt-pb-((int)rows-1)*gy);unsigned row=0,index=0,key=0;while(map[index]&&*map[index]&&key<MAX_KEYS){unsigned start=index,end=index,weights=0,columns=0;while(map[end]&&*map[end]&&strcmp(map[end],"\n")){unsigned weight=1;weight=keyboard_controls(o)[key+columns]&NUI_BUTTONMATRIX_CTRL_WIDTH_MASK;if(!weight)weight=1;weights+=weight;columns++;end++;}int available=nui_area_get_width(&a)-pl-pr-MAX(0,(int)columns-1)*gx,x=a.x1+pl;unsigned used=0;for(unsigned i=start;i<end;i++,key++){unsigned weight=1;weight=keyboard_controls(o)[key]&NUI_BUTTONMATRIX_CTRL_WIDTH_MASK;if(!weight)weight=1;int w=available*(int)(used+weight)/(int)weights-available*(int)used/(int)weights;used+=weight;if(key<cap){if(areas)areas[key]=(nui_area_t){x,a.y1+pt+(int)row*gy+(int)row*usable_height/(int)rows,x+w-1,a.y1+pt+(int)row*gy+(int)(row+1)*usable_height/(int)rows-1};if(texts)texts[key]=map[i];}x+=w+gx;}index=end;if(!map[index]||!*map[index])break;index++;row++;}return key;}
size_t nui_native_keyboard_get_keys(nui_obj_t *o,nui_area_t *areas,const char **texts,size_t cap){return is_kind(o,5)?keyboard_geometry(o,areas,texts,cap):0;}
const char *nui_buttonmatrix_get_button_text(nui_obj_t *o,uint32_t index){const char *keys[MAX_KEYS];size_t count=keyboard_geometry(o,NULL,keys,MAX_KEYS);return index<count?keys[index]:NULL;}
void nui_keyboard_set_map(nui_obj_t *o,int mode,const char *const map[],const nui_buttonmatrix_ctrl_t ctrl[]){if(!o||mode<0||mode>=4)return;o->maps[mode]=map;o->ctrl[mode]=ctrl;nui_obj_invalidate(o);}
void nui_keyboard_set_mode(nui_obj_t *o,int mode){if(o&&o->key_mode!=mode){o->key_mode=mode;o->key_pressed=-1;nui_obj_invalidate(o);}}
void nui_keyboard_set_textarea(nui_obj_t *o,nui_obj_t *textarea){if(!o)return;o->textarea=textarea;if(textarea&&!textarea->deleted){n.focus=textarea;textarea->blink_epoch=n.ticks;nui_obj_add_state(textarea,NUI_STATE_FOCUSED);}}
static int keyboard_hit(nui_obj_t *o,int x,int y){nui_area_t areas[MAX_KEYS];size_t count=keyboard_geometry(o,areas,NULL,MAX_KEYS);for(unsigned i=0;i<count;i++)if(x>=areas[i].x1&&x<=areas[i].x2&&y>=areas[i].y1&&y<=areas[i].y2)return (int)i;return -1;}
static void keyboard_execute(nui_obj_t *o,int index){const char *key=nui_buttonmatrix_get_button_text(o,(uint32_t)index);if(!key)return;if(!strcmp(key,NUI_SYMBOL_OK)){event_send(o,NUI_EVENT_READY,NULL,NULL);return;}if(!strcmp(key,NUI_SYMBOL_CLOSE)||!strcmp(key,KEYBOARD_SYMBOL)){event_send(o,NUI_EVENT_CANCEL,NULL,NULL);return;}if(!strcmp(key,"ABC")){nui_keyboard_set_mode(o,NUI_KEYBOARD_MODE_TEXT_UPPER);return;}if(!strcmp(key,"abc")){nui_keyboard_set_mode(o,NUI_KEYBOARD_MODE_TEXT_LOWER);return;}if(!strcmp(key,"1#")){nui_keyboard_set_mode(o,4);return;}nui_obj_t *ta=o->textarea;if(!ta||ta->deleted)return;if(!strcmp(key,NUI_SYMBOL_BACKSPACE))nui_textarea_delete_char(ta);else if(!strcmp(key,NUI_SYMBOL_LEFT)){uint32_t pos=ta->cursor;if(pos){pos--;while(pos&&((uint8_t)nui_textarea_get_text(ta)[pos]&0xc0)==0x80)pos--;}ta->cursor=pos;ta->blink_epoch=n.ticks;nui_obj_invalidate(ta);}else if(!strcmp(key,NUI_SYMBOL_RIGHT)){uint32_t pos=ta->cursor;nui_text_utf8_next(nui_textarea_get_text(ta),&pos);ta->cursor=pos;ta->blink_epoch=n.ticks;nui_obj_invalidate(ta);}else if(!strcmp(key,NUI_SYMBOL_NEW_LINE))nui_textarea_add_text(ta,"\n");else if(!strcmp(key,"+/-")){const char *value=nui_textarea_get_text(ta);if(*value=='-')nui_textarea_set_text(ta,value+1);else{size_t len=strlen(value);char *buf=mem_alloc(len+2);if(buf){buf[0]='-';memcpy(buf+1,value,len+1);nui_textarea_set_text(ta,buf);mem_free(buf);}}}else nui_textarea_add_text(ta,key);}
static bool hit_area(nui_obj_t *o,int x,int y){nui_area_t a;nui_obj_get_coords(o,&a);return x>=a.x1-o->ext&&x<=a.x2+o->ext&&y>=a.y1-o->ext&&y<=a.y2+o->ext;}
static nui_obj_t *hit_test(nui_obj_t *o,int x,int y){if(!nui_obj_is_visible(o)||(o->state&NUI_STATE_DISABLED))return NULL;bool hit=hit_area(o,x,y);if(!hit)return NULL;for(nui_obj_t *c=o->last;c;c=c->prev){nui_obj_t *found=hit_test(c,x,y);if(found)return found;}return o->flags&NUI_OBJ_FLAG_CLICKABLE?o:NULL;}
nui_indev_t *nui_indev_active(void){return &n.input;}
void nui_indev_get_point(nui_indev_t *dev,nui_point_t *point){if(point)*point=dev?dev->point:n.input.point;}
void nui_native_touch(int x,int y,bool pressed){bool was=n.input.pressed;n.input.point=(nui_point_t){x,y};n.input.pressed=pressed;n.event_depth++;if(pressed&&!was){n.capture=hit_test(n.root,x,y);nui_obj_t *o=n.capture;if(o){if(is_kind(o,4)){if(n.focus&&n.focus!=o)nui_obj_remove_state(n.focus,NUI_STATE_FOCUSED);n.focus=o;o->blink_epoch=n.ticks;nui_obj_add_state(o,NUI_STATE_FOCUSED);event_send(o,NUI_EVENT_FOCUSED,NULL,NULL);}nui_obj_add_state(o,NUI_STATE_PRESSED);if(is_kind(o,5)){o->key_pressed=keyboard_hit(o,x,y);o->key_on_press=false;o->key_repeat_due=n.ticks+400;
        if(o->key_pressed>=0&&!(keyboard_controls(o)[o->key_pressed]&0x0200u)){o->key_on_press=true;keyboard_execute(o,o->key_pressed);}
        if(!o->deleted)nui_obj_invalidate(o);}event_send(o,NUI_EVENT_PRESSED,NULL,NULL);}}else if(pressed&&was){nui_obj_t *o=n.capture;if(o&&!o->deleted){if(!hit_area(o,x,y)||!nui_obj_is_visible(o)){nui_obj_remove_state(o,NUI_STATE_PRESSED);event_send(o,NUI_EVENT_PRESS_LOST,NULL,NULL);n.capture=NULL;}else{if(is_kind(o,5)){int key=keyboard_hit(o,x,y);if(key!=o->key_pressed){o->key_pressed=key;nui_obj_invalidate(o);}}event_send(o,NUI_EVENT_PRESSING,NULL,NULL);}}}else if(!pressed&&was){nui_obj_t *o=n.capture;n.capture=NULL;if(o&&!o->deleted){bool click=hit_area(o,x,y)&&nui_obj_is_visible(o)&&!(o->state&NUI_STATE_DISABLED);nui_obj_remove_state(o,NUI_STATE_PRESSED);event_send(o,NUI_EVENT_RELEASED,NULL,NULL);if(!o->deleted&&click){if(is_kind(o,3)){if(o->state&NUI_STATE_CHECKED)nui_obj_remove_state(o,NUI_STATE_CHECKED);else nui_obj_add_state(o,NUI_STATE_CHECKED);event_send(o,NUI_EVENT_VALUE_CHANGED,NULL,NULL);}if(!o->deleted&&is_kind(o,5)){int key=keyboard_hit(o,x,y);if(key>=0&&key==o->key_pressed&&!o->key_on_press)keyboard_execute(o,key);if(!o->deleted){o->key_pressed=-1;nui_obj_invalidate(o);}}if(!o->deleted)event_send(o,NUI_EVENT_CLICKED,NULL,NULL);}}}n.event_depth--;garbage_collect();}
nui_timer_t *nui_timer_create(void (*cb)(nui_timer_t*),uint32_t period,void *user){nui_timer_t *t=mem_alloc(sizeof(*t));if(!t)return NULL;*t=(nui_timer_t){MAX(1u,period),n.ticks+MAX(1u,period),cb,user,false,n.timers};n.timers=t;return t;}
void nui_timer_delete(nui_timer_t *t){if(t)t->deleted=true;}
void nui_timer_set_period(nui_timer_t *t,uint32_t period){if(t&&!t->deleted){t->period=MAX(1u,period);t->due=n.ticks+t->period;}}
void *nui_timer_get_user_data(nui_timer_t *t){return t?t->user:NULL;}
static void tick_walk(nui_obj_t *o,uint32_t old){if(!o||!nui_obj_is_visible(o))return;if(is_kind(o,1)&&o->long_mode==NUI_LABEL_LONG_SCROLL_CIRCULAR&&old/80!=n.ticks/80){nui_point_t size;nui_text_get_size(&size,nui_label_get_text(o),font(o,0),num(o,NUI_STYLE_TEXT_LETTER_SPACE,0),0,NUI_COORD_MAX,0);if(size.x>nui_obj_get_width(o)&&n.ticks-o->text_epoch>1000)nui_obj_invalidate(o);}for(nui_obj_t *c=o->first;c;c=c->next)tick_walk(c,old);}
void nui_native_tick(uint32_t ms){uint32_t old=n.ticks;n.ticks+=ms;n.event_depth++;for(nui_timer_t *t=n.timers;t;){nui_timer_t *next=t->next;if(!t->deleted&&(int32_t)(n.ticks-t->due)>=0){t->due=n.ticks+t->period;if(t->cb)t->cb(t);}t=next;}nui_timer_t **tp=&n.timers;while(*tp){if((*tp)->deleted){nui_timer_t *dead=*tp;*tp=dead->next;mem_free(dead);}else tp=&(*tp)->next;}if(n.capture&&n.input.pressed&&!n.capture->deleted)event_send(n.capture,NUI_EVENT_PRESSING,NULL,NULL);if(n.capture&&is_kind(n.capture,5)&&n.input.pressed&&n.capture->key_pressed>=0){
        nui_obj_t *k=n.capture;unsigned flags=keyboard_controls(k)[k->key_pressed];
        if(!(flags&0x0220u)&&(int32_t)(n.ticks-k->key_repeat_due)>=0){k->key_repeat_due=n.ticks+100;keyboard_execute(k,k->key_pressed);}
    }
    if(n.focus&&(old-n.focus->blink_epoch)/400!=(n.ticks-n.focus->blink_epoch)/400)nui_obj_invalidate(n.focus);
    tick_walk(n.root,old);n.event_depth--;garbage_collect();}
uint32_t nui_tick_get(void){return n.ticks;}
void nui_tick_inc(uint32_t ms){nui_native_tick(ms);}
uint32_t nui_timer_handler(void){nui_native_tick(0);return 20;}
void nui_chart_set_type(nui_obj_t *o,int type){(void)o;assert(type==NUI_CHART_TYPE_LINE);}
void nui_chart_set_point_count(nui_obj_t *o,uint32_t count){if(o&&o->point_count!=count){o->point_count=MAX(1u,count);nui_obj_invalidate(o);}}
uint32_t nui_chart_get_point_count(const nui_obj_t *o){return o?o->point_count:0;}
void nui_chart_set_axis_range(nui_obj_t *o,int axis,int32_t min,int32_t max){assert(axis==NUI_CHART_AXIS_PRIMARY_Y);if(o&&(o->ymin!=min||o->ymax!=max)){o->ymin=min;o->ymax=max>min?max:min+1;nui_obj_invalidate(o);}}
void nui_chart_set_div_line_count(nui_obj_t *o,uint8_t h,uint8_t v){if(o&&(o->hdiv!=h||o->vdiv!=v)){o->hdiv=h;o->vdiv=v;nui_obj_invalidate(o);}}
nui_chart_series_t *nui_chart_add_series(nui_obj_t *o,nui_color_t c,int axis){assert(axis==NUI_CHART_AXIS_PRIMARY_Y);if(!o)return NULL;nui_chart_series_t *s=mem_alloc(sizeof(*s));if(!s)return NULL;s->color=c;nui_chart_series_t **tail=&o->series;while(*tail)tail=&(*tail)->next;*tail=s;return s;}
void nui_chart_set_series_ext_y_array(nui_obj_t *o,nui_chart_series_t *s,int32_t *array){if(!s)return;if(s->owned)mem_free(s->y_points);if(s->y_points!=array){s->y_points=array;s->owned=false;nui_obj_invalidate(o);}}
void nui_chart_set_series_color(nui_obj_t *o,nui_chart_series_t *s,nui_color_t c){if(s&&s->color!=c){s->color=c;nui_obj_invalidate(o);}}
nui_color_t nui_chart_get_series_color(nui_obj_t *o,const nui_chart_series_t *s){(void)o;return s?s->color:0;}
void nui_chart_hide_series(nui_obj_t *o,nui_chart_series_t *s,bool hidden){if(s&&s->hidden!=hidden){s->hidden=hidden;nui_obj_invalidate(o);}}
nui_chart_series_t *nui_chart_get_series_next(nui_obj_t *o,const nui_chart_series_t *s){return s?s->next:o?o->series:NULL;}
int32_t *nui_chart_get_series_y_array(nui_obj_t *o,nui_chart_series_t *s){(void)o;return s?s->y_points:NULL;}
void nui_chart_refresh(nui_obj_t *o){nui_obj_invalidate(o);}

static void image_render(nui_obj_t *o,nui_layer_t *layer,nui_area_t a){const nui_image_dsc_t *d=o->image;if(!d||!d->data)return;assert(d->header.cf==NUI_COLOR_FORMAT_RGB565);nui_area_t clip;if(!area_intersect(&clip,a,layer->clip))return;unsigned stride=d->header.stride?d->header.stride/2:d->header.w,opa=(unsigned)num(o,NUI_STYLE_IMAGE_OPA,0)*layer->opa/255;assert(stride>=d->header.w&&d->data_size>=(size_t)stride*d->header.h*2);const uint16_t *src=(const uint16_t*)d->data;uint64_t t=profile_begin(NUI_PRIM_IMAGE);nui_primitive_t prev=active_primitive;active_primitive=NUI_PRIM_IMAGE;for(int y=clip.y1;y<=clip.y2;y++){const uint16_t *row=src+(size_t)(y-a.y1)*stride+(clip.x1-a.x1);uint16_t *dest=n.fb+(size_t)y*n.width+clip.x1;size_t count=(size_t)(clip.x2-clip.x1+1);if(opa==255)memcpy(dest,row,count*2);else for(size_t x=0;x<count;x++)dest[x]=blend565(dest[x],row[x],opa);FINE_COUNT(n.stats.primitive[NUI_PRIM_IMAGE].pixels+=count);}profile_end(NUI_PRIM_IMAGE,t);active_primitive=prev;}

/* Separable integer blur of a rounded opaque silhouette. Support is half the
 * declared shadow width; the center is shifted before filtering. No masks,
 * per-frame allocation or expanded frame layers are used. */
static bool inside_round(nui_area_t a,int r,int x,int y){if(x<a.x1||x>a.x2||y<a.y1||y>a.y2)return false;r=MAX(0,MIN(r,MIN(nui_area_get_width(&a),nui_area_get_height(&a))/2));if(!r||(x>=a.x1+r&&x<=a.x2-r)||(y>=a.y1+r&&y<=a.y2-r))return true;int cx=x<a.x1+r?a.x1+r:a.x2-r,cy=y<a.y1+r?a.y1+r:a.y2-r;int64_t dx=x-cx,dy=y-cy;return dx*dx+dy*dy<=(int64_t)r*r;}
static unsigned blur_axis(int coordinate,int low,int high,int blur){
    static const unsigned weight[9]={1,8,28,56,70,56,28,8,1};unsigned sum=0;
    for(int k=-4;k<=4;k++){int pos=coordinate+k*blur/4;if(pos>=low&&pos<=high)sum+=weight[k+4];}return sum;
}

#define SHADOW_CACHE_LIMIT 65536u
#define SHADOW_CACHE_SLOTS 8
/* A canonical 9-slice is independent of card width/height. The cached data is
 * only alpha, computed by exactly the v1 integer blur formula. */
typedef struct {int radius,width,offset,opa,cap,cw,ch,mw,mh;bool narrow_w,narrow_h;uint8_t *alpha;size_t bytes;uint64_t used;} shadow_cache_entry;
static shadow_cache_entry shadow_cache[SHADOW_CACHE_SLOTS];
static size_t shadow_cache_bytes,shadow_cache_peak_bytes;
static uint64_t shadow_cache_clock;
static unsigned shadow_alpha(nui_area_t a,int radius,int blur,int offset,unsigned opa,int x,int y){
    if(inside_round(a,MAX(0,radius-1),x,y))return 0;
    nui_area_t shape={a.x1,a.y1+offset,a.x2,a.y2+offset};
    static const unsigned weight[9]={1,8,28,56,70,56,28,8,1};unsigned coverage;
    if(x>=shape.x1+radius+blur&&x<=shape.x2-radius-blur)coverage=blur_axis(y,shape.y1,shape.y2,blur)*256;
    else if(y>=shape.y1+radius+blur&&y<=shape.y2-radius-blur)coverage=blur_axis(x,shape.x1,shape.x2,blur)*256;
    else{coverage=0;for(int iy=-4;iy<=4;iy++)for(int ix=-4;ix<=4;ix++)if(inside_round(shape,radius,x+ix*blur/4,y+iy*blur/4))coverage+=weight[ix+4]*weight[iy+4];}
    unsigned alpha=(coverage*opa+32768)>>16;return MIN(alpha,(opa+1)/2);
}
static void shadow_cache_clear(void){for(unsigned i=0;i<SHADOW_CACHE_SLOTS;i++){mem_free(shadow_cache[i].alpha);memset(&shadow_cache[i],0,sizeof(shadow_cache[i]));}shadow_cache_bytes=0;}
static shadow_cache_entry *shadow_mask(int w,int h,int radius,int width,int offset,unsigned opa){
    int blur=MAX(1,(width+1)/2),cap=radius+2*blur+abs(offset)+2;
    if(cap>96||width>32||abs(offset)>32)return NULL;
    int cw=MIN(w,2*cap+1),ch=MIN(h,2*cap+1);
    for(unsigned i=0;i<SHADOW_CACHE_SLOTS;i++){shadow_cache_entry *e=&shadow_cache[i];if(e->alpha&&e->radius==radius&&e->width==width&&e->offset==offset&&e->opa==(int)opa&&e->cw==cw&&e->ch==ch){e->used=++shadow_cache_clock;FINE_COUNT(n.stats.shadow_cache_hits++);return e;}}
    FINE_COUNT(n.stats.shadow_cache_misses++);
    size_t bytes=(size_t)(cw+2*blur)*(ch+2*blur);if(bytes>SHADOW_CACHE_LIMIT)return NULL;
    int slot=-1;for(unsigned i=0;i<SHADOW_CACHE_SLOTS;i++)if(!shadow_cache[i].alpha){slot=(int)i;break;}
    if(slot<0){slot=0;for(unsigned i=1;i<SHADOW_CACHE_SLOTS;i++)if(shadow_cache[i].used<shadow_cache[slot].used)slot=(int)i;}
    while(shadow_cache_bytes+bytes>SHADOW_CACHE_LIMIT||shadow_cache[slot].alpha){shadow_cache_entry *e=&shadow_cache[slot];shadow_cache_bytes-=e->bytes;if(e->alpha)FINE_COUNT(n.stats.shadow_cache_evictions++);mem_free(e->alpha);memset(e,0,sizeof(*e));if(shadow_cache_bytes+bytes<=SHADOW_CACHE_LIMIT)break;slot=0;for(unsigned i=1;i<SHADOW_CACHE_SLOTS;i++)if(shadow_cache[i].alpha&&(!shadow_cache[slot].alpha||shadow_cache[i].used<shadow_cache[slot].used))slot=(int)i;}
    shadow_cache_entry *e=&shadow_cache[slot];uint8_t *data=mem_alloc(bytes);if(!data)return NULL;
    *e=(shadow_cache_entry){.radius=radius,.width=width,.offset=offset,.opa=(int)opa,.cap=cap,.cw=cw,.ch=ch,.mw=cw+2*blur,.mh=ch+2*blur,.narrow_w=w<=2*cap,.narrow_h=h<=2*cap,.alpha=data,.bytes=bytes,.used=++shadow_cache_clock};
    nui_area_t canonical={0,0,cw-1,ch-1};for(int y=0;y<e->mh;y++)for(int x=0;x<e->mw;x++)data[(size_t)y*e->mw+x]=(uint8_t)shadow_alpha(canonical,radius,blur,offset,opa,x-blur,y-blur+offset);
    shadow_cache_bytes+=bytes;if(shadow_cache_bytes>shadow_cache_peak_bytes)shadow_cache_peak_bytes=shadow_cache_bytes;FINE_COUNT(n.stats.shadow_cache_build_pixels+=bytes);return e;
}
static int shadow_map(int coordinate,int length,int canonical,int cap,bool narrow){if(narrow||coordinate<cap)return coordinate;if(coordinate>=length-cap)return coordinate-length+canonical;return cap;}
static void shadow_render(nui_layer_t *l,nui_area_t a,int radius,int width,int offset,uint16_t c,unsigned opa){
    int blur=MAX(1,(width+1)/2);nui_area_t expanded={a.x1-blur,a.y1+offset-blur,a.x2+blur,a.y2+offset+blur},clip;
    if(!area_intersect(&clip,expanded,l->clip))return;
    int w=nui_area_get_width(&a),h=nui_area_get_height(&a);radius=MAX(0,MIN(radius,MIN(w,h)/2));
    shadow_cache_entry *e=shadow_mask(w,h,radius,width,offset,opa);
    /* Rows through the solid interior only visit the two edge strips. */
    int edge=MAX(radius+blur+abs(offset),blur+1);
    for(int y=clip.y1;y<=clip.y2;y++){
        int intervals[4]={clip.x1,clip.x2,1,0};
        if(y>=a.y1+edge&&y<=a.y2-edge){intervals[1]=MIN(clip.x2,a.x1+edge);intervals[2]=MAX(clip.x1,a.x2-edge);intervals[3]=clip.x2;if(intervals[2]<=intervals[1]){intervals[1]=clip.x2;intervals[2]=1;intervals[3]=0;}}
        for(unsigned run=0;run<2;run++)for(int x=intervals[run*2];x<=intervals[run*2+1];x++){
            unsigned alpha;
            if(e){int sx=shadow_map(x-a.x1,w,e->cw,e->cap,e->narrow_w)+blur,sy=shadow_map(y-a.y1,h,e->ch,e->cap,e->narrow_h)+blur-offset;alpha=e->alpha[(size_t)sy*e->mw+sx];}
            else alpha=shadow_alpha(a,radius,blur,offset,opa,x,y);
            if(alpha)pixel(l,x,y,c,alpha);
        }
    }
}

/* One bounded mathematical LUT, never a cached rendered image. */
static nui_gradient_cache_t gradient_cache;
static void gradient_render(nui_layer_t *l,nui_area_t a,nui_area_t clip,int radius,
                            int direction,nui_color_t begin,nui_color_t end,unsigned opa){
    bool horizontal=direction==NUI_GRAD_DIR_HOR;
    int total=horizontal?nui_area_get_width(&a):nui_area_get_height(&a);
#if NUI_PROFILE_FINE_COUNTERS
    bool replaced=gradient_cache.valid&&(gradient_cache.length!=total||gradient_cache.begin!=begin||gradient_cache.end!=end);
#endif
    const uint16_t *lut=nui_gradient_prepare(&gradient_cache,total,begin,end);
    FINE_COUNT(if(lut&&replaced)n.stats.gradient_cache_evictions++);
    radius=MIN(radius,MIN(nui_area_get_width(&a),nui_area_get_height(&a))/2);
    for(int y=clip.y1;y<=clip.y2;y++){
        int left=clip.x1,right=clip.x1-1;
        if(lut&&horizontal&&opa==255&&l->opa==255){
            if(radius<=0||(y>=a.y1+radius&&y<=a.y2-radius)){left=clip.x1;right=clip.x2;}
            else{left=MAX(clip.x1,a.x1+radius);right=MIN(clip.x2,a.x2-radius);}
            if(left<=right){
                size_t count=(size_t)(right-left+1);
                memcpy(n.fb+(size_t)y*n.width+left,lut+left-a.x1,count*sizeof(*lut));
                FINE_COUNT(n.stats.primitive[NUI_PRIM_FILL].pixels+=count);
            }else{left=clip.x1;right=clip.x1-1;}
        }
        /* Identical coverage/blend path for corners, inherited opacity and LUT fallback. */
        for(int run=0;run<2;run++){
            int from=run?right+1:clip.x1,to=run?clip.x2:left-1;
            for(int x=from;x<=to;x++){
                int pos=horizontal?x-a.x1:y-a.y1;
                uint16_t c=lut?lut[pos]:nui_color_to_u16(nui_color_mix(end,begin,(uint8_t)(pos*255/MAX(1,total-1))));
                pixel(l,x,y,c,opa*round_coverage(a,radius,x,y)/255);
            }
        }
    }
}

static void object_box(nui_obj_t *o,nui_layer_t *l,nui_area_t a,unsigned part){int r=num(o,NUI_STYLE_RADIUS,part),sw=num(o,NUI_STYLE_SHADOW_WIDTH,part),sy=num(o,NUI_STYLE_SHADOW_OFFSET_Y,part);if(sw>0&&num(o,NUI_STYLE_SHADOW_OPA,part)>0){
        uint64_t t=profile_begin(NUI_PRIM_SHADOW);nui_primitive_t prev=active_primitive;active_primitive=NUI_PRIM_SHADOW;
        shadow_render(l,a,r,sw,sy,nui_color_to_u16(color(o,NUI_STYLE_SHADOW_COLOR,part)),(unsigned)num(o,NUI_STYLE_SHADOW_OPA,part));
        profile_end(NUI_PRIM_SHADOW,t);active_primitive=prev;
    }
    nui_draw_rect_dsc_t d={.radius=r,.bg_color=color(o,NUI_STYLE_BG_COLOR,part),.bg_opa=(uint8_t)num(o,NUI_STYLE_BG_OPA,part),.border_color=color(o,NUI_STYLE_BORDER_COLOR,part),.border_width=num(o,NUI_STYLE_BORDER_WIDTH,part),.border_opa=255};
    int grad=num(o,NUI_STYLE_BG_GRAD_DIR,part);if(grad&&d.bg_opa){uint64_t t=profile_begin(NUI_PRIM_FILL);nui_primitive_t prev=active_primitive;active_primitive=NUI_PRIM_FILL;nui_area_t clip;if(area_intersect(&clip,a,l->clip)){nui_color_t end=color(o,NUI_STYLE_BG_GRAD_COLOR,part);gradient_render(l,a,clip,r,grad,d.bg_color,end,d.bg_opa);}profile_end(NUI_PRIM_FILL,t);active_primitive=prev;d.bg_opa=0;}
    nui_draw_rect(l,&d,&a);int ow=num(o,NUI_STYLE_OUTLINE_WIDTH,part),op=num(o,NUI_STYLE_OUTLINE_PAD,part);if(ow>0){nui_area_t outline={a.x1-op-ow,a.y1-op-ow,a.x2+op+ow,a.y2+op+ow};nui_draw_rect_dsc_t od={.radius=r==NUI_RADIUS_CIRCLE?r:r+op+ow,.border_color=color(o,NUI_STYLE_OUTLINE_COLOR,part),.border_width=ow,.border_opa=(uint8_t)num(o,NUI_STYLE_OUTLINE_OPA,part)};nui_draw_rect(l,&od,&outline);}}
static void keyboard_render(nui_obj_t *o,nui_layer_t *l){nui_area_t areas[MAX_KEYS];const char *keys[MAX_KEYS];size_t count=keyboard_geometry(o,areas,keys,MAX_KEYS);uint32_t saved=o->state;for(unsigned i=0;i<count;i++){if(!area_intersect(&(nui_area_t){0},areas[i],l->clip))continue;o->state=saved;if(keyboard_controls(o)[i]&0x0100u)o->state|=NUI_STATE_CHECKED;if(o->key_pressed==(int)i)o->state|=NUI_STATE_PRESSED;unsigned part=NUI_PART_ITEMS;object_box(o,l,areas[i],part);const nui_font_t *f=font(o,part);int letter=num(o,NUI_STYLE_TEXT_LETTER_SPACE,part),w=text_range_width(keys[i],0,(uint32_t)strlen(keys[i]),f,letter),x=areas[i].x1+(nui_area_get_width(&areas[i])-w)/2,y=areas[i].y1+(nui_area_get_height(&areas[i])-f->line_height)/2;nui_layer_t key_layer=*l;if(area_intersect(&key_layer.clip,l->clip,areas[i])){uint64_t t=profile_begin(NUI_PRIM_LABEL);nui_primitive_t prev=active_primitive;active_primitive=NUI_PRIM_LABEL;draw_text_range(&key_layer,keys[i],0,(uint32_t)strlen(keys[i]),f,letter,x,y,color(o,NUI_STYLE_TEXT_COLOR,part),255);profile_end(NUI_PRIM_LABEL,t);active_primitive=prev;}}o->state=saved;}
static void chart_render(nui_obj_t *o,nui_layer_t *l,nui_area_t a){int w=nui_area_get_width(&a),h=nui_area_get_height(&a);nui_draw_line_dsc_t line;nui_draw_line_dsc_init(&line);line.color=color(o,NUI_STYLE_LINE_COLOR,0);line.width=num(o,NUI_STYLE_LINE_WIDTH,0);line.opa=(uint8_t)num(o,NUI_STYLE_LINE_OPA,0);if(line.opa){for(unsigned i=0;i<o->hdiv;i++){int y=a.y1+(h-1)*(int)i/MAX(1,(int)o->hdiv-1);line.p1=(nui_point_precise_t){(float)a.x1,(float)y};line.p2=(nui_point_precise_t){(float)a.x2,(float)y};nui_draw_line(l,&line);}for(unsigned i=0;i<o->vdiv;i++){int x=a.x1+(w-1)*(int)i/MAX(1,(int)o->vdiv-1);line.p1=(nui_point_precise_t){(float)x,(float)a.y1};line.p2=(nui_point_precise_t){(float)x,(float)a.y2};nui_draw_line(l,&line);}}
    line.width=num(o,NUI_STYLE_LINE_WIDTH,NUI_PART_ITEMS);line.opa=(uint8_t)num(o,NUI_STYLE_LINE_OPA,NUI_PART_ITEMS);if(!line.opa)return;for(nui_chart_series_t *s=o->series;s;s=s->next){if(s->hidden||!s->y_points)continue;line.color=s->color;for(unsigned i=1;i<o->point_count;i++){int32_t v0=s->y_points[i-1],v1=s->y_points[i];if(v0==NUI_CHART_POINT_NONE||v1==NUI_CHART_POINT_NONE)continue;int x0=a.x1+(int)((uint64_t)(w-1)*(i-1)/MAX(1u,o->point_count-1)),x1=a.x1+(int)((uint64_t)(w-1)*i/MAX(1u,o->point_count-1));if(x1<l->clip.x1||x0>l->clip.x2)continue;int y0=a.y2-(int)((int64_t)(v0-o->ymin)*(h-1)/MAX(1,o->ymax-o->ymin)),y1=a.y2-(int)((int64_t)(v1-o->ymin)*(h-1)/MAX(1,o->ymax-o->ymin));line.p1=(nui_point_precise_t){(float)x0,(float)y0};line.p2=(nui_point_precise_t){(float)x1,(float)y1};nui_draw_line(l,&line);}}}
/* A visible opaque descendant can cover the whole dirty rectangle. Skip
 * everything underneath it, while still painting later siblings in Z order.
 * This is retained occlusion, not a second full-size composition layer. */
static bool opaque_cover(nui_obj_t *o,nui_area_t dirty){
    if(!nui_obj_is_visible(o)||num(o,NUI_STYLE_OPA,0)!=255)return false;
    nui_area_t a;nui_obj_get_coords(o,&a);if(!area_contains(a,dirty))return false;
    for(nui_obj_t *c=o->last;c;c=c->prev)if(opaque_cover(c,dirty))return true;
    if(is_kind(o,7)&&o->image&&o->image->header.cf==NUI_COLOR_FORMAT_RGB565&&num(o,NUI_STYLE_IMAGE_OPA,0)==255)return true;
    if(num(o,NUI_STYLE_BG_OPA,0)!=255)return false;
    int r=num(o,NUI_STYLE_RADIUS,0);r=MAX(0,MIN(r,MIN(nui_area_get_width(&a),nui_area_get_height(&a))/2));
    return !r||(dirty.x1>=a.x1+r&&dirty.x2<=a.x2-r)||(dirty.y1>=a.y1+r&&dirty.y2<=a.y2-r);
}
/* Paint-inert-on-press is NOT permission to suppress draw events. Only an
 * explicit occlusion audit may opt a callback subtree out of this guard. */
static bool occlusion_callbacks_required(const nui_obj_t *o){
    if(!nui_obj_is_visible(o)||(o->flags&NUI_OBJ_FLAG_OCCLUSION_PAINT_INERT))return false;
    for(handler *h=o->handlers;h;h=h->next)
        if(h->code==NUI_EVENT_DRAW_MAIN||h->code==NUI_EVENT_DRAW_POST||h->code==NUI_EVENT_ALL||
           (h->code==NUI_EVENT_DRAW_TASK_ADDED&&(o->flags&NUI_OBJ_FLAG_SEND_DRAW_TASK_EVENTS)))return true;
    for(nui_obj_t *c=o->first;c;c=c->next)if(occlusion_callbacks_required(c))return true;
    return false;
}
static void object_render(nui_obj_t *o,nui_layer_t parent){if(!nui_obj_is_visible(o))return;nui_area_t a,visual;nui_obj_get_coords(o,&a);visual=object_visual(o);nui_area_t check;if(!area_intersect(&check,parent.clip,visual))return;nui_layer_t l=parent;l.opa=(uint8_t)((unsigned)parent.opa*(unsigned)num(o,NUI_STYLE_OPA,0)/255);if(!l.opa)return;nui_obj_t *saved_source=draw_source;draw_source=o;
    nui_obj_t *cover=NULL;if(l.opa==255)for(nui_obj_t *c=o->last;c;c=c->prev)if(opaque_cover(c,l.clip)){cover=c;break;}
    if(cover)for(nui_obj_t *c=o->first;c&&c!=cover;c=c->next)if(occlusion_callbacks_required(c)){cover=NULL;break;}
    if(!cover)object_box(o,&l,a,0);
    nui_layer_t inner=l;if(!area_intersect(&inner.clip,l.clip,a)){draw_source=saved_source;return;}
    if(!cover){if(is_kind(o,1)||is_kind(o,4))text_render(o,&inner,a,0);
    else if(is_kind(o,7))image_render(o,&inner,a);
    else if(is_kind(o,5))keyboard_render(o,&inner);
    else if(is_kind(o,6))chart_render(o,&inner,a);
    else if(is_kind(o,8)){nui_area_t fill=a;fill.x2=fill.x1+(int)((int64_t)nui_area_get_width(&a)*(o->bar_value-o->bar_min)/MAX(1,o->bar_max-o->bar_min))-1;if(fill.x2>=fill.x1)object_box(o,&inner,fill,NUI_PART_INDICATOR);}
    else if(is_kind(o,3)){nui_area_t track=a;int radius=nui_area_get_height(&a)/2;nui_draw_rect_dsc_t d={.radius=radius,.bg_opa=255,.bg_color=color(o,NUI_STYLE_BG_COLOR,(o->state&NUI_STATE_CHECKED)?NUI_PART_INDICATOR:0)};nui_draw_rect(&inner,&d,&track);int inset=4,size=nui_area_get_height(&a)-inset*2;int x=(o->state&NUI_STATE_CHECKED)?a.x2-inset-size+1:a.x1+inset;nui_area_t knob={x,a.y1+inset,x+size-1,a.y1+inset+size-1};d.bg_color=color(o,NUI_STYLE_BG_COLOR,NUI_PART_KNOB);d.radius=NUI_RADIUS_CIRCLE;nui_draw_rect(&inner,&d,&knob);}
    }event_send(o,NUI_EVENT_DRAW_MAIN,NULL,&inner);if(o->deleted){draw_source=saved_source;return;}for(nui_obj_t *c=cover?cover:o->first;c;){nui_obj_t *next=c->next;object_render(c,inner);c=next;}event_send(o,NUI_EVENT_DRAW_POST,NULL,&inner);draw_source=saved_source;}
void nui_native_set_flush_cb(nui_native_flush_cb_t cb,void *user){n.flush=cb;n.flush_user=user;}
void nui_native_set_clock_cb(uint64_t (*cb)(void)){n.clock_us=cb;}
void nui_native_profile_snapshot(nui_native_stats_t *out){if(out){*out=n.stats;out->shadow_cache_bytes=shadow_cache_bytes+sizeof(shadow_cache);out->shadow_cache_peak_bytes=shadow_cache_peak_bytes+sizeof(shadow_cache);out->shadow_cache_capacity_bytes=SHADOW_CACHE_LIMIT+sizeof(shadow_cache);out->shadow_cache_static_bytes=sizeof(shadow_cache);out->object_cache_bytes=cache_objects*OBJECT_CACHE_BYTES;out->object_cache_peak_bytes=cache_objects_peak*OBJECT_CACHE_BYTES;out->object_cache_bytes_per_object=OBJECT_CACHE_BYTES;out->gradient_cache_bytes=sizeof(gradient_cache);out->gradient_cache_peak_bytes=sizeof(gradient_cache);out->gradient_cache_capacity_bytes=sizeof(gradient_cache);out->gradient_cache_hits=gradient_cache.hits;out->gradient_cache_misses=gradient_cache.misses;out->gradient_cache_build_pixels=gradient_cache.build_pixels;}}
void nui_native_profile_reset(void){size_t bytes=n.stats.allocation_bytes,peak=n.stats.allocation_peak,count=n.stats.allocation_count;uint64_t fail=n.stats.allocation_failures;memset(&n.stats,0,sizeof(n.stats));n.stats.allocation_bytes=bytes;n.stats.allocation_peak=peak;n.stats.allocation_count=count;n.stats.allocation_failures=fail;gradient_cache.hits=gradient_cache.misses=gradient_cache.build_pixels=0;}
size_t nui_native_get_dirty(nui_area_t *out,size_t cap){size_t count=MIN(cap,n.rendered_count);if(out&&count)memcpy(out,n.rendered,count*sizeof(*out));return n.rendered_count;}
size_t nui_native_render(void){if(!n.fb||!n.root||!n.dirty_count){n.rendered_count=0;return 0;}uint64_t start=now_us();n.rendered_count=n.dirty_count;memcpy(n.rendered,n.dirty,n.dirty_count*sizeof(nui_area_t));n.dirty_count=0;n.rendering=true;for(size_t i=0;i<n.rendered_count;i++){nui_area_t a=n.rendered[i];nui_layer_t layer={a,255};active_primitive=NUI_PRIM_FILL;nui_area_t root_bounds;nui_obj_get_coords(n.root,&root_bounds);
    bool root_opaque=nui_obj_is_visible(n.root)&&num(n.root,NUI_STYLE_BG_OPA,0)==255&&num(n.root,NUI_STYLE_OPA,0)==255&&num(n.root,NUI_STYLE_RADIUS,0)==0&&area_contains(root_bounds,a);
    if(root_opaque)FINE_COUNT(n.stats.clear_pixels_skipped+=(uint64_t)nui_area_get_width(&a)*nui_area_get_height(&a));
    else for(int y=a.y1;y<=a.y2;y++){uint16_t *row=n.fb+(size_t)y*n.width+a.x1;memset(row,0,(size_t)nui_area_get_width(&a)*2);}
    object_render(n.root,layer);n.stats.dirty_pixels+=(uint64_t)nui_area_get_width(&a)*nui_area_get_height(&a);}n.rendering=false;garbage_collect();n.stats.frames++;n.stats.dirty_rects+=n.rendered_count;uint64_t end=now_us();if(end>=start)n.stats.render_us+=end-start;/* Callback time is excluded from render_us; platform measures transport. */if(n.flush)for(size_t i=0;i<n.rendered_count;i++){nui_area_t *a=&n.rendered[i];n.flush(a,n.fb+(size_t)a->y1*n.width+a->x1,(size_t)n.width,n.flush_user);}return n.rendered_count;}
void nui_native_deinit(void){gradient_cache.valid=false;shadow_cache_clear();if(n.root)nui_obj_delete(n.root);garbage_collect();while(n.timers){nui_timer_t *t=n.timers;n.timers=t->next;mem_free(t);}if(n.own_fb)mem_free(n.fb);n.fb=NULL;n.own_fb=false;n.dirty_count=n.rendered_count=0;n.capture=n.focus=NULL;}
bool nui_native_init(int width,int height,uint16_t *fb){if(width<=0||height<=0||(size_t)width>SIZE_MAX/2/(size_t)height)return false;nui_native_deinit();n.width=width;n.height=height;n.ticks=0;n.input=(nui_indev_t){0};if(!fb){fb=mem_alloc((size_t)width*height*2);if(!fb)return false;n.own_fb=true;}n.fb=fb;n.root=object_new(NULL,&nui_obj_class);if(!n.root){if(n.own_fb)mem_free(fb);n.fb=NULL;return false;}n.root->w=width;n.root->h=height;n.root->flags=0;nui_obj_set_style_bg_opa(n.root,255,0);nui_obj_set_style_bg_color(n.root,0,0);nui_obj_invalidate(n.root);return true;}
nui_obj_t *nui_screen_active(void){if(!n.root)nui_native_init(1280,720,NULL);return n.root;}
void nui_init(void){if(!n.root)nui_native_init(1280,720,NULL);}
void nui_deinit(void){nui_native_deinit();}
