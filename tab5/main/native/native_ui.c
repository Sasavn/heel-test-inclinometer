/* Independent retained renderer, written for the Tab5 UI contract. */
#include "native_ui.h"
#include "native_gradient.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
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
typedef struct surface_storage {
    uint16_t *landscape,*portrait;void *landscape_raw,*portrait_raw;
    size_t landscape_bytes,portrait_bytes;int w,h,pw,ph;
    unsigned pins;bool owned,portrait_valid,portrait_full,portrait_failed;
    uint64_t id,portrait_generation,generation,failed_generation;nui_area_t portrait_damage;
} surface_storage;
typedef struct retained_layer {
    uint16_t *pixels;size_t bytes;int w,h;surface_storage *storage;
    uint64_t style,geometry;nui_color_t matte;
    nui_area_t damage;bool parent_matte,valid,failed,damage_valid;
} retained_layer;
struct nui_render_target {uint16_t *pixels;int stride,origin_x,origin_y;};
#define LAYER_CACHE_LIMIT (8u*1024u*1024u)
#ifndef NUI_LAYER_CACHE_OCCLUSION
#define NUI_LAYER_CACHE_OCCLUSION 1
#endif
/* Scoped to logical display passes; offscreen targets always use legacy
 * occlusion. Edge passes deliberately use the unchanged ordinary painter. */
static bool cache_occlusion_pass,cache_occlusion_edges;
static nui_layer_cache_stats_t layer_stats={.limit_bytes=LAYER_CACHE_LIMIT};
static unsigned layer_cache_requests;
static nui_surface_defer_cb_t surface_defer_cb;static void *surface_defer_user;
static unsigned dirty_alignment=1,deferred_accepts,surface_alignment=16;
static uint64_t framebuffer_epoch=1,surface_generation,surface_storage_id;
typedef struct surface_lease {surface_storage *storage;nui_surface_view_t view;uint64_t framebuffer_epoch;} surface_lease;
static surface_lease surface_leases[16];

struct nui_obj {
    /* Position-independent label measurement: bounded 16-byte payload in the
     * existing PSRAM object allocation. geometry_epoch keys resolved font,
     * spacing, padding, border and inherited/shared/state geometry styles;
     * text/size/mode setters explicitly invalidate the remaining inputs. */
    uint64_t measure_geometry;nui_point_t measure_size;
    uint64_t coords_epoch;uint64_t coords_style_epoch;nui_area_t coords_cache;
    uint64_t cache_epoch;uint32_t cache_state,cache_part;uint64_t cache_valid;nui_style_value_t main_cache[34];
    retained_layer *layer_cache;
    size_t potential_draw_handlers; /* Own + descendants, including hidden/flag-off TASK. */
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
    int width,height;uint16_t *fb,*owned_fb;uint32_t ticks;unsigned props,event_depth;bool rendering;
    nui_obj_t *root,*capture,*focus,*garbage;nui_indev_t input;nui_timer_t *timers;
    nui_area_t dirty[MAX_DIRTY],rendered[MAX_DIRTY];size_t dirty_count,rendered_count;
    nui_native_flush_cb_t flush;void *flush_user;uint64_t (*clock_us)(void);nui_native_stats_t stats;
} n;
typedef union {max_align_t alignment;size_t size;} allocation_header;
static void *mem_alloc(size_t size){
    allocation_header *h;
    /* Retained objects/styles/text and render caches are CPU-only data.
     * Default calloc prefers internal RAM for these small allocations, starving
     * the later 16*3072-byte service queue and internal-only task stacks.
     * Keep them in PSRAM; do not fall back to scarce internal/DMA memory. */
#ifdef ESP_PLATFORM
    h=heap_caps_calloc(1,sizeof(*h)+size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    h=calloc(1,sizeof(*h)+size);
#endif
    if(!h){n.stats.allocation_failures++;return NULL;}
    h->size=size;n.stats.allocation_bytes+=size;n.stats.allocation_count++;
    if(n.stats.allocation_bytes>n.stats.allocation_peak)n.stats.allocation_peak=n.stats.allocation_bytes;
    return h+1;
}
static void mem_free(void *p){if(p){allocation_header *h=(allocation_header*)p-1;n.stats.allocation_bytes-=h->size;n.stats.allocation_count--;free(h);}}
static size_t payload64(size_t bytes){return (bytes+63u)&~(size_t)63u;}
static void *aligned_payload(size_t bytes,void **raw){
    *raw=mem_alloc(bytes+63u);if(!*raw)return NULL;
    return (void*)(((uintptr_t)*raw+63u)&~(uintptr_t)63u);
}
static void storage_free(surface_storage *st){
    assert(!st->pins&&!st->owned);layer_stats.bytes-=st->landscape_bytes+st->portrait_bytes;
    mem_free(st->landscape_raw);mem_free(st->portrait_raw);mem_free(st);
}
static surface_storage *storage_create(int w,int h){
    size_t bytes=payload64((size_t)w*h*2);
    if(bytes>LAYER_CACHE_LIMIT-layer_stats.bytes)return NULL;
    surface_storage *st=mem_alloc(sizeof(*st));if(!st)return NULL;
    st->landscape=aligned_payload(bytes,&st->landscape_raw);
    if(!st->landscape){mem_free(st);return NULL;}
    st->landscape_bytes=bytes;st->w=w;st->h=h;st->owned=true;st->portrait_full=true;
    st->id=++surface_storage_id;assert(st->id);
    st->generation=++surface_generation;layer_stats.bytes+=bytes;layer_stats.peak_bytes=MAX(layer_stats.peak_bytes,layer_stats.bytes);return st;
}
static void layer_cache_release_pixels(retained_layer *c){
    surface_storage *st=c->storage;c->storage=NULL;c->pixels=NULL;c->bytes=0;c->valid=false;c->damage_valid=false;
    if(st){st->owned=false;if(!st->pins)storage_free(st);}
}
static void layer_cache_destroy(nui_obj_t *o){
    if(!o->layer_cache)return;
    layer_cache_release_pixels(o->layer_cache);mem_free(o->layer_cache);o->layer_cache=NULL;layer_cache_requests--;
}
static void layer_cache_invalidate_all_for(nui_obj_t *o){
    if(!layer_cache_requests)return;
    for(;o;o=o->parent)if(o->layer_cache){o->layer_cache->valid=false;o->layer_cache->failed=false;o->layer_cache->damage_valid=false;}
}
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
static void storage_changed(surface_storage *st,nui_area_t local,bool full){
    assert(!st->pins);st->generation=++surface_generation;st->portrait_failed=false;
    if(full||!st->portrait){st->portrait_full=true;st->portrait_damage=(nui_area_t){0,0,st->w-1,st->h-1};}
    else if(st->portrait_valid)st->portrait_damage=local;
    else st->portrait_damage=(nui_area_t){MIN(st->portrait_damage.x1,local.x1),MIN(st->portrait_damage.y1,local.y1),MAX(st->portrait_damage.x2,local.x2),MAX(st->portrait_damage.y2,local.y2)};
    st->portrait_valid=false;
}
static bool storage_portrait(surface_storage *st){
    if(st->portrait_valid)return true;
    if(st->pins||(st->portrait_failed&&st->failed_generation==st->generation))return false;
    if(!st->portrait){
        st->pw=(st->h+31)&~31;st->ph=(st->w+15)&~15;
        if(st->pw>16383||st->ph>16383)return false;
        size_t bytes=(size_t)st->pw*st->ph*2;
        if(bytes>LAYER_CACHE_LIMIT-layer_stats.bytes||(st->portrait=aligned_payload(bytes,&st->portrait_raw))==NULL){
            st->portrait_failed=true;st->failed_generation=st->generation;return false;
        }
        st->portrait_bytes=bytes;layer_stats.bytes+=bytes;layer_stats.peak_bytes=MAX(layer_stats.peak_bytes,layer_stats.bytes);st->portrait_full=true;
    }
    nui_area_t a=st->portrait_full?(nui_area_t){0,0,st->w-1,st->h-1}:st->portrait_damage;
    uint64_t start=now_us();
    /* 16x16 tiled transpose, exact RGB565. Only changed logical samples update. */
    for(int by=a.y1;by<=a.y2;by+=16)for(int bx=a.x1;bx<=a.x2;bx+=16)
        for(int x=bx;x<=MIN(bx+15,a.x2);x++)for(int y=by;y<=MIN(by+15,a.y2);y++)
            st->portrait[(size_t)(st->w-1-x)*st->pw+y]=st->landscape[(size_t)y*st->w+x];
    uint64_t end=now_us();if(end>=start)layer_stats.portrait_us+=end-start;
    st->portrait_generation++;assert(st->portrait_generation);
    layer_stats.portrait_updates++;st->portrait_valid=true;st->portrait_full=false;return true;
}
static surface_lease *view_lease(const nui_surface_view_t *v){
    if(!v||!v->lease)return NULL;
    uintptr_t ptr=(uintptr_t)v->lease,begin=(uintptr_t)surface_leases;
    if(ptr<begin||ptr>=begin+sizeof(surface_leases)||(ptr-begin)%sizeof(surface_lease))return NULL;
    surface_lease *lease=(surface_lease*)v->lease;
    if(!lease->storage||v->storage_id!=lease->view.storage_id||v->portrait_generation!=lease->view.portrait_generation||
        v->landscape!=lease->view.landscape||v->portrait!=lease->view.portrait)return NULL;
    return lease;
}
void nui_native_set_surface_defer_cb(nui_surface_defer_cb_t cb,void *user){surface_defer_cb=cb;surface_defer_user=user;}
bool nui_native_set_dirty_alignment(unsigned tile){if(tile!=1&&tile!=16)return false;dirty_alignment=tile;return true;}
bool nui_native_set_surface_alignment(unsigned tile){if(tile!=1&&tile!=16)return false;surface_alignment=tile;return true;}
unsigned nui_native_surface_alignment(void){return surface_alignment;}

void nui_surface_view_release(nui_surface_view_t *v){
    surface_lease *lease=view_lease(v);
    if(lease){surface_storage *st=lease->storage;lease->storage=NULL;memset(&lease->view,0,sizeof(lease->view));assert(st->pins);st->pins--;layer_stats.pins--;
        if(!st->pins&&!st->owned)storage_free(st);
    }
    if(v)memset(v,0,sizeof(*v));
}
bool nui_surface_view_materialize(const nui_surface_view_t *v){
    surface_lease *lease=view_lease(v);
    if(!lease||lease->framebuffer_epoch!=framebuffer_epoch||!n.fb)return false;
    const nui_surface_view_t *held=&lease->view;surface_storage *st=lease->storage;nui_area_t d=held->destination;
    int64_t w=(int64_t)d.x2-d.x1+1,h=(int64_t)d.y2-d.y1+1;
    if(w<=0||h<=0||d.x1<0||d.y1<0||d.x2>=n.width||d.y2>=n.height||
       (uint64_t)held->source_x+w>(unsigned)st->w||(uint64_t)held->source_y+h>(unsigned)st->h)return false;
    for(int64_t y=0;y<h;y++)memcpy(n.fb+(size_t)(d.y1+y)*n.width+d.x1,
        st->landscape+(size_t)(held->source_y+y)*st->w+held->source_x,(size_t)w*2);
    layer_stats.materialize_pixels+=(uint64_t)w*h;return true;
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
static uint64_t style_epoch=1,layout_epoch=1,geometry_epoch=1;
/* Cache payload is embedded in each retained object and already included in allocations. */
#define OBJECT_CACHE_BYTES (offsetof(nui_obj_t,class_p))
static size_t cache_objects,cache_objects_peak;
static bool inherited(nui_style_prop_t p){return p==NUI_STYLE_TEXT_COLOR||p==NUI_STYLE_TEXT_FONT||p==NUI_STYLE_TEXT_LETTER_SPACE||p==NUI_STYLE_TEXT_LINE_SPACE||p==NUI_STYLE_TEXT_ALIGN;}
/* Paint-only local edits must not evict unrelated objects or coordinates.
 * Geometry and inheritance stay conservative at shared-generation boundaries. */
static bool geometry_prop(nui_style_prop_t p){return p==NUI_STYLE_BORDER_WIDTH||
    (p>=NUI_STYLE_PAD_TOP&&p<=NUI_STYLE_PAD_COLUMN)||p==NUI_STYLE_TEXT_FONT||
    p==NUI_STYLE_TEXT_LETTER_SPACE||p==NUI_STYLE_TEXT_LINE_SPACE||
    p==NUI_STYLE_WIDTH||p==NUI_STYLE_HEIGHT;}
static void local_style_changed(nui_obj_t *o,nui_style_prop_t p){
    layer_cache_invalidate_all_for(o);
    if(inherited(p))style_epoch++;else o->cache_epoch=0;
    if(geometry_prop(p)){o->measure_geometry=0;geometry_epoch++;}
}
static void state_style_changed(nui_obj_t *o,uint32_t changed){
    bool inherit_change=false,geometry_change=false;
    for(nui_style_entry_t *e=o->local;e;e=e->next)if(e->selector&changed){
        inherit_change|=inherited(e->prop);geometry_change|=geometry_prop(e->prop);
    }
    for(style_ref *r=o->styles;r;r=r->next)for(nui_style_entry_t *e=r->style->entries;e;e=e->next)
        if((r->selector|e->selector)&changed){inherit_change|=inherited(e->prop);geometry_change|=geometry_prop(e->prop);}
    if(inherit_change)style_epoch++;else o->cache_epoch=0;
    if(geometry_change)geometry_epoch++;
}
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
static bool entry_set(nui_style_entry_t **head,nui_style_prop_t p,nui_style_selector_t sel,nui_style_value_t v){for(nui_style_entry_t *e=*head;e;e=e->next)if(e->prop==p&&e->selector==sel){if(!memcmp(&e->value,&v,sizeof(v)))return false;e->value=v;return true;}nui_style_entry_t *e=mem_alloc(sizeof(*e));if(!e)return false;*e=(nui_style_entry_t){p,sel,v,*head};*head=e;return true;}
void nui_style_init(nui_style_t *s){s->entries=NULL;}
void nui_style_reset(nui_style_t *s){if(s){entries_free(s->entries);s->entries=NULL;style_epoch++;geometry_epoch++;}}
void nui_style_set_prop(nui_style_t *s,nui_style_prop_t p,nui_style_value_t v){if(s&&entry_set(&s->entries,p,0,v)){style_epoch++;if(geometry_prop(p))geometry_epoch++;}}
nui_style_prop_t nui_style_register_prop(unsigned flags){(void)flags;return 1024+(++n.props);}
unsigned nui_style_get_num_custom_props(void){return n.props;}
int nui_obj_get_local_style_prop(const nui_obj_t *o,nui_style_prop_t p,nui_style_value_t *v,nui_style_selector_t sel){return o&&entry_find(o->local,p,sel,v,true)?NUI_STYLE_RES_FOUND:NUI_STYLE_RES_NOT_FOUND;}
void nui_obj_set_local_style_prop(nui_obj_t *o,nui_style_prop_t p,nui_style_value_t v,nui_style_selector_t sel){if(o&&!o->deleted&&entry_set(&o->local,p,sel,v)){local_style_changed(o,p);nui_obj_invalidate(o);}}
void nui_obj_add_style(nui_obj_t *o,const nui_style_t *s,nui_style_selector_t sel){if(!o||!s)return;for(style_ref *r=o->styles;r;r=r->next)if(r->style==s&&r->selector==sel)return;style_ref *r=mem_alloc(sizeof(*r));if(r){*r=(style_ref){s,sel,o->styles};o->styles=r;style_epoch++;geometry_epoch++;nui_obj_invalidate(o);}}
/* Replace only an unambiguous MAIN reference, retaining its list position.
 * Resolve without caches so the transient removed-style inheritance never
 * reaches either the framebuffer or global cache generations. */
bool nui_obj_replace_style_atomic(nui_obj_t *o,const nui_style_t *old_style,const nui_style_t *new_style,nui_style_selector_t sel){
    if(!o||o->deleted||!old_style||!new_style||sel!=NUI_PART_MAIN||!o->styles||
       o->styles->next||o->styles->selector!=sel||o->styles->style!=old_style)return false;
    if(old_style==new_style)return true;
    nui_style_value_t before[34];
    for(nui_style_prop_t p=1;p<34;p++)if(inherited(p)||geometry_prop(p))
        if(!prop_find(o,p,sel,&before[p]))before[p]=default_prop(p);
    nui_obj_invalidate(o);
    o->styles->style=new_style;
    bool inherit_change=false,geometry_change=false;
    for(nui_style_prop_t p=1;p<34;p++)if(inherited(p)||geometry_prop(p)){
        nui_style_value_t after;if(!prop_find(o,p,sel,&after))after=default_prop(p);
        bool changed=p==NUI_STYLE_TEXT_FONT?before[p].ptr!=after.ptr:before[p].num!=after.num;
        if(changed){inherit_change|=inherited(p);geometry_change|=geometry_prop(p);}
    }
    o->cache_epoch=0;
    if(inherit_change)style_epoch++;
    if(geometry_change)geometry_epoch++;
    nui_obj_invalidate(o);
    return true;
}
void nui_obj_remove_style(nui_obj_t *o,const nui_style_t *s,nui_style_selector_t sel){if(!o)return;style_ref **r=&o->styles;while(*r){if((!s||(*r)->style==s)&&(*r)->selector==sel){style_ref *dead=*r;*r=dead->next;mem_free(dead);style_epoch++;geometry_epoch++;nui_obj_invalidate(o);}else r=&(*r)->next;}}
void nui_obj_remove_style_all(nui_obj_t *o){if(!o)return;entries_free(o->local);o->local=NULL;while(o->styles){style_ref *r=o->styles;o->styles=r->next;mem_free(r);}style_epoch++;geometry_epoch++;nui_obj_invalidate(o);}
void nui_obj_report_style_change(const nui_style_t *s){(void)s;style_epoch++;geometry_epoch++;if(n.root)nui_obj_invalidate(n.root);}
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
static void content_size(const nui_obj_t *o,int *w,int *h){
    bool cache_label=is_kind(o,1);
    if(cache_label&&o->measure_geometry==geometry_epoch){*w=o->measure_size.x;*h=o->measure_size.y;return;}
    nui_point_t size={0,0};if(is_kind(o,1)||is_kind(o,4)){nui_text_get_size(&size,o->text?o->text:"",font(o,0),num(o,NUI_STYLE_TEXT_LETTER_SPACE,0),num(o,NUI_STYLE_TEXT_LINE_SPACE,0),o->w>0?MAX(1,o->w-num(o,NUI_STYLE_PAD_LEFT,0)-num(o,NUI_STYLE_PAD_RIGHT,0)):NUI_COORD_MAX,(o->w==NUI_SIZE_CONTENT||o->one_line)?NUI_TEXT_FLAG_EXPAND:0);}else if(is_kind(o,7)&&o->image){size.x=o->image->header.w;size.y=o->image->header.h;}else{for(nui_obj_t *c=o->first;c;c=c->next){size.x=MAX(size.x,c->x+nui_obj_get_width(c));size.y=MAX(size.y,c->y+nui_obj_get_height(c));}}*w=size.x+num(o,NUI_STYLE_PAD_LEFT,0)+num(o,NUI_STYLE_PAD_RIGHT,0)+2*num(o,NUI_STYLE_BORDER_WIDTH,0);*h=size.y+num(o,NUI_STYLE_PAD_TOP,0)+num(o,NUI_STYLE_PAD_BOTTOM,0)+2*num(o,NUI_STYLE_BORDER_WIDTH,0);
    if(cache_label){nui_obj_t *mutable=(nui_obj_t*)o;mutable->measure_size=(nui_point_t){*w,*h};mutable->measure_geometry=geometry_epoch;}
}
int32_t nui_obj_get_width(const nui_obj_t *o){if(!o||o->deleted)return 0;if(o->w!=NUI_SIZE_CONTENT)return MAX(0,o->w);int w,h;content_size(o,&w,&h);return w;}
int32_t nui_obj_get_height(const nui_obj_t *o){if(!o||o->deleted)return 0;if(o->h!=NUI_SIZE_CONTENT)return MAX(0,o->h);int w,h;content_size(o,&w,&h);return h;}
void nui_obj_get_coords(const nui_obj_t *o,nui_area_t *a){if(!a)return;if(!o||o->deleted){*a=(nui_area_t){0,0,-1,-1};return;}if(o->coords_epoch==layout_epoch&&o->coords_style_epoch==geometry_epoch){*a=o->coords_cache;FINE_COUNT(n.stats.layout_cache_hits++);return;}if(o->coords_epoch)FINE_COUNT(n.stats.layout_cache_evictions++);int w=nui_obj_get_width(o),h=nui_obj_get_height(o),x=o->x,y=o->y;if(o->parent){nui_area_t p;nui_obj_get_coords(o->parent,&p);int border=num(o->parent,NUI_STYLE_BORDER_WIDTH,0),left=num(o->parent,NUI_STYLE_PAD_LEFT,0),top=num(o->parent,NUI_STYLE_PAD_TOP,0);
if(o->center_x)x=(nui_obj_get_width(o->parent)-2*border-left-num(o->parent,NUI_STYLE_PAD_RIGHT,0)-w)/2;
if(o->center_y)y=(nui_obj_get_height(o->parent)-2*border-top-num(o->parent,NUI_STYLE_PAD_BOTTOM,0)-h)/2;
x+=p.x1+border+left;y+=p.y1+border+top;}*a=(nui_area_t){x,y,x+w-1,y+h-1};((nui_obj_t*)o)->coords_cache=*a;((nui_obj_t*)o)->coords_epoch=layout_epoch;((nui_obj_t*)o)->coords_style_epoch=geometry_epoch;FINE_COUNT(n.stats.layout_cache_misses++);}
bool nui_obj_is_visible(const nui_obj_t *o){if(!o)return false;for(;o;o=o->parent)if(o->deleted||(o->flags&NUI_OBJ_FLAG_HIDDEN))return false;return true;}
static nui_area_t object_visual(const nui_obj_t *o){nui_area_t a;nui_obj_get_coords(o,&a);int ext=MAX(num(o,NUI_STYLE_SHADOW_WIDTH,0)+abs(num(o,NUI_STYLE_SHADOW_OFFSET_Y,0)),num(o,NUI_STYLE_OUTLINE_WIDTH,0)+num(o,NUI_STYLE_OUTLINE_PAD,0));a.x1-=ext;a.y1-=ext;a.x2+=ext;a.y2+=ext;return a;}
void nui_native_invalidate(const nui_area_t *area){if(!n.fb||!area)return;nui_area_t a;if(!area_intersect(&a,*area,(nui_area_t){0,0,n.width-1,n.height-1}))return;for(size_t i=0;i<n.dirty_count;){nui_area_t existing=n.dirty[i],isect;if(area_contains(existing,a))return;if(area_intersect(&isect,a,existing)||area_contains(a,existing)){a=(nui_area_t){MIN(a.x1,existing.x1),MIN(a.y1,existing.y1),MAX(a.x2,existing.x2),MAX(a.y2,existing.y2)};n.dirty[i]=n.dirty[--n.dirty_count];i=0;}else i++;}if(n.dirty_count==MAX_DIRTY){n.dirty_count=1;n.dirty[0]=(nui_area_t){0,0,n.width-1,n.height-1};}else n.dirty[n.dirty_count++]=a;}
static void layer_cache_damage(nui_obj_t *o,bool include_self){
    if(!layer_cache_requests||!o)return;
    nui_area_t visual={0};bool have_visual=false;
    for(nui_obj_t *p=include_self?o:o->parent;p;p=p->parent)if(p->layer_cache){
        retained_layer *c=p->layer_cache;c->failed=false;
        if(p==o){c->valid=false;c->damage_valid=false;continue;}
        if(!c->valid)continue;
        if(!have_visual){visual=object_visual(o);have_visual=true;}
        nui_area_t bounds,local;nui_obj_get_coords(p,&bounds);
        if(!area_intersect(&local,visual,bounds))continue;
        local.x1-=bounds.x1;local.x2-=bounds.x1;local.y1-=bounds.y1;local.y2-=bounds.y1;
        if(c->damage_valid)c->damage=(nui_area_t){MIN(c->damage.x1,local.x1),MIN(c->damage.y1,local.y1),MAX(c->damage.x2,local.x2),MAX(c->damage.y2,local.y2)};
        else{c->damage=local;c->damage_valid=true;}
    }
}
static void object_invalidate_screen(nui_obj_t *o){if(!nui_obj_is_visible(o))return;nui_area_t a=object_visual(o);for(nui_obj_t *p=o->parent;p;p=p->parent){nui_area_t bounds;nui_obj_get_coords(p,&bounds);if(!area_intersect(&a,a,bounds))return;}nui_native_invalidate(&a);}
void nui_obj_invalidate(nui_obj_t *o){layer_cache_damage(o,true);object_invalidate_screen(o);}
static void object_invalidate_position(nui_obj_t *o){layer_cache_damage(o,false);object_invalidate_screen(o);}
static bool layer_cache_request(nui_obj_t *o,bool enabled,bool parent_matte){
    if(!o||o->deleted)return false;
    if(!enabled){layer_cache_destroy(o);nui_obj_invalidate(o);return true;}
    if(!o->layer_cache){o->layer_cache=mem_alloc(sizeof(*o->layer_cache));if(!o->layer_cache)return false;layer_cache_requests++;}
    o->layer_cache->parent_matte=parent_matte;nui_obj_invalidate(o);return true;
}
bool nui_obj_set_layer_cache(nui_obj_t *o,bool enabled){return layer_cache_request(o,enabled,false);}
bool nui_obj_set_layer_cache_parent_matte(nui_obj_t *o,bool enabled){return layer_cache_request(o,enabled,true);}
bool nui_obj_get_layer_cache_enabled(const nui_obj_t *o){return o&&!o->deleted&&o->layer_cache;}
void nui_obj_clear_layer_cache(nui_obj_t *o){if(o&&o->layer_cache){layer_cache_release_pixels(o->layer_cache);o->layer_cache->failed=false;nui_obj_invalidate(o);}}
void nui_layer_cache_get_stats(nui_layer_cache_stats_t *out){if(out)*out=layer_stats;}
void nui_layer_cache_reset_stats(void){size_t bytes=layer_stats.bytes;uint64_t pins=layer_stats.pins;memset(&layer_stats,0,sizeof(layer_stats));layer_stats.pins=pins;layer_stats.bytes=layer_stats.peak_bytes=bytes;layer_stats.limit_bytes=LAYER_CACHE_LIMIT;}

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
static bool potential_draw_code(nui_event_code_t code){
    return code==NUI_EVENT_DRAW_MAIN||code==NUI_EVENT_DRAW_POST||code==NUI_EVENT_ALL||code==NUI_EVENT_DRAW_TASK_ADDED;
}
static bool descends_from(nui_obj_t *o,nui_obj_t *ancestor){for(;o;o=o->parent)if(o==ancestor)return true;return false;}
static void object_release(nui_obj_t *o){layer_cache_destroy(o);while(o->handlers){handler *h=o->handlers;o->handlers=h->next;mem_free(h);}entries_free(o->local);while(o->styles){style_ref *r=o->styles;o->styles=r->next;mem_free(r);}while(o->series){nui_chart_series_t *s=o->series;o->series=s->next;if(s->owned)mem_free(s->y_points);mem_free(s);}mem_free(o->text);mem_free(o->accepted);mem_free(o->display_text);cache_objects--;mem_free(o);}
static void garbage_collect(void){if(n.event_depth||n.rendering)return;while(n.garbage){nui_obj_t *o=n.garbage;n.garbage=o->garbage_next;object_release(o);}}
void nui_obj_delete(nui_obj_t *o){if(!o||o->deleted)return;nui_obj_invalidate(o);if(descends_from(n.capture,o))n.capture=NULL;if(descends_from(n.focus,o))n.focus=NULL;while(o->first)nui_obj_delete(o->first);if(o->potential_draw_handlers){
    size_t count=o->potential_draw_handlers;
    for(nui_obj_t *p=o->parent;p;p=p->parent){assert(p->potential_draw_handlers>=count);p->potential_draw_handlers-=count;}
    o->potential_draw_handlers=0;
}if(o->parent){if(o->prev)o->prev->next=o->next;else o->parent->first=o->next;if(o->next)o->next->prev=o->prev;else o->parent->last=o->prev;}if(n.root==o)n.root=NULL;layout_epoch++;o->deleted=true;o->parent=o->next=o->prev=NULL;o->garbage_next=n.garbage;n.garbage=o;garbage_collect();}
void nui_obj_set_pos(nui_obj_t *o,int32_t x,int32_t y){if(!o||o->deleted||(o->x==x&&o->y==y&&!o->center_x&&!o->center_y))return;object_invalidate_position(o);layout_epoch++;o->x=x;o->y=y;o->center_x=o->center_y=false;object_invalidate_position(o);}
void nui_obj_set_size(nui_obj_t *o,int32_t w,int32_t h){if(!o||o->deleted||(o->w==w&&o->h==h))return;nui_obj_invalidate(o);layout_epoch++;o->measure_geometry=0;o->w=w;o->h=h;nui_obj_invalidate(o);}
void nui_obj_set_width(nui_obj_t *o,int32_t w){if(o)nui_obj_set_size(o,w,o->h);}
void nui_obj_set_height(nui_obj_t *o,int32_t h){if(o)nui_obj_set_size(o,o->w,h);}
void nui_obj_set_x(nui_obj_t *o,int32_t x){if(o)nui_obj_set_pos(o,x,o->y);}
void nui_obj_set_y(nui_obj_t *o,int32_t y){if(o)nui_obj_set_pos(o,o->x,y);}
void nui_obj_set_align(nui_obj_t *o,int align){if(!o)return;object_invalidate_position(o);layout_epoch++;o->center_x=o->center_y=align==NUI_ALIGN_CENTER;object_invalidate_position(o);}
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
void nui_obj_add_state(nui_obj_t *o,uint32_t state){if(o&&(o->state&state)!=state){bool skip=pressed_paint_inert(o,state);if(!skip)nui_obj_invalidate(o);uint32_t changed=state&~o->state;o->state|=state;if(!skip)state_style_changed(o,changed);if(!skip)nui_obj_invalidate(o);else FINE_COUNT(n.stats.skipped_state_invalidations++);}}
void nui_obj_remove_state(nui_obj_t *o,uint32_t state){if(o&&(o->state&state)){bool skip=pressed_paint_inert(o,state);if(!skip)nui_obj_invalidate(o);uint32_t changed=state&o->state;o->state&=~state;if(!skip)state_style_changed(o,changed);if(!skip)nui_obj_invalidate(o);else FINE_COUNT(n.stats.skipped_state_invalidations++);}}

bool nui_obj_has_state(const nui_obj_t *o,uint32_t state){return o&&(o->state&state)==state;}
void nui_obj_set_ext_click_area(nui_obj_t *o,int32_t v){if(o)o->ext=v;}
void nui_obj_add_event_cb(nui_obj_t *o,nui_event_cb_t cb,nui_event_code_t code,void *user){if(!o||o->deleted||!cb)return;handler *h=mem_alloc(sizeof(*h));if(!h)return;*h=(handler){cb,code,user,NULL};handler **tail=&o->handlers;while(*tail)tail=&(*tail)->next;*tail=h;if(potential_draw_code(code))for(nui_obj_t *p=o;p;p=p->parent)p->potential_draw_handlers++;layer_cache_invalidate_all_for(o);}
static void event_send(nui_obj_t *o,nui_event_code_t code,void *param,nui_layer_t *layer){if(!o||o->deleted)return;n.event_depth++;for(handler *h=o->handlers;h&&!o->deleted;){handler *next=h->next;if(h->code==NUI_EVENT_ALL||h->code==code){nui_event_t e={o,h->user,param,layer,code};h->cb(&e);}h=next;}n.event_depth--;garbage_collect();}
void nui_obj_send_event(nui_obj_t *o,nui_event_code_t code,void *param){event_send(o,code,param,NULL);}
nui_event_code_t nui_event_get_code(const nui_event_t *e){return e?e->code:NUI_EVENT_ALL;}
nui_obj_t *nui_event_get_target(const nui_event_t *e){return e?e->target:NULL;}
void *nui_event_get_user_data(const nui_event_t *e){return e?e->user_data:NULL;}
void *nui_event_get_param(const nui_event_t *e){return e?e->param:NULL;}
nui_layer_t *nui_event_get_layer(const nui_event_t *e){return e?e->layer:NULL;}
nui_draw_task_t *nui_event_get_draw_task(const nui_event_t *e){return e?e->param:NULL;}
nui_draw_task_type_t nui_draw_task_get_type(const nui_draw_task_t *t){return t?t->type:NUI_DRAW_TASK_TYPE_NONE;}
void nui_label_set_text(nui_obj_t *o,const char *text){if(!o||o->deleted)return;if(!text)text="";if(o->text&&!strcmp(o->text,text))return;char *copy=str_copy(text);if(!copy)return;nui_obj_invalidate(o);mem_free(o->text);mem_free(o->display_text);o->display_text=NULL;o->measure_geometry=0;o->text=copy;if(o->w==NUI_SIZE_CONTENT||o->h==NUI_SIZE_CONTENT)layout_epoch++;o->text_epoch=n.ticks;nui_obj_invalidate(o);}
const char *nui_label_get_text(const nui_obj_t *o){return o?(o->display_text?o->display_text:o->text?o->text:""):"";}
void nui_label_set_long_mode(nui_obj_t *o,int mode){if(o&&o->long_mode!=mode){o->measure_geometry=0;o->long_mode=mode;nui_obj_invalidate(o);}}
void nui_textarea_set_text(nui_obj_t *o,const char *s){if(!o||o->deleted)return;bool changed=strcmp(nui_label_get_text(o),s?s:"")!=0;nui_label_set_text(o,s);o->cursor=(uint32_t)strlen(nui_label_get_text(o));if(changed){o->blink_epoch=n.ticks;nui_obj_send_event(o,NUI_EVENT_VALUE_CHANGED,NULL);}}
const char *nui_textarea_get_text(const nui_obj_t *o){return nui_label_get_text(o);}
void nui_textarea_set_one_line(nui_obj_t *o,bool v){if(o&&o->one_line!=v){nui_obj_invalidate(o);layout_epoch++;o->measure_geometry=0;o->one_line=v;if(v)o->h=NUI_SIZE_CONTENT;nui_obj_invalidate(o);}}
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
static uint16_t *layer_buffer(const nui_layer_t *l){return l->target?l->target->pixels:n.fb;}
static uint16_t *layer_row(const nui_layer_t *l,int x,int y){
    if(l->target)return l->target->pixels+(size_t)(y-l->target->origin_y)*l->target->stride+(x-l->target->origin_x);
    return n.fb+(size_t)y*n.width+x;
}
static void pixel(nui_layer_t *l,int x,int y,uint16_t c,unsigned alpha){if(!layer_buffer(l)||x<l->clip.x1||x>l->clip.x2||y<l->clip.y1||y>l->clip.y2)return;alpha=alpha*l->opa/255;if(!alpha)return;uint16_t *p=layer_row(l,x,y);*p=blend565(*p,c,alpha);FINE_COUNT(n.stats.primitive[active_primitive].pixels++);}
/* Long opaque rows: prepare in internal task stack, copy whole blocks to
 * the framebuffer. memcpy handles two-byte/odd-pixel destination alignment;
 * no wider lvalue aliases RGB565 storage. Keep this stack off short spans. */
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void opaque_span_blocks(uint16_t *row,size_t count,uint16_t c){
    _Alignas(16) uint32_t pattern[64];uint32_t pair=(uint32_t)c|((uint32_t)c<<16);
    for(unsigned i=0;i<64;i++)pattern[i]=pair;
    while(count>=128){memcpy(row,pattern,sizeof(pattern));row+=128;count-=128;}
    if(count)memcpy(row,pattern,count*sizeof(*row));
}
static void opaque_span_fill(uint16_t *row,size_t count,uint16_t c){
    if((c&255u)==(c>>8))memset(row,c&255u,count*sizeof(*row));
    else if(count>=128)opaque_span_blocks(row,count,c);
    else for(size_t i=0;i<count;i++)row[i]=c;
}
static void span(nui_layer_t *l,int x1,int x2,int y,uint16_t c,unsigned alpha){if(y<l->clip.y1||y>l->clip.y2)return;x1=MAX(x1,l->clip.x1);x2=MIN(x2,l->clip.x2);if(x1>x2)return;alpha=alpha*l->opa/255;if(!alpha)return;uint16_t *row=layer_row(l,x1,y);if(alpha==255){opaque_span_fill(row,(size_t)(x2-x1+1),c);}else for(int x=x1;x<=x2;x++){*row=blend565(*row,c,alpha);row++;}FINE_COUNT(n.stats.primitive[active_primitive].pixels+=(uint64_t)(x2-x1+1));}
/* Coverage at 2x2 integer subpixels only in rounded corners. The central
 * spans use direct RGB565 stores; no mask/layer/framebuffer allocations. */
static unsigned round_coverage(nui_area_t a,int r,int x,int y){if(r<=0)return 255;int cx=x<a.x1+r?a.x1+r:x>a.x2-r?a.x2-r:x,cy=y<a.y1+r?a.y1+r:y>a.y2-r?a.y2-r:y;if(cx==x||cy==y)return 255;unsigned hit=0;int64_t rr=(int64_t)(r*4)*(r*4);for(int sy=1;sy<=3;sy+=2)for(int sx=1;sx<=3;sx+=2){int64_t dx=4*(x-cx)+sx-2,dy=4*(y-cy)+sy-2;if(dx*dx+dy*dy<=rr)hit++;}return hit*255/4;}
/* Lazy mathematical coverage tables, one quarter per radius <= 32.
 * At most sum(r*r, r=1..32) = 11440 PSRAM bytes; no rendered pixels cached.
 * Look up once per primitive, never allocate/retry inside a pixel loop. */
#define ROUND_CACHE_RADIUS 32
static uint8_t *round_cache[ROUND_CACHE_RADIUS+1];
static const uint8_t *round_table(int r){
    if(r<=0||r>ROUND_CACHE_RADIUS)return NULL;
    if(!round_cache[r]){
        uint8_t *table=mem_alloc((size_t)r*r);if(!table)return NULL;
        nui_area_t canonical={0,0,2*r,2*r};
        for(int dy=1;dy<=r;dy++)for(int dx=1;dx<=r;dx++)
            table[(dy-1)*r+dx-1]=(uint8_t)round_coverage(canonical,r,r-dx,r-dy);
        round_cache[r]=table;
    }
    return round_cache[r];
}
static void round_cache_clear(void){
    for(int i=1;i<=ROUND_CACHE_RADIUS;i++){mem_free(round_cache[i]);round_cache[i]=NULL;}
}
static unsigned cached_round_coverage(nui_area_t a,int r,int x,int y,const uint8_t *table){
    if(!table)return round_coverage(a,r,x,y);
    int dx=x<a.x1+r?a.x1+r-x:x>a.x2-r?x-a.x2+r:0;
    int dy=y<a.y1+r?a.y1+r-y:y>a.y2-r?y-a.y2+r:0;
    if(!dx||!dy)return 255;
    if(dx>r||dy>r)return round_coverage(a,r,x,y);
    return table[(dy-1)*r+dx-1];
}
static void rectangle(nui_layer_t *l,nui_area_t a,int radius,uint16_t c,unsigned alpha){nui_area_t clip;if(!area_intersect(&clip,a,l->clip))return;radius=MAX(0,MIN(radius,MIN(nui_area_get_width(&a),nui_area_get_height(&a))/2));const uint8_t *coverage=round_table(radius);for(int y=clip.y1;y<=clip.y2;y++){if(!radius||(y>=a.y1+radius&&y<=a.y2-radius)){
    int last=radius?MIN(clip.y2,a.y2-radius):clip.y2;
    if(alpha*l->opa/255==255&&last>y){
        size_t count=(size_t)(clip.x2-clip.x1+1);uint16_t *first=layer_row(l,clip.x1,y);
        opaque_span_fill(first,count,c);
        for(int py=y+1;py<=last;py++)memcpy(layer_row(l,clip.x1,py),first,count*sizeof(*first));
        FINE_COUNT(n.stats.primitive[active_primitive].pixels+=(uint64_t)count*(last-y+1));y=last;
    }else span(l,clip.x1,clip.x2,y,c,alpha);
    continue;}int left=MAX(clip.x1,a.x1+radius),right=MIN(clip.x2,a.x2-radius);if(left<=right)span(l,left,right,y,c,alpha);for(int x=clip.x1;x<left;x++)pixel(l,x,y,c,cached_round_coverage(a,radius,x,y,coverage)*alpha/255);for(int x=MAX(clip.x1,right+1);x<=clip.x2;x++)pixel(l,x,y,c,cached_round_coverage(a,radius,x,y,coverage)*alpha/255);}}
static void border_pixel(nui_layer_t *l,nui_area_t a,nui_area_t inner,int radius,int width,int x,int y,uint16_t c,unsigned alpha,const uint8_t *outer_table,const uint8_t *inner_table){unsigned outer=cached_round_coverage(a,radius,x,y,outer_table),inside=0;if(x>=inner.x1&&x<=inner.x2&&y>=inner.y1&&y<=inner.y2)inside=cached_round_coverage(inner,MAX(0,radius-width),x,y,inner_table);if(outer>inside)pixel(l,x,y,c,(outer-inside)*alpha/255);}
static void border(nui_layer_t *l,nui_area_t a,int radius,int width,uint16_t c,unsigned alpha){
    if(width<=0)return;
    nui_area_t clip;if(!area_intersect(&clip,a,l->clip))return;
    radius=MAX(0,MIN(radius,MIN(nui_area_get_width(&a),nui_area_get_height(&a))/2));
    nui_area_t inner={a.x1+width,a.y1+width,a.x2-width,a.y2-width};
    int edge=MAX(radius,width);
    const uint8_t *outer_table=round_table(radius),*inner_table=round_table(MAX(0,radius-width));
    for(int y=clip.y1;y<=clip.y2;y++){
        if(y<a.y1+edge||y>a.y2-edge){
            if(y<a.y1+width||y>a.y2-width){
                int left=MAX(clip.x1,a.x1+radius),right=MIN(clip.x2,a.x2-radius);
                if(left<=right)span(l,left,right,y,c,alpha);
                for(int x=clip.x1;x<left;x++)border_pixel(l,a,inner,radius,width,x,y,c,alpha,outer_table,inner_table);
                for(int x=MAX(right+1,clip.x1);x<=clip.x2;x++)border_pixel(l,a,inner,radius,width,x,y,c,alpha,outer_table,inner_table);
            }else{
                for(int x=clip.x1;x<=MIN(clip.x2,a.x1+edge);x++)border_pixel(l,a,inner,radius,width,x,y,c,alpha,outer_table,inner_table);
                for(int x=MAX(clip.x1,a.x2-edge);x<=clip.x2;x++)border_pixel(l,a,inner,radius,width,x,y,c,alpha,outer_table,inner_table);
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
    uint16_t *dst=layer_row(l,gx+lo,py);
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
    if(old/80!=n.ticks/80)tick_walk(n.root,old);
    n.event_depth--;garbage_collect();}
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

static void image_render(nui_obj_t *o,nui_layer_t *layer,nui_area_t a){const nui_image_dsc_t *d=o->image;if(!d||!d->data)return;assert(d->header.cf==NUI_COLOR_FORMAT_RGB565);nui_area_t clip;if(!area_intersect(&clip,a,layer->clip))return;unsigned stride=d->header.stride?d->header.stride/2:d->header.w,opa=(unsigned)num(o,NUI_STYLE_IMAGE_OPA,0)*layer->opa/255;assert(stride>=d->header.w&&d->data_size>=(size_t)stride*d->header.h*2);const uint16_t *src=(const uint16_t*)d->data;uint64_t t=profile_begin(NUI_PRIM_IMAGE);nui_primitive_t prev=active_primitive;active_primitive=NUI_PRIM_IMAGE;for(int y=clip.y1;y<=clip.y2;y++){const uint16_t *row=src+(size_t)(y-a.y1)*stride+(clip.x1-a.x1);uint16_t *dest=layer_row(layer,clip.x1,y);size_t count=(size_t)(clip.x2-clip.x1+1);if(opa==255)memcpy(dest,row,count*2);else for(size_t x=0;x<count;x++)dest[x]=blend565(dest[x],row[x],opa);FINE_COUNT(n.stats.primitive[NUI_PRIM_IMAGE].pixels+=count);}profile_end(NUI_PRIM_IMAGE,t);active_primitive=prev;}

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
        for(unsigned run=0;run<2;run++){
            int from=intervals[run*2],to=intervals[run*2+1];
            for(int x=from;x<=to;x++){
                unsigned alpha;
                if(e){
                    int sy=shadow_map(y-a.y1,h,e->ch,e->cap,e->narrow_h)+blur-offset;
                    /* The canonical horizontal middle maps to one alpha sample.
                     * Blend every destination pixel with the existing span path. */
                    if(!e->narrow_w&&x>=a.x1+e->cap&&x<=a.x2-e->cap){
                        int last=MIN(to,a.x2-e->cap);
                        alpha=e->alpha[(size_t)sy*e->mw+e->cap+blur];
                        if(alpha)span(l,x,last,y,c,alpha);
                        x=last;continue;
                    }
                    int sx=shadow_map(x-a.x1,w,e->cw,e->cap,e->narrow_w)+blur;
                    alpha=e->alpha[(size_t)sy*e->mw+sx];
                }else alpha=shadow_alpha(a,radius,blur,offset,opa,x,y);
                if(alpha)pixel(l,x,y,c,alpha);
            }
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
                memcpy(layer_row(l,left,y),lut+left-a.x1,count*sizeof(*lut));
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
static bool layer_cache_fresh_region(nui_obj_t*,nui_layer_t,nui_area_t*);
static bool callbacks_reachable(const nui_obj_t*,nui_layer_t);
static bool opaque_cover(nui_obj_t *o,nui_area_t dirty,bool allow_cache){
    if(!nui_obj_is_visible(o)||num(o,NUI_STYLE_OPA,0)!=255)return false;
    nui_area_t a;nui_obj_get_coords(o,&a);if(!area_contains(a,dirty))return false;
    if(allow_cache&&o->layer_cache){
        nui_layer_t probe={.clip=dirty,.opa=255};nui_area_t covered;
        if(layer_cache_fresh_region(o,probe,&covered)&&area_contains(covered,dirty))return true;
    }
    for(nui_obj_t *c=o->last;c;c=c->prev)if(opaque_cover(c,dirty,allow_cache))return true;
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
static void object_render(nui_obj_t*,nui_layer_t);
static void object_render_base(nui_obj_t *o,nui_layer_t parent){if(!nui_obj_is_visible(o))return;nui_area_t a,visual;nui_obj_get_coords(o,&a);visual=object_visual(o);nui_area_t check;if(!area_intersect(&check,parent.clip,visual))return;nui_layer_t l=parent;l.opa=(uint8_t)((unsigned)parent.opa*(unsigned)num(o,NUI_STYLE_OPA,0)/255);if(!l.opa)return;nui_obj_t *saved_source=draw_source;draw_source=o;
    bool optimized=cache_occlusion_pass&&!l.target;
    nui_obj_t *cover=NULL;if(l.opa==255)for(nui_obj_t *c=o->last;c;c=c->prev)if(opaque_cover(c,l.clip,optimized)){cover=c;break;}
    if(cover)for(nui_obj_t *c=o->first;c&&c!=cover;c=c->next)if(optimized?callbacks_reachable(c,l):occlusion_callbacks_required(c)){cover=NULL;break;}
    if(!cover)object_box(o,&l,a,0);
    nui_layer_t inner=l;if(!area_intersect(&inner.clip,l.clip,a)){draw_source=saved_source;return;}
    if(!cover){if(is_kind(o,1)||is_kind(o,4))text_render(o,&inner,a,0);
    else if(is_kind(o,7))image_render(o,&inner,a);
    else if(is_kind(o,5))keyboard_render(o,&inner);
    else if(is_kind(o,6))chart_render(o,&inner,a);
    else if(is_kind(o,8)){nui_area_t fill=a;fill.x2=fill.x1+(int)((int64_t)nui_area_get_width(&a)*(o->bar_value-o->bar_min)/MAX(1,o->bar_max-o->bar_min))-1;if(fill.x2>=fill.x1)object_box(o,&inner,fill,NUI_PART_INDICATOR);}
    else if(is_kind(o,3)){nui_area_t track=a;int radius=nui_area_get_height(&a)/2;nui_draw_rect_dsc_t d={.radius=radius,.bg_opa=255,.bg_color=color(o,NUI_STYLE_BG_COLOR,(o->state&NUI_STATE_CHECKED)?NUI_PART_INDICATOR:0)};nui_draw_rect(&inner,&d,&track);int inset=4,size=nui_area_get_height(&a)-inset*2;int x=(o->state&NUI_STATE_CHECKED)?a.x2-inset-size+1:a.x1+inset;nui_area_t knob={x,a.y1+inset,x+size-1,a.y1+inset+size-1};d.bg_color=color(o,NUI_STYLE_BG_COLOR,NUI_PART_KNOB);d.radius=NUI_RADIUS_CIRCLE;nui_draw_rect(&inner,&d,&knob);}
    }event_send(o,NUI_EVENT_DRAW_MAIN,NULL,&inner);if(o->deleted){draw_source=saved_source;return;}for(nui_obj_t *c=cover?cover:o->first;c;){nui_obj_t *next=c->next;object_render(c,inner);c=next;}event_send(o,NUI_EVENT_DRAW_POST,NULL,&inner);draw_source=saved_source;}
/* Retained surfaces keep absolute draw coordinates with a borrowed local
 * target. There is no recursive swap of display framebuffer/global geometry. */
static bool layer_draw_callbacks(const nui_obj_t *o){
    for(handler *h=o->handlers;h;h=h->next)if(h->code==NUI_EVENT_DRAW_MAIN||h->code==NUI_EVENT_DRAW_POST||h->code==NUI_EVENT_ALL||
        (h->code==NUI_EVENT_DRAW_TASK_ADDED&&(o->flags&NUI_OBJ_FLAG_SEND_DRAW_TASK_EVENTS)))return true;
    return false;
}
/* Conservative event reachability, matching the painter's visibility,
 * visual-intersection, opacity and ancestor clips. Never use paint-inert flags
 * to permit splitting: callback count/order/clip are observable semantics. */
static bool callbacks_reachable_impl(const nui_obj_t *o,nui_layer_t parent){
#ifdef NUI_CALLBACK_AUDIT_VISIT
    NUI_CALLBACK_AUDIT_VISIT();
#endif
    if(!o->potential_draw_handlers)return false;
    if(!nui_obj_is_visible(o))return false;
    nui_area_t a,visual=object_visual(o),check;
    if(!area_intersect(&check,parent.clip,visual))return false;
    nui_layer_t inner=parent;inner.opa=(uint8_t)((unsigned)parent.opa*(unsigned)num(o,NUI_STYLE_OPA,0)/255);
    if(!inner.opa)return false;
    /* Task callbacks may fire from a shadow/box before inner bounds intersect. */
    if(layer_draw_callbacks(o))return true;
    nui_obj_get_coords(o,&a);if(!area_intersect(&inner.clip,parent.clip,a))return false;
    for(nui_obj_t *c=o->first;c;c=c->next)if(callbacks_reachable_impl(c,inner))return true;
    return false;
}
/* Only external audits read the clock; recursive nodes do not. */
static bool callbacks_reachable(const nui_obj_t *o,nui_layer_t parent){
    uint64_t start=now_us();bool result=callbacks_reachable_impl(o,parent);uint64_t end=now_us();
    if(end>=start)n.stats.callback_audit_us+=end-start;
    return result;
}
static bool layer_static_subtree(const nui_obj_t *o){
    if(o->deleted||(o->flags&NUI_OBJ_FLAG_HIDDEN))return true;
    if(o->class_p->kind>3||layer_draw_callbacks(o)||(is_kind(o,1)&&o->long_mode==NUI_LABEL_LONG_SCROLL_CIRCULAR))return false;
    for(nui_obj_t *c=o->first;c;c=c->next)if(!layer_static_subtree(c))return false;
    return true;
}
static bool layer_cache_region(nui_obj_t *o,nui_layer_t parent,nui_area_t a,nui_area_t *clip,nui_color_t *matte){
    if(parent.target||parent.opa!=255||!is_kind(o,0)||num(o,NUI_STYLE_OPA,0)!=255||
       num(o,NUI_STYLE_SHADOW_WIDTH,0)||num(o,NUI_STYLE_OUTLINE_WIDTH,0))return false;
    if(!area_intersect(clip,parent.clip,a))return false;
    *matte=0;
    bool own=!o->layer_cache->parent_matte;
    nui_obj_t *p=own?o:o->parent;
    if(!p||!is_kind(p,0)||num(p,NUI_STYLE_BG_OPA,0)!=255||num(p,NUI_STYLE_OPA,0)!=255||
       (!own&&num(p,NUI_STYLE_BG_GRAD_DIR,0))||num(p,NUI_STYLE_OUTLINE_WIDTH,0)||layer_draw_callbacks(p))return false;
    /* A self-painted opaque interior needs no assumed parent matte. For
     * transparent content, earlier siblings could modify that backdrop. */
    if(!own)for(nui_obj_t *c=p->first;c&&c!=o;c=c->next)if(nui_obj_is_visible(c))return false;
    nui_area_t pa;nui_obj_get_coords(p,&pa);
    int radius=MAX(0,MIN(num(p,NUI_STYLE_RADIUS,0),MIN(nui_area_get_width(&pa),nui_area_get_height(&pa))/2));
    int border=MAX(0,num(p,NUI_STYLE_BORDER_WIDTH,0)),edge=MAX(radius,border);
    /* The rounded background is uniformly opaque on the UNION of these two
     * bands, not merely their intersection. Border pixels are not matte. */
    nui_area_t bands[2]={{pa.x1+border,pa.y1+edge,pa.x2-border,pa.y2-edge},
        {pa.x1+edge,pa.y1+border,pa.x2-edge,pa.y2-border}};
    uint64_t largest=0;nui_area_t best={0};
    for(unsigned i=0;i<2;i++){
        nui_area_t part;if(!area_intersect(&part,*clip,bands[i]))continue;
        uint64_t size=(uint64_t)nui_area_get_width(&part)*nui_area_get_height(&part);
        if(size>largest){largest=size;best=part;}
    }
    *matte=own?0:color(p,NUI_STYLE_BG_COLOR,0);
    if(!largest)return false;
    *clip=best;return true;
}
/* Read-only opacity proof. In particular this must not prepare/repair a
 * surface whose allocation or eligibility could fail after backgrounds skip. */
static bool layer_cache_fresh_region(nui_obj_t *o,nui_layer_t parent,nui_area_t *clip){
    retained_layer *c=o->layer_cache;
    if(parent.target||!c||!c->pixels||!c->valid||c->failed||c->damage_valid)return false;
    nui_area_t a;nui_obj_get_coords(o,&a);nui_color_t matte;
    if(!layer_cache_region(o,parent,a,clip,&matte))return false;
    return c->w==nui_area_get_width(&a)&&c->h==nui_area_get_height(&a)&&
        c->style==style_epoch&&c->geometry==geometry_epoch&&c->matte==matte;
}
/* One proved later opaque band; never infer coverage from a translucent
 * corner or a visual bounding box. The existing logical-pass callback audit
 * guarantees no draw callback can mutate the later cover before it paints. */
static bool cache_later_opaque(nui_obj_t *o,nui_area_t clip,nui_area_t *cover){
    if(!cache_occlusion_pass&&!cache_occlusion_edges)return false;
    uint64_t largest=0;
    for(nui_obj_t *p=o;p;p=p->parent)for(nui_obj_t *c=p->next;c;c=c->next){
        if(!nui_obj_is_visible(c)||!is_kind(c,0)||num(c,NUI_STYLE_OPA,0)!=255||num(c,NUI_STYLE_BG_OPA,0)!=255)continue;
        /* Prove inherited opacity and clips independently of the cached layer. */
        nui_area_t inherited=clip;bool eligible=true;
        for(nui_obj_t *ancestor=c->parent;ancestor;ancestor=ancestor->parent){
            nui_area_t bounds;nui_obj_get_coords(ancestor,&bounds);
            if(!nui_obj_is_visible(ancestor)||num(ancestor,NUI_STYLE_OPA,0)!=255||
                !area_intersect(&inherited,inherited,bounds)){eligible=false;break;}
        }
        if(!eligible)continue;
        nui_area_t a;nui_obj_get_coords(c,&a);
        int r=MAX(0,MIN(num(c,NUI_STYLE_RADIUS,0),MIN(nui_area_get_width(&a),nui_area_get_height(&a))/2));
        nui_area_t bands[2]={{a.x1,a.y1+r,a.x2,a.y2-r},{a.x1+r,a.y1,a.x2-r,a.y2}};
        for(unsigned i=0;i<2;i++){
            nui_area_t part;if(!area_intersect(&part,inherited,bands[i]))continue;
            uint64_t size=(uint64_t)nui_area_get_width(&part)*nui_area_get_height(&part);
            if(size>largest){largest=size;*cover=part;}
        }
    }
    return largest!=0;
}
static unsigned cache_rect_subtract(nui_area_t a,nui_area_t cover,nui_area_t out[4]){
    nui_area_t b;if(!area_intersect(&b,a,cover)){out[0]=a;return 1;}
    nui_area_t parts[4]={{a.x1,a.y1,a.x2,b.y1-1},{a.x1,b.y2+1,a.x2,a.y2},
        {a.x1,b.y1,b.x1-1,b.y2},{b.x2+1,b.y1,a.x2,b.y2}};
    unsigned count=0;for(unsigned i=0;i<4;i++)if(parts[i].x1<=parts[i].x2&&parts[i].y1<=parts[i].y2)out[count++]=parts[i];
    return count;
}
static nui_area_t surface_align_rect(nui_area_t);
static bool surface_try_defer(nui_obj_t*,nui_area_t,nui_area_t*);
static bool layer_cache_draw(nui_obj_t *o,nui_layer_t parent){
    retained_layer *c=o->layer_cache;nui_area_t a,clip;
    nui_obj_get_coords(o,&a);nui_color_t matte;
    if(!layer_cache_region(o,parent,a,&clip,&matte)){layer_stats.fallbacks++;return false;}
    int w=nui_area_get_width(&a),h=nui_area_get_height(&a);
    bool key_changed=c->w!=w||c->h!=h||c->style!=style_epoch||c->geometry!=geometry_epoch||c->matte!=matte;
    if(key_changed){c->valid=false;c->failed=false;c->damage_valid=false;}
    if(c->failed){layer_stats.fallbacks++;return false;}
    if(!c->valid||c->damage_valid){
        if(c->storage&&c->storage->pins){layer_stats.fallbacks++;return false;}
        layer_stats.misses++;
        c->style=style_epoch;c->geometry=geometry_epoch;c->matte=matte;
        if(w<=0||h<=0||(size_t)w>LAYER_CACHE_LIMIT/2/(size_t)h||!layer_static_subtree(o)){
            layer_cache_release_pixels(c);c->w=w;c->h=h;c->failed=true;layer_stats.fallbacks++;return false;
        }
        size_t bytes=(size_t)w*h*2;
        if(c->bytes!=bytes){
            layer_cache_release_pixels(c);c->w=w;c->h=h;
            c->storage=storage_create(w,h);
            if(!c->storage){c->failed=true;layer_stats.fallbacks++;return false;}
            c->pixels=c->storage->landscape;c->bytes=bytes;
        }
        c->w=w;c->h=h;
        bool full=!c->valid;
        nui_area_t repaint=full?a:(nui_area_t){a.x1+c->damage.x1,a.y1+c->damage.y1,a.x1+c->damage.x2,a.y1+c->damage.y2};
        struct nui_render_target target={c->pixels,w,a.x1,a.y1};
        nui_layer_t layer={.clip=repaint,.opa=255,.target=&target};uint64_t started=now_us();
        if(c->parent_matte){
            uint16_t bg=nui_color_to_u16(matte);
            for(int y=repaint.y1;y<=repaint.y2;y++){
                uint16_t *row=layer_row(&layer,repaint.x1,y);
                for(int x=repaint.x1;x<=repaint.x2;x++)*row++=bg;
            }
        }
        /* No draw callbacks are eligible, so this cannot mutate the scene or
         * recursively free/reconfigure the retained target. */
        object_render_base(o,layer);uint64_t ended=now_us();
        if(ended>=started)layer_stats.build_us+=ended-started;
        if(full)layer_stats.builds++;else layer_stats.updates++;
        nui_area_t local={repaint.x1-a.x1,repaint.y1-a.y1,repaint.x2-a.x1,repaint.y2-a.y1};
        storage_changed(c->storage,local,full);
        c->valid=true;c->damage_valid=false;
    }
    bool deferred=false;nui_area_t deferred_clip={0};
    if(surface_defer_cb&&!parent.target){
        nui_area_t aligned=surface_align_rect(clip);
        if(aligned.x1<=aligned.x2&&aligned.y1<=aligned.y2&&surface_try_defer(o,a,&aligned)){deferred_clip=aligned;deferred=true;}
    }
    size_t count=(size_t)nui_area_get_width(&clip);
    nui_area_t whole,opaque={0};bool have_whole=area_intersect(&whole,parent.clip,a);
    bool have_opaque=have_whole&&cache_later_opaque(o,whole,&opaque);
    nui_area_t cpu[4]={clip};unsigned cpu_count=1;
    if(deferred){cpu[0]=(nui_area_t){clip.x1,clip.y1,clip.x2,deferred_clip.y1-1};
        cpu[1]=(nui_area_t){clip.x1,deferred_clip.y2+1,clip.x2,clip.y2};
        cpu[2]=(nui_area_t){clip.x1,deferred_clip.y1,deferred_clip.x1-1,deferred_clip.y2};
        cpu[3]=(nui_area_t){deferred_clip.x2+1,deferred_clip.y1,clip.x2,deferred_clip.y2};cpu_count=4;}
    uint64_t blit_start=now_us();
    for(unsigned i=0;i<cpu_count;i++)if(cpu[i].x1<=cpu[i].x2&&cpu[i].y1<=cpu[i].y2){
        nui_area_t visible[4];unsigned visible_count;
        if(have_opaque)visible_count=cache_rect_subtract(cpu[i],opaque,visible);else{visible[0]=cpu[i];visible_count=1;}
        for(unsigned j=0;j<visible_count;j++){
            size_t width=(size_t)nui_area_get_width(&visible[j]);
            for(int y=visible[j].y1;y<=visible[j].y2;y++)memcpy(layer_row(&parent,visible[j].x1,y),c->pixels+(size_t)(y-a.y1)*w+visible[j].x1-a.x1,width*2);
            layer_stats.cpu_blit_pixels+=(uint64_t)width*nui_area_get_height(&visible[j]);
        }
    }
    uint64_t blit_end=now_us();if(blit_end>=blit_start)n.stats.cache_cpu_blit_us+=blit_end-blit_start;
    layer_stats.hits++;layer_stats.blit_pixels+=(uint64_t)count*nui_area_get_height(&clip);
    /* Preserve the original composition at rounded corners and borders. The
     * four strips are disjoint and use the original, unchanged subtree painter. */
    nui_area_t all;if(area_intersect(&all,parent.clip,a)){
        nui_area_t strips[4]={{all.x1,all.y1,all.x2,clip.y1-1},{all.x1,clip.y2+1,all.x2,all.y2},
            {all.x1,clip.y1,clip.x1-1,clip.y2},{clip.x2+1,clip.y1,all.x2,clip.y2}};
        for(unsigned i=0;i<4;i++)if(strips[i].x1<=strips[i].x2&&strips[i].y1<=strips[i].y2){
            nui_area_t visible[4];unsigned visible_count;
            if(have_opaque)visible_count=cache_rect_subtract(strips[i],opaque,visible);else{visible[0]=strips[i];visible_count=1;}
            for(unsigned j=0;j<visible_count;j++){
                nui_layer_t edge=parent;edge.clip=visible[j];
                /* Reuse the other arm of the matte cross. Recursion strictly
                 * shrinks the clip; only true corners/border reach baseline. */
                if(!layer_cache_draw(o,edge))object_render_base(o,edge);
            }
        }
    }
    return true;
}
static void object_render(nui_obj_t *o,nui_layer_t parent){
    if(!nui_obj_is_visible(o))return;
    if(o->layer_cache&&layer_cache_draw(o,parent))return;
    object_render_base(o,parent);
}
static nui_area_t surface_align_rect(nui_area_t a){
    if(surface_alignment==1)return a;
    return (nui_area_t){(a.x1+15)&~15,(a.y1+15)&~15,((a.x2+1)&~15)-1,((a.y2+1)&~15)-1};
}
static bool surface_crop(nui_area_t *area,nui_area_t obstacle){
    nui_area_t overlap;if(!area_intersect(&overlap,*area,obstacle))return true;
    nui_area_t a=*area,choices[4]={{a.x1,a.y1,obstacle.x1-1,a.y2},{obstacle.x2+1,a.y1,a.x2,a.y2},
        {a.x1,a.y1,a.x2,obstacle.y1-1},{a.x1,obstacle.y2+1,a.x2,a.y2}};
    uint64_t largest=0;nui_area_t best={0};
    for(unsigned i=0;i<4;i++){
        nui_area_t c=surface_align_rect(choices[i]);
        if(c.x1>c.x2||c.y1>c.y2)continue;
        uint64_t size=(uint64_t)nui_area_get_width(&c)*nui_area_get_height(&c);
        if(size>largest){largest=size;best=c;}
    }
    if(!largest)return false;
    *area=best;return true;
}
static bool surface_try_defer(nui_obj_t *o,nui_area_t a,nui_area_t *selected){
    if(n.flush||deferred_accepts>=NUI_NATIVE_MAX_DEFERRED_VIEWS)return false;
    nui_area_t dst=*selected;
    /* One bounded conservative rectangle. Any later visible painter along the
     * ancestor chain crops its visual bounds out of the deferred region. */
    for(nui_obj_t *p=o;p;p=p->parent)for(nui_obj_t *later=p->next;later;later=later->next)
        if(nui_obj_is_visible(later)&&!surface_crop(&dst,object_visual(later)))return false;
    nui_layer_t query={.clip=dst,.opa=255};if(callbacks_reachable(n.root,query))return false;
    surface_storage *st=o->layer_cache->storage;
    if(!st||!storage_portrait(st))return false;
    surface_lease *lease=NULL;
    for(unsigned i=0;i<16;i++)if(!surface_leases[i].storage){lease=&surface_leases[i];break;}
    if(!lease)return false;
    memset(&lease->view,0,sizeof(lease->view));lease->storage=st;lease->framebuffer_epoch=framebuffer_epoch;st->pins++;layer_stats.pins++;
    lease->view=(nui_surface_view_t){.landscape=st->landscape,.portrait=st->portrait,
        .landscape_stride=(uint32_t)st->w,.portrait_stride=(uint32_t)st->pw,.logical_width=(uint32_t)st->w,.logical_height=(uint32_t)st->h,
        .source_x=(uint32_t)(dst.x1-a.x1),.source_y=(uint32_t)(dst.y1-a.y1),.destination=dst,
        .storage_id=st->id,.portrait_generation=st->portrait_generation,.lease=lease};
    if(!surface_defer_cb(&lease->view,surface_defer_user)){
        nui_surface_view_t rejected=lease->view;nui_surface_view_release(&rejected);layer_stats.defer_rejects++;return false;
    }
    *selected=dst;deferred_accepts++;layer_stats.deferred_views++;
    if(((uint32_t)dst.x1|(uint32_t)dst.y1|(uint32_t)(dst.x2+1)|(uint32_t)(dst.y2+1)|
        lease->view.source_x|lease->view.source_y|(uint32_t)nui_area_get_width(&dst)|(uint32_t)nui_area_get_height(&dst))&15u)
        layer_stats.deferred_unaligned_views++;
    layer_stats.deferred_pixels+=(uint64_t)nui_area_get_width(&dst)*nui_area_get_height(&dst);return true;
}
#if NUI_LAYER_CACHE_OCCLUSION
static bool layer_cache_split_region(nui_obj_t *o,nui_layer_t parent,nui_area_t *region){
    if(!nui_obj_is_visible(o))return false;
    nui_area_t visual=object_visual(o),check,a;
    if(!area_intersect(&check,parent.clip,visual))return false;
    if(parent.target||parent.opa!=255||num(o,NUI_STYLE_OPA,0)!=255)return false;
    /* Compare all eligible fresh surfaces, not just the last painter.
     * A small foreground notice must not fragment the larger scrolling view. */
    uint64_t largest=0;nui_area_t candidate;
    if(layer_cache_fresh_region(o,parent,&candidate)){
        largest=(uint64_t)nui_area_get_width(&candidate)*nui_area_get_height(&candidate);*region=candidate;
    }
    nui_obj_get_coords(o,&a);
    if(area_intersect(&parent.clip,parent.clip,a))for(nui_obj_t *c=o->last;c;c=c->prev){
        if(!layer_cache_split_region(c,parent,&candidate))continue;
        uint64_t size=(uint64_t)nui_area_get_width(&candidate)*nui_area_get_height(&candidate);
        if(size>largest){largest=size;*region=candidate;}
    }
    return largest!=0;
}
#endif
static void render_logical_dirty(nui_layer_t layer){
#if NUI_LAYER_CACHE_OCCLUSION
    nui_area_t interior;
    if(layer_cache_requests&&!layer.target&&layer_cache_split_region(n.root,layer,&interior)){
        if(callbacks_reachable(n.root,layer)){
            layer_stats.occlusion_fallbacks++;object_render(n.root,layer);return;
        }
        bool old_pass=cache_occlusion_pass,old_edges=cache_occlusion_edges;
        layer_stats.occlusion_splits++;
        layer_stats.occlusion_pixels+=(uint64_t)nui_area_get_width(&interior)*nui_area_get_height(&interior);
        cache_occlusion_pass=true;cache_occlusion_edges=false;
        nui_layer_t part=layer;part.clip=interior;object_render(n.root,part);
        /* Four disjoint strips, keeping rendered[] / transport ROI unchanged. */
        nui_area_t a=layer.clip;
        nui_area_t strips[4]={{a.x1,a.y1,a.x2,interior.y1-1},{a.x1,interior.y2+1,a.x2,a.y2},
            {a.x1,interior.y1,interior.x1-1,interior.y2},{interior.x2+1,interior.y1,a.x2,interior.y2}};
        cache_occlusion_pass=false;cache_occlusion_edges=true;
        for(unsigned i=0;i<4;i++)if(strips[i].x1<=strips[i].x2&&strips[i].y1<=strips[i].y2){part.clip=strips[i];object_render(n.root,part);}
        cache_occlusion_pass=old_pass;cache_occlusion_edges=old_edges;return;
    }
#endif
    object_render(n.root,layer);
}
void nui_native_set_flush_cb(nui_native_flush_cb_t cb,void *user){n.flush=cb;n.flush_user=user;}
void nui_native_set_clock_cb(uint64_t (*cb)(void)){n.clock_us=cb;}
void nui_native_profile_snapshot(nui_native_stats_t *out){if(out){*out=n.stats;out->shadow_cache_bytes=shadow_cache_bytes+sizeof(shadow_cache);out->shadow_cache_peak_bytes=shadow_cache_peak_bytes+sizeof(shadow_cache);out->shadow_cache_capacity_bytes=SHADOW_CACHE_LIMIT+sizeof(shadow_cache);out->shadow_cache_static_bytes=sizeof(shadow_cache);out->object_cache_bytes=cache_objects*OBJECT_CACHE_BYTES;out->object_cache_peak_bytes=cache_objects_peak*OBJECT_CACHE_BYTES;out->object_cache_bytes_per_object=OBJECT_CACHE_BYTES;out->gradient_cache_bytes=sizeof(gradient_cache);out->gradient_cache_peak_bytes=sizeof(gradient_cache);out->gradient_cache_capacity_bytes=sizeof(gradient_cache);out->gradient_cache_hits=gradient_cache.hits;out->gradient_cache_misses=gradient_cache.misses;out->gradient_cache_build_pixels=gradient_cache.build_pixels;}}
void nui_native_profile_reset(void){size_t bytes=n.stats.allocation_bytes,peak=n.stats.allocation_peak,count=n.stats.allocation_count;uint64_t fail=n.stats.allocation_failures;memset(&n.stats,0,sizeof(n.stats));n.stats.allocation_bytes=bytes;n.stats.allocation_peak=peak;n.stats.allocation_count=count;n.stats.allocation_failures=fail;gradient_cache.hits=gradient_cache.misses=gradient_cache.build_pixels=0;}
size_t nui_native_get_dirty(nui_area_t *out,size_t cap){size_t count=MIN(cap,n.rendered_count);if(out&&count)memcpy(out,n.rendered,count*sizeof(*out));return n.rendered_count;}
static void align_pending_dirty(void){
    if(dirty_alignment==1||!n.dirty_count)return;
    nui_area_t pending[MAX_DIRTY];size_t count=n.dirty_count;memcpy(pending,n.dirty,count*sizeof(*pending));n.dirty_count=0;
    for(size_t i=0;i<count;i++){
        nui_area_t a=pending[i];a.x1&=~15;a.y1&=~15;
        a.x2=(int32_t)MIN((int64_t)n.width-1,(((int64_t)a.x2+16)&~15)-1);
        a.y2=(int32_t)MIN((int64_t)n.height-1,(((int64_t)a.y2+16)&~15)-1);nui_native_invalidate(&a);
    }
}
size_t nui_native_render(void){uint64_t align_start=now_us();align_pending_dirty();uint64_t align_end=now_us();if(align_end>=align_start)n.stats.align_us+=align_end-align_start;deferred_accepts=0;if(!n.fb||!n.root||!n.dirty_count){n.rendered_count=0;return 0;}uint64_t start=now_us();n.rendered_count=n.dirty_count;memcpy(n.rendered,n.dirty,n.dirty_count*sizeof(nui_area_t));n.dirty_count=0;n.rendering=true;for(size_t i=0;i<n.rendered_count;i++){nui_area_t a=n.rendered[i];nui_layer_t layer={.clip=a,.opa=255};active_primitive=NUI_PRIM_FILL;nui_area_t root_bounds;nui_obj_get_coords(n.root,&root_bounds);
    bool root_opaque=nui_obj_is_visible(n.root)&&num(n.root,NUI_STYLE_BG_OPA,0)==255&&num(n.root,NUI_STYLE_OPA,0)==255&&num(n.root,NUI_STYLE_RADIUS,0)==0&&area_contains(root_bounds,a);
    if(root_opaque)FINE_COUNT(n.stats.clear_pixels_skipped+=(uint64_t)nui_area_get_width(&a)*nui_area_get_height(&a));
    else for(int y=a.y1;y<=a.y2;y++){uint16_t *row=n.fb+(size_t)y*n.width+a.x1;memset(row,0,(size_t)nui_area_get_width(&a)*2);}
    uint64_t logical_start=now_us();render_logical_dirty(layer);uint64_t logical_end=now_us();if(logical_end>=logical_start)n.stats.logical_render_us+=logical_end-logical_start;n.stats.dirty_pixels+=(uint64_t)nui_area_get_width(&a)*nui_area_get_height(&a);}n.rendering=false;uint64_t gc_start=now_us();garbage_collect();uint64_t gc_end=now_us();if(gc_end>=gc_start)n.stats.gc_us+=gc_end-gc_start;n.stats.frames++;n.stats.dirty_rects+=n.rendered_count;uint64_t end=now_us();if(end>=start)n.stats.render_us+=end-start;/* Callback time is excluded from render_us; platform measures transport. */if(n.flush)for(size_t i=0;i<n.rendered_count;i++){nui_area_t *a=&n.rendered[i];n.flush(a,n.fb+(size_t)a->y1*n.width+a->x1,(size_t)n.width,n.flush_user);}return n.rendered_count;}
void nui_native_deinit(void){surface_alignment=16;framebuffer_epoch++;round_cache_clear();gradient_cache.valid=false;shadow_cache_clear();if(n.root)nui_obj_delete(n.root);garbage_collect();while(n.timers){nui_timer_t *t=n.timers;n.timers=t->next;mem_free(t);}mem_free(n.owned_fb);n.fb=n.owned_fb=NULL;n.dirty_count=n.rendered_count=0;n.capture=n.focus=NULL;}
bool nui_native_init(int width,int height,uint16_t *fb){if(width<=0||height<=0||(size_t)width>SIZE_MAX/2/(size_t)height)return false;nui_native_deinit();n.width=width;n.height=height;n.ticks=0;n.input=(nui_indev_t){0};if(!fb){fb=mem_alloc((size_t)width*height*2);if(!fb)return false;n.owned_fb=fb;}n.fb=fb;n.root=object_new(NULL,&nui_obj_class);if(!n.root){mem_free(n.owned_fb);n.fb=n.owned_fb=NULL;return false;}n.root->w=width;n.root->h=height;n.root->flags=0;nui_obj_set_style_bg_opa(n.root,255,0);nui_obj_set_style_bg_color(n.root,0,0);nui_obj_invalidate(n.root);return true;}
size_t nui_native_get_pending_dirty(nui_area_t *out,size_t cap){
    size_t count=MIN(cap,n.dirty_count);if(out&&count)memcpy(out,n.dirty,count*sizeof(*out));
    return n.dirty_count;
}
bool nui_native_bind_framebuffer(uint16_t *fb){
    if(!fb||!n.fb||!n.root||n.rendering||n.event_depth)return false;
    if(n.fb!=fb){n.fb=fb;framebuffer_epoch++;}
    return true;
}
nui_obj_t *nui_screen_active(void){if(!n.root)nui_native_init(1280,720,NULL);return n.root;}
void nui_init(void){if(!n.root)nui_native_init(1280,720,NULL);}
void nui_deinit(void){nui_native_deinit();}
