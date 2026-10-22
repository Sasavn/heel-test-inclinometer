/* Real production native RGB565 display, no OS window, SDL, device or private UI hooks.
 * Finds controls through public object tree and drives a synthetic touch input.
 * Captures actual flush pixels and audits rendered object coordinates. */
#include "tab5_ui.h"
#include "native/native_ui.h"
#include "tab5_ui_graph.h"
#include "tab5_level.h"
/* Keyboard geometry is queried through native public introspection. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
_Static_assert(NUI_CHART_POINT_NONE == INT32_MAX, "native chart gap must match graph core");
#ifndef NUI_PROFILE_FINE_COUNTERS
#error "Host harness requires explicit NUI_PROFILE_FINE_COUNTERS=1 or an explicit release capture build"
#endif
#if !NUI_PROFILE_FINE_COUNTERS && !defined(TAB5_RELEASE_CAPTURE_BUILD)
#error "Fine0 is reserved for release capture equivalence; functional primitive diagnostics require fine1"
#endif
NUI_FONT_DECLARE(font_tab5_64);

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
static double bench_now_ms(void){
#ifdef _WIN32
    LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return 1000.0*t.QuadPart/f.QuadPart;
#else
    struct timespec t;timespec_get(&t,TIME_UTC);return t.tv_sec*1000.0+t.tv_nsec/1000000.0;
#endif
}
static bool rgb565_mode;
static double flush_time_ms;
static uint32_t pixels[1280*720];
static uint16_t framebuffer[1280*720];
static nui_point_t point;
static bool pressed;
static unsigned callbacks, failures, cases;
static uint64_t flushes,flushed_pixels,refreshes;
static tab5_ui_action_t last;
static FILE *report,*points,*textarea_points,*contracts;
static const char *output;
static void flush(const nui_area_t *a,const uint16_t *data,size_t stride,void *user) {
    (void)user;double started=bench_now_ms();
    assert(a->x1>=0&&a->y1>=0&&a->x2<1280&&a->y2<720);
    assert(stride==1280&&data==framebuffer+(size_t)a->y1*1280+a->x1);
    flushes++;flushed_pixels+=(uint64_t)nui_area_get_width(a)*nui_area_get_height(a);
    /* Read the actual framebuffer supplied to production native_init. */
    for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++){
        unsigned v=framebuffer[y*1280+x],r=(v>>11)&31,g=(v>>5)&63,b=v&31;
        pixels[y*1280+x]=((r*255/31)<<16)|((g*255/63)<<8)|(b*255/31);
    }
    flush_time_ms+=bench_now_ms()-started;
}
static void render_now(void){if(nui_native_render())refreshes++;}
static void pump(void){for(unsigned i=0;i<5;i++){nui_native_tick(40);nui_native_touch(point.x,point.y,pressed);render_now();}nui_obj_update_layout(nui_screen_active());render_now();}
/* Adapters over native public keyboard introspection, never private structs.
 * Its returned areas are screen coordinates; old test helpers use local ones. */
static unsigned nui_buttonmatrix_get_button_count(nui_obj_t *k){return (unsigned)nui_native_keyboard_get_keys(k,NULL,NULL,0);}
static bool nui_buttonmatrix_get_button_area(nui_obj_t *k,unsigned index,nui_area_t *out){
    nui_area_t areas[256],origin;size_t n=nui_native_keyboard_get_keys(k,areas,NULL,256);
    assert(n<=256);if(index>=n)return false;nui_obj_get_coords(k,&origin);*out=areas[index];
    out->x1-=origin.x1;out->x2-=origin.x1;out->y1-=origin.y1;out->y2-=origin.y1;return true;
}
static void action(const tab5_ui_action_t *a,void *u){(void)u;last=*a;callbacks++;}
static nui_obj_t *find_label(nui_obj_t *o,const char *text){
    if(nui_obj_has_flag(o,NUI_OBJ_FLAG_HIDDEN))return NULL;
    if(nui_obj_check_type(o,&nui_label_class) && !strcmp(nui_label_get_text(o),text))return o;
    for(int32_t i=(int32_t)nui_obj_get_child_count(o)-1;i>=0;i--){nui_obj_t *r=find_label(nui_obj_get_child(o,i),text);if(r)return r;}
    return NULL;
}
static void click_obj(nui_obj_t *o){assert(o);nui_area_t a;nui_obj_get_coords(o,&a);point.x=(a.x1+a.x2)/2;point.y=(a.y1+a.y2)/2;pressed=true;pump();pressed=false;pump();}
static void click(const char *text){nui_obj_t *o=find_label(nui_screen_active(),text);assert(o);click_obj(nui_obj_get_parent(o));}
static nui_obj_t *field(const char *name){nui_obj_t *l=find_label(nui_screen_active(),name);assert(l);nui_obj_t *parent=nui_obj_get_parent(l);for(uint32_t i=0;i<nui_obj_get_child_count(parent);i++)if(nui_obj_get_child(parent,i)==l)return nui_obj_get_child(parent,i+1);abort();}
static nui_obj_t *keyboard(nui_obj_t *o){if(nui_obj_check_type(o,&nui_keyboard_class))return o;for(uint32_t i=0;i<nui_obj_get_child_count(o);i++){nui_obj_t *r=keyboard(nui_obj_get_child(o,i));if(r)return r;}return NULL;}
static void key_click(const char *text){
    nui_obj_t *k=keyboard(nui_screen_active());assert(k && !nui_obj_has_flag(k,NUI_OBJ_FLAG_HIDDEN));
    nui_area_t a;nui_obj_get_coords(k,&a);
    for(unsigned i=0;i<nui_buttonmatrix_get_button_count(k);i++)if(!strcmp(nui_buttonmatrix_get_button_text(k,i),text)){
        nui_area_t b;assert(nui_buttonmatrix_get_button_area(k,i,&b));point.x=a.x1+(b.x1+b.x2)/2;point.y=a.y1+(b.y1+b.y2)/2;
        pressed=true;pump();pressed=false;pump();return;
    }
    fprintf(stderr,"keyboard key not found: %s\n",text);abort();
}

static bool contains(nui_area_t a,nui_area_t b){return b.x1>=a.x1&&b.y1>=a.y1&&b.x2<=a.x2&&b.y2<=a.y2;}
static void audit(nui_obj_t *o,bool covered){
    if(nui_obj_has_flag(o,NUI_OBJ_FLAG_HIDDEN))return;
    nui_area_t a;nui_obj_get_coords(o,&a);nui_area_t screen={0,0,1279,719};
    bool control=nui_obj_check_type(o,&nui_button_class)||nui_obj_check_type(o,&nui_switch_class)||nui_obj_check_type(o,&nui_textarea_class)||nui_obj_check_type(o,&nui_keyboard_class);
    if(nui_obj_check_type(o,&nui_label_class)&&nui_obj_get_style_text_font(o,0)==&font_tab5_64){
        nui_point_t size;nui_text_get_size(&size,nui_label_get_text(o),&font_tab5_64,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);
        if(size.x>nui_obj_get_width(o)){fprintf(report,"FAIL angle glyph advance %d > %d\n",size.x,nui_obj_get_width(o));failures++;}
    }
    if(control&&!covered){
        if(!contains(screen,a)){fprintf(report,"FAIL outside screen (%d,%d)-(%d,%d)\n",a.x1,a.y1,a.x2,a.y2);failures++;}
        int ext=nui_obj_check_type(o,&nui_switch_class)?8:0;
        if(nui_area_get_width(&a)+2*ext<56||nui_area_get_height(&a)+2*ext<56){fprintf(report,"FAIL target <56 (%d,%d)-(%d,%d)\n",a.x1,a.y1,a.x2,a.y2);failures++;}
    }
    if(nui_obj_check_type(o,&nui_keyboard_class)){
        
        for(unsigned i=0;i<nui_buttonmatrix_get_button_count(o);i++){nui_area_t b;assert(nui_buttonmatrix_get_button_area(o,i,&b));if(nui_area_get_width(&b)<56||nui_area_get_height(&b)<56){fprintf(report,"FAIL keyboard key <56: %s\n",nui_buttonmatrix_get_button_text(o,i));failures++;}}
    }
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++)audit(nui_obj_get_child(o,i),covered);
}
/* Explicit independent source coordinates: SPEC §1, §5 and reference PNG.
 * Search actual public object tree; no production layout constants imported. */
static bool rect_exists(nui_obj_t *o,int x,int y,int w,int h){
    if(nui_obj_has_flag(o,NUI_OBJ_FLAG_HIDDEN))return false;nui_area_t a;nui_obj_get_coords(o,&a);
    if(abs(a.x1-x)<=4&&abs(a.y1-y)<=4&&abs(nui_area_get_width(&a)-w)<=4&&abs(nui_area_get_height(&a)-h)<=4)return true;
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++)if(rect_exists(nui_obj_get_child(o,i),x,y,w,h))return true;return false;
}
static void expect_rect(int x,int y,int w,int h){if(!rect_exists(nui_screen_active(),x,y,w,h)){fprintf(report,"FAIL SPEC rect %d,%d %dx%d\n",x,y,w,h);failures++;}}
static void geometry(const char *name){
    expect_rect(0,0,104,720);for(unsigned i=0;i<5;i++)expect_rect(0,88+i*88,104,88);
    if(strstr(name,"dashboard")){
        expect_rect(120,16,1144,128);expect_rect(120,160,564,376);expect_rect(700,160,564,376);
        expect_rect(120,552,336,152);expect_rect(472,552,272,152);expect_rect(760,552,256,152);expect_rect(1032,552,232,152);
        expect_rect(464,218,200,200);expect_rect(1044,218,200,200);expect_rect(1000,40,244,80);
    }
    if(strstr(name,"chart")){expect_rect(120,104,1144,440);for(int i=0;i<4;i++)expect_rect(120+i*290,560,274,144);}
    if(!strncmp(name,"recording_",10)){expect_rect(120,104,564,236);expect_rect(700,104,564,236);expect_rect(120,356,1144,348);}
    if(strstr(name,"settings")||strstr(name,"numeric")){expect_rect(120,104,564,600);expect_rect(700,104,564,268);expect_rect(700,388,564,316);}
    if(strstr(name,"numeric"))expect_rect(340,52,600,616);
    if(!strncmp(name,"network",7)&&!strstr(name,"keyboard")){expect_rect(120,104,564,380);expect_rect(700,104,564,380);expect_rect(120,500,1144,204);}
}

/* Stable public-tree manifest: exact inclusive bounds, named font and text
 * advance. UTF-8 strings are hex encoded so tabs/newlines cannot split rows.
 * Baseline producer can emit the same schema using its public lv_* APIs. */
NUI_FONT_DECLARE(font_tab5_16);
NUI_FONT_DECLARE(font_tab5_16m);
NUI_FONT_DECLARE(font_tab5_20);
NUI_FONT_DECLARE(font_tab5_20m);
NUI_FONT_DECLARE(font_tab5_24);
NUI_FONT_DECLARE(font_tab5_32);
NUI_FONT_DECLARE(font_tab5_40);
static const char *font_name(const nui_font_t *font){
#define FONT(n) if(font==&font_tab5_##n)return "font_tab5_" #n
    FONT(16);FONT(16m);FONT(20);FONT(20m);FONT(24);FONT(32);FONT(40);FONT(64);
#undef FONT
    if(font==NUI_FONT_DEFAULT)return "font_default_14";
    if(font->dsc==font_tab5_24.dsc&&font->fallback==NUI_FONT_DEFAULT)return "font_tab5_24_with_fallback_14";
    return "other";
}

/* Semantic textarea content is distinct from implementation object children.
 * The reference uses its real child label. Native uses renderer introspection,
 * never a fabricated child or a guessed content rectangle. */
static void dump_textarea_content(nui_obj_t *o,const char *shot,const char *path){
    nui_area_t a={INT32_MIN,INT32_MIN,INT32_MIN,INT32_MIN};
    const nui_font_t *f=nui_obj_get_style_text_font(o,0);
#ifdef TAB5_REFERENCE_PROBE
    nui_obj_t *label=nui_textarea_get_label(o);assert(label);
    nui_obj_get_coords(label,&a);f=nui_obj_get_style_text_font(label,0);
#elif defined(NUI_TEST_HAS_TEXTAREA_BOUNDS)
    assert(nui_native_textarea_get_text_bounds(o,&a));
#endif
    const char *text=nui_textarea_get_text(o);nui_point_t size;
    nui_text_get_size(&size,text,f,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);
    fprintf(textarea_points,"%s\t%s/@text\ttextarea_content\t%d\t%d\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t",shot,path,a.x1,a.y1,a.x2,a.y2,font_name(f),f->line_height,f->base_line,size.x,size.y);
    for(const unsigned char *p=(const unsigned char *)text;*p;p++)fprintf(textarea_points,"%02x",*p);
    fputc('\n',textarea_points);
}
static void dump_points(nui_obj_t *o,const char *shot,const char *path){
    if(nui_obj_has_flag(o,NUI_OBJ_FLAG_HIDDEN))return;
    nui_area_t a;nui_obj_get_coords(o,&a);
    if(nui_obj_check_type(o,&nui_textarea_class))dump_textarea_content(o,shot,path);
    const char *kind="object",*text="",*font="-";int tw=0,th=0,lh=0,bl=0;
    if(nui_obj_check_type(o,&nui_label_class)){
        kind="label";text=nui_label_get_text(o);
        const nui_font_t *f=nui_obj_get_style_text_font(o,0);font=font_name(f);lh=f->line_height;bl=f->base_line;
        nui_point_t size;nui_text_get_size(&size,text,f,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);tw=size.x;th=size.y;
    }else if(nui_obj_check_type(o,&nui_keyboard_class))kind="keyboard";
    else if(nui_obj_check_type(o,&nui_button_class))kind="button";
    else if(nui_obj_check_type(o,&nui_textarea_class))kind="textarea";
    else if(nui_obj_check_type(o,&nui_switch_class))kind="switch";
    else if(nui_obj_check_type(o,&nui_image_class))kind="image";
    else if(nui_obj_check_type(o,&nui_chart_class))kind="chart";
    fprintf(points,"%s\t%s\t%s\t%d\t%d\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t",shot,path,kind,a.x1,a.y1,a.x2,a.y2,font,lh,bl,tw,th);
    for(const unsigned char *p=(const unsigned char *)text;*p;p++)fprintf(points,"%02x",*p);
    fputc('\n',points);
    if(nui_obj_check_type(o,&nui_keyboard_class))for(unsigned i=0;i<nui_buttonmatrix_get_button_count(o);i++){
        nui_area_t b;assert(nui_buttonmatrix_get_button_area(o,i,&b));
        const nui_font_t *kf=nui_obj_get_style_text_font(o,NUI_PART_ITEMS);nui_point_t ks;
        nui_text_get_size(&ks,nui_buttonmatrix_get_button_text(o,i),kf,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);
        fprintf(points,"%s\t%s/key%u\tkey\t%d\t%d\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t",shot,path,i,a.x1+b.x1,a.y1+b.y1,a.x1+b.x2,a.y1+b.y2,font_name(kf),kf->line_height,kf->base_line,ks.x,ks.y);
        const unsigned char *p=(const unsigned char*)nui_buttonmatrix_get_button_text(o,i);for(;p&&*p;p++)fprintf(points,"%02x",*p);fputc('\n',points);
    }
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++){char child[512];snprintf(child,sizeof(child),"%s/%u",path,i);dump_points(nui_obj_get_child(o,i),shot,child);}
}

static void capture(const char *name){
    pump();char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",output,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1280 720\n255\n");
    for(unsigned i=0;i<1280*720;i++){uint8_t rgb[3]={(uint8_t)(pixels[i]>>16),(uint8_t)(pixels[i]>>8),(uint8_t)pixels[i]};fwrite(rgb,1,3,f);}fclose(f);
    dump_points(nui_screen_active(),name,"0");fflush(textarea_points);fflush(points);fprintf(report,"CASE %s\n",name);geometry(name);audit(nui_screen_active(),false);cases++;
}
static void tap(int x,int y){point.x=x;point.y=y;pressed=true;pump();pressed=false;pump();}
static void nav(unsigned page){tap(52,132+page*88);}
static nui_obj_t *nth_textarea(nui_obj_t *o,unsigned *index){if(nui_obj_has_flag(o,NUI_OBJ_FLAG_HIDDEN))return NULL;if(nui_obj_check_type(o,&nui_textarea_class)){if(!(*index)--)return o;}for(unsigned i=0;i<nui_obj_get_child_count(o);i++){nui_obj_t *r=nth_textarea(nui_obj_get_child(o,i),index);if(r)return r;}return NULL;}
static nui_obj_t *textarea(unsigned n){return nth_textarea(nui_screen_active(),&n);}
static void fresh(tab5_ui_state_t *s){s->now_us+=100000;for(unsigned i=0;i<2;i++)if(s->measurement.sensor[i].valid)s->measurement.sensor[i].timestamp_us=s->now_us;tab5_ui_update(s);pump();}
/* Real software-renderer work, after settling initial layout/animations.
 * Timestamp-only samples still enter graph history, but cannot repaint a
 * visually unchanged dashboard. Counters measure actual flushed regions. */
static void idle_regression(tab5_ui_state_t *s,unsigned theme){
    for(unsigned mode=0;mode<2;mode++){
        pump();flushes=flushed_pixels=refreshes=0;
        for(unsigned tick=0;tick<20;tick++){
            if(mode){s->now_us+=100000;for(unsigned j=0;j<2;j++)s->measurement.sensor[j].timestamp_us=s->now_us;}
            tab5_ui_update(s);pump();
        }
        printf("INVALIDATION theme=%u timestamp_only=%u updates=20 flushes=%llu pixels=%llu refreshes=%llu\n",theme,mode,(unsigned long long)flushes,(unsigned long long)flushed_pixels,(unsigned long long)refreshes);
        assert(flushes==0&&flushed_pixels==0&&refreshes==0);
    }
    /* Exercise the custom draw itself, including roll-only ellipse changes. */
    static uint32_t before[1280*720];
    tab5_ui_state_t saved=*s;
    memcpy(before,pixels,sizeof(pixels));s->measurement.sensor[0].x+=1;fresh(s);
    assert(flushed_pixels>0&&memcmp(before,pixels,sizeof(pixels)));
    memcpy(before,pixels,sizeof(pixels));s->measurement.sensor[0].roll_x+=1;fresh(s);
    assert(memcmp(before,pixels,sizeof(pixels)));
    /* Hidden page must present latest values immediately, without a new tick. */
    nav(2);s->measurement.sensor[0].x=3.21f;fresh(s);nav(0);
    assert(find_label(nui_screen_active(),"+3.21°"));
    nav(0);*s=saved;fresh(s);
}
/* Separate fixture so idle/missing tests cannot perturb screenshot history. */
static void graph_idle_regression(unsigned theme){
    tab5_ui_init(action,NULL);
    tab5_ui_state_t s={.light_theme=theme,.now_known=true,.now_us=100000000};
    tab5_measure_config_default(&s.measurement.config);tab5_ui_update(&s);nav(1);pump();
    for(unsigned mode=0;mode<2;mode++){
        flushes=flushed_pixels=refreshes=0;
        for(unsigned i=0;i<20;i++){
            if(mode){s.now_us+=100000;for(unsigned j=0;j<2;j++)s.measurement.sensor[j].timestamp_us=s.now_us;}
            tab5_ui_update(&s);pump();
        }
        printf("GRAPH_IDLE theme=%u missing_timestamp=%u flushes=%llu pixels=%llu refreshes=%llu\n",theme,mode,(unsigned long long)flushes,(unsigned long long)flushed_pixels,(unsigned long long)refreshes);
        assert(!flushes&&!flushed_pixels&&!refreshes);
    }
    /* Empty data must still refresh range, legend, and theme controls. */
    flushes=0;click("30 с");assert(flushes);flushes=0;tap(440,52);assert(flushes);
    flushes=0;s.light_theme=!s.light_theme;fresh(&s);assert(flushes);
    s.measurement.sensor[0].valid=true;s.measurement.sensor[0].good=1;s.measurement.sensor[0].x=1.23f;
    flushes=0;fresh(&s);assert(flushes);
    static uint32_t before[1280*720];memcpy(before,pixels,sizeof(pixels));
    s.now_us+=500000;s.measurement.sensor[0].x=2.34f;
    s.measurement.sensor[0].timestamp_us=s.now_us;flushes=0;tab5_ui_update(&s);pump();
    assert(flushes&&memcmp(before,pixels,sizeof(pixels)));
    printf("GRAPH_CHANGE theme=%u range/legend/theme/fresh samples: PASS\n",theme);
    tab5_ui_destroy();
}
/* Check actual font advances and flushes, not only label strings. */
static void resource_labels(const char *cpu,const char *ram,const char *ps){
    const char *expected[]={cpu,ram,ps};
    for(unsigned i=0;i<3;i++){
        nui_obj_t *o=find_label(nui_screen_active(),expected[i]);assert(o);
        nui_area_t a;nui_obj_get_coords(o,&a);
        assert(a.x1==2&&a.x2==101&&a.y1==550+(int)i*28&&a.y2==573+(int)i*28);
        assert(nui_obj_get_style_text_align(o,0)==NUI_TEXT_ALIGN_CENTER);
        const nui_font_t *f=nui_obj_get_style_text_font(o,0);assert(f->line_height<=24);
        nui_point_t size;nui_text_get_size(&size,expected[i],f,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);
        assert(size.x<=nui_obj_get_width(o)&&size.y<=nui_obj_get_height(o));
        fprintf(report,"RESOURCE %s bounds=(%d,%d)-(%d,%d) glyph=%dx%d\n",expected[i],a.x1,a.y1,a.x2,a.y2,size.x,size.y);
    }
}
static void resources_regression(tab5_ui_state_t *s,unsigned theme){
    char name[80];const char *tag=theme?"light":"dark";
    expect_rect(0,654,104,34);expect_rect(32,690,70,24);
    resource_labels("ЦП —","ОЗУ —","PS —");
    snprintf(name,sizeof(name),"resources_unknown_%s",tag);capture(name);
    s->cpu_known=s->resources_known=true;s->cpu_load_pct=100;
    s->ram_used_bytes=s->ram_total_bytes=UINT32_MAX;
    s->psram_used_bytes=s->psram_total_bytes=UINT32_MAX;
    pump();flushes=flushed_pixels=refreshes=0;tab5_ui_update(s);pump();assert(flushes&&flushed_pixels);
    resource_labels("ЦП 100%","ОЗУ 100%","PS 100%");
    snprintf(name,sizeof(name),"resources_100_%s",tag);capture(name);
    flushes=flushed_pixels=refreshes=0;
    for(unsigned i=0;i<20;i++){tab5_ui_update(s);pump();}
    assert(!flushes&&!flushed_pixels&&!refreshes);
    /* Different byte counts with the same displayed percent also stay idle. */
    s->ram_total_bytes=1000;s->ram_used_bytes=1000;tab5_ui_update(s);pump();assert(!flushes);
    s->cpu_load_pct=255;s->ram_used_bytes=2000;tab5_ui_update(s);pump();assert(!flushes);
    s->cpu_known=false;s->ram_total_bytes=0;tab5_ui_update(s);pump();
    resource_labels("ЦП —","ОЗУ —","PS 100%");
    s->cpu_known=true;s->resources_known=false;s->cpu_load_pct=0;tab5_ui_update(s);pump();
    resource_labels("ЦП 0%","ОЗУ —","PS —");
    s->resources_known=true;s->cpu_load_pct=12;s->ram_total_bytes=1000;s->ram_used_bytes=480;
    s->psram_total_bytes=1000;s->psram_used_bytes=80;
    flushes=0;tab5_ui_update(s);pump();assert(flushes);resource_labels("ЦП 12%","ОЗУ 48%","PS 8%");
    snprintf(name,sizeof(name),"resources_dashboard_%s",tag);capture(name);
    flushes=flushed_pixels=refreshes=0;
    for(unsigned i=0;i<20;i++){tab5_ui_update(s);pump();}
    assert(!flushes&&!flushed_pixels&&!refreshes);
    printf("RESOURCES theme=%u unknown/zero/100/clamp/geometry/change/idle: PASS\n",theme);
}

static nui_obj_t *find_chart(nui_obj_t *o){
    if(nui_obj_check_type(o,&nui_chart_class))return o;
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++){nui_obj_t *r=find_chart(nui_obj_get_child(o,i));if(r)return r;}return NULL;
}

/* Assert the real screen uses an opaque RGB565 image child, not merely a
 * retained series array. Compare rendered flush pixels with source pixels. */
static void raster_overlay_audit(void){
    pump();nui_obj_t *chart=find_chart(nui_screen_active()),*image=NULL;assert(chart);
    for(unsigned i=0;i<nui_obj_get_child_count(chart);i++)if(nui_obj_check_type(nui_obj_get_child(chart,i),&nui_image_class))image=nui_obj_get_child(chart,i);
    assert(image&&!nui_obj_has_flag(image,NUI_OBJ_FLAG_HIDDEN));
    assert(!nui_obj_has_flag(image,NUI_OBJ_FLAG_CLICKABLE));
    const nui_image_dsc_t *src=nui_image_get_src(image);assert(src&&src->data);
    assert(src->header.cf==NUI_COLOR_FORMAT_RGB565&&src->header.w==1040&&src->header.h==364&&src->header.stride==2080);
    assert(src->data_size==1040*364*2);
    assert(nui_obj_get_style_opa(image,0)==255&&nui_obj_get_style_image_opa(image,0)==255);
    nui_area_t a,c;nui_obj_get_coords(image,&a);nui_obj_get_coords(chart,&c);assert(a.x1==c.x1&&a.y1==c.y1&&a.x2==c.x2&&a.y2==c.y2);
    const uint16_t *rgb=(const uint16_t*)src->data;unsigned nonzero=0,nonflat=0,checked=0;
    uint16_t background=rgb[1040+1];
    for(unsigned i=0;i<1040*364;i++){nonzero+=rgb[i]!=0;nonflat+=rgb[i]!=background;}
    assert(nonzero>1000&&nonflat>1000);
    for(unsigned y=3;y<364;y+=19)for(unsigned x=3;x<1040;x+=37){
        uint16_t actual=nui_color_to_u16(nui_color_hex(pixels[(a.y1+y)*1280+a.x1+x]));
        assert(actual==rgb[y*1040+x]);checked++;
    }
    unsigned color_count[4]={0},k=0;
    for(nui_chart_series_t *ser=nui_chart_get_series_next(chart,NULL);ser;ser=nui_chart_get_series_next(chart,ser)){
        uint16_t color=nui_color_to_u16(nui_chart_get_series_color(chart,ser));
        for(unsigned i=0;i<1040*364;i++)color_count[k]+=rgb[i]==color;
        assert(color_count[k]>50);k++;
    }assert(k==4);
    printf("RASTER_OVERLAY opaque RGB5651040x364 actual flush samples=%u curve_pixels=%u/%u/%u/%u PASS\n",checked,color_count[0],color_count[1],color_count[2],color_count[3]);
}

/* Compare the actual LVGL external arrays with the render contract for all
 * four series; series traversal order is deliberately not assumed. */
static void graph_arrays(const tab5_ui_graph_t *history,unsigned seconds){
    /* Range selection is an immediate user request, bypassing the 5Hz plot
     * budget. Compare complete curves independently of snapshot phase. */
    click(seconds==30?"30 с":"2 мин");
    nui_obj_t *chart=find_chart(nui_screen_active());assert(chart);
    unsigned n=seconds==30?300:240;assert(nui_chart_get_point_count(chart)==n);
    bool matched[4]={0};unsigned count=0;int32_t expected[1200],raw[1200];
    for(nui_chart_series_t *series=nui_chart_get_series_next(chart,NULL);series;series=nui_chart_get_series_next(chart,series)){
        int32_t *actual=nui_chart_get_series_y_array(chart,series);bool found=false;
        for(unsigned k=0;k<4;k++){
            assert(tab5_ui_graph_read_render(history,seconds,k,expected)==n);
            if(seconds==120){assert(tab5_ui_graph_read(history,seconds,k,raw)==n);assert(!memcmp(raw,expected,n*sizeof(*raw)));}
            if(!matched[k]&&!memcmp(actual,expected,n*sizeof(*actual))){matched[k]=true;found=true;break;}
        }
        assert(found);count++;
    }
    assert(count==4);
}
static void graph_5hz_regression(unsigned theme){
    tab5_ui_init(action,NULL);
    tab5_ui_state_t s={.light_theme=theme,.now_known=true,.now_us=100000000};
    tab5_measure_config_default(&s.measurement.config);s.measurement.config.frequency_hz=5;s.measurement.config.gap_ms=20;
    tab5_ui_graph_t history;tab5_ui_graph_reset(&history);
    tab5_ui_update(&s);tab5_ui_graph_sample(&history,&s);nav(1);click("30 с");
    /* Explicitly synthetic input, 5Hz replies and 100ms UI snapshots, 120s
     * retained history. Distinct channels make series comparisons meaningful. */
    for(unsigned tick=0;tick<=1200;tick++){
        s.now_us=100000000+(uint64_t)tick*100000;
        if(!(tick%2))for(unsigned j=0;j<2;j++){
            tab5_measure_sensor_t *m=&s.measurement.sensor[j];m->valid=true;m->good++;m->timestamp_us=s.now_us;
            m->x=1.2f+j+.35f*sinf(tick*.015f);m->y=-.4f-j+.15f*cosf(tick*.015f);
        }
        tab5_ui_graph_sample(&history,&s);tab5_ui_update(&s);
        if(tick%100==0)pump();
    }
    pump();graph_arrays(&history,30);
    int32_t values[1200];assert(tab5_ui_graph_read_render(&history,30,0,values)==300);
    for(unsigned i=1;i<300;i++)assert(values[i]!=NUI_CHART_POINT_NONE);
    char name[120];const char *tag=theme?"light":"dark";
    snprintf(name,sizeof(name),"chart_5hz_synthetic_continuous_30s_%s",tag);capture(name);raster_overlay_audit();
    /* Pending half-cycle: no extrapolation, but current numerics stay visible. */
    s.now_us+=100000;tab5_ui_graph_sample(&history,&s);tab5_ui_update(&s);pump();graph_arrays(&history,30);
    tab5_ui_graph_read_render(&history,30,0,values);assert(values[299]==NUI_CHART_POINT_NONE&&values[298]!=NUI_CHART_POINT_NONE);
    for(unsigned j=0;j<2;j++)for(unsigned axis=0;axis<2;axis++){
        float v=axis?s.measurement.sensor[j].y:s.measurement.sensor[j].x;char text[40];
        snprintf(text,sizeof(text),"%s%.2f°",v<0?"−":"+",fabs(v));assert(find_label(nui_screen_active(),text));
    }
    snprintf(name,sizeof(name),"chart_5hz_synthetic_pending_readings_%s",tag);capture(name);
    click("2 мин");graph_arrays(&history,120);tab5_ui_graph_read_render(&history,120,0,values);
    for(unsigned i=0;i<240;i++)assert(values[i]!=NUI_CHART_POINT_NONE);
    snprintf(name,sizeof(name),"chart_5hz_synthetic_2min_%s",tag);capture(name);click("30 с");
    /* A failed poll retains valid last-good data, but increments errors.
     * Recovery must not bridge that failed edge; healthy peer stays continuous. */
    for(unsigned tick=2;tick<=4;tick++){
        s.now_us=220000000+(uint64_t)tick*100000;
        if(tick==2)s.measurement.sensor[0].errors++;
        if(!(tick%2))for(unsigned j=0;j<2;j++)if(j||tick==4){s.measurement.sensor[j].good++;s.measurement.sensor[j].timestamp_us=s.now_us;}
        tab5_ui_graph_sample(&history,&s);tab5_ui_update(&s);pump();
    }
    graph_arrays(&history,30);tab5_ui_graph_read_render(&history,30,0,values);
    for(unsigned i=1;i<300;i++)assert(values[i]!=NUI_CHART_POINT_NONE);
    tab5_ui_graph_read(&history,30,0,values);assert(values[298]==NUI_CHART_POINT_NONE&&values[297]==NUI_CHART_POINT_NONE);
    tab5_ui_graph_read_render(&history,30,2,values);for(unsigned i=1;i<300;i++)assert(values[i]!=NUI_CHART_POINT_NONE);
    snprintf(name,sizeof(name),"chart_5hz_synthetic_failed_gap_%s",tag);capture(name);
    /* 600ms silence exceeds the current 500ms presentation bridge bound. */
    for(unsigned tick=0;tick<6;tick++){
        s.now_us+=100000;
        if(tick==5)for(unsigned j=0;j<2;j++){s.measurement.sensor[j].good++;s.measurement.sensor[j].timestamp_us=s.now_us;}
        tab5_ui_graph_sample(&history,&s);tab5_ui_update(&s);pump();
        if(tick==2){tab5_ui_graph_read_render(&history,30,2,values);assert(values[297]==NUI_CHART_POINT_NONE&&values[298]==NUI_CHART_POINT_NONE&&values[299]==NUI_CHART_POINT_NONE);}
    }
    graph_arrays(&history,30);tab5_ui_graph_read_render(&history,30,2,values);assert(values[299]!=NUI_CHART_POINT_NONE&&values[298]==NUI_CHART_POINT_NONE);
    /* Offline while the latest plotted cell is already empty: the array
     * cache alone cannot detect this change, but numeric cards must clear. */
    s.now_us+=100000;tab5_ui_graph_sample(&history,&s);tab5_ui_update(&s);pump();
    click("30 с");nui_obj_t *chart=find_chart(nui_screen_active());int32_t before[4][300];unsigned index=0;
    for(nui_chart_series_t *ser=nui_chart_get_series_next(chart,NULL);ser;ser=nui_chart_get_series_next(chart,ser)){
        memcpy(before[index++],nui_chart_get_series_y_array(chart,ser),sizeof(before[0]));
    }
    char latest[40];snprintf(latest,sizeof(latest),"%s%.2f°",s.measurement.sensor[0].x<0?"−":"+",fabs(s.measurement.sensor[0].x));
    assert(find_label(nui_screen_active(),latest));
    nav(0);s.measurement.sensor[0].valid=false;tab5_ui_update(&s);nav(1);pump();
    assert(!find_label(nui_screen_active(),latest));index=0;
    for(nui_chart_series_t *ser=nui_chart_get_series_next(chart,NULL);ser;ser=nui_chart_get_series_next(chart,ser)){
        assert(!memcmp(before[index++],nui_chart_get_series_y_array(chart,ser),sizeof(before[0])));
    }
    snprintf(name,sizeof(name),"chart_5hz_synthetic_offline_empty_tail_%s",tag);capture(name);
    printf("GRAPH_5HZ_OFFLINE theme=%u unchanged empty tail clears numeric cards PASS\n",theme);
    printf("GRAPH_5HZ theme=%u synthetic: continuous 30s / unchanged raw 2min / pending numerics / raw errors++ gap / 600ms silence PASS\n",theme);
    tab5_ui_destroy();
}


/* Synthetic large decoded steps with deliberately stale EMA/filtered fields.
 * Graph points still require real timestamp/good progress; no UI-made samples. */
static void live_unfiltered_regression(unsigned theme){
    tab5_ui_init(action,NULL);tab5_ui_state_t s={.light_theme=theme,.now_known=true,.now_us=300000000};
    tab5_measure_config_default(&s.measurement.config);s.measurement.config.alpha=.01f;
    for(unsigned j=0;j<2;j++)s.measurement.sensor[j]=(tab5_measure_sensor_t){
        .valid=true,.good=1,.timestamp_us=s.now_us,.x=.1f+j,.y=.2f+j,
        .filtered_x=2.6f+j,.filtered_y=-1.55f+j,.decoded_x=15+j,.decoded_y=-10.f-j,
        .zero_x=2.5f,.zero_y=-1.75f,.roll_x=.14f,.roll_y=.05f,.calm_x=true,.calm_y=true};
    fresh(&s);assert(find_label(nui_screen_active(),"+0.10°"));
    /* Keep a raster of the level to verify that the angle actually drives it. */
    uint32_t level_before[200*200];for(unsigned y=0;y<200;y++)memcpy(level_before+y*200,pixels+(218+y)*1280+464,200*sizeof(uint32_t));
    s.live_unfiltered=true;tab5_ui_state_t input=s;fresh(&s);
    /* fresh advances only fixture time; compare the actual caller snapshot. */
    input=s;tab5_ui_update(&s);pump();assert(!memcmp(&input,&s,sizeof(s)));
    assert(find_label(nui_screen_active(),"+12.50°"));assert(find_label(nui_screen_active(),"−8.25°"));
    bool level_changed=false;for(unsigned y=0;y<200;y++)if(memcmp(level_before+y*200,pixels+(218+y)*1280+464,200*sizeof(uint32_t)))level_changed=true;assert(level_changed);
    assert(s.measurement.sensor[0].x==.1f&&s.measurement.sensor[0].zero_x==2.5f&&s.measurement.sensor[0].roll_x==.14f);
    char name[120];const char *tag=theme?"light":"dark";
    snprintf(name,sizeof(name),"dashboard_live_unfiltered_synthetic_%s",tag);capture(name);
    /* A new raw reply jumps immediately even though filtered state is frozen. */
    s.measurement.sensor[0].decoded_x=25;s.measurement.sensor[0].good++;fresh(&s);
    assert(find_label(nui_screen_active(),"+22.50°"));assert(!find_label(nui_screen_active(),"+12.50°"));
    s.recording=true;s.sd.known=s.sd.mounted=s.sd.recording=true;s.sd.written=123;
    input=s;tab5_ui_update(&s);pump();assert(!memcmp(&input,&s,sizeof(s)));
    assert(find_label(nui_screen_active(),"■ Стоп записи"));
    nav(1);click("30 с");
    const char *caption="Угол без сглаживания · Коснитесь графика для отсчёта";
    nui_obj_t *caption_label=find_label(nui_screen_active(),caption);assert(caption_label);
    const nui_font_t *font=nui_obj_get_style_text_font(caption_label,0);
    /* Ask the existing font directly: fallback/substitution cannot mask a
     * missing glyph. Caption uses only its existing ASCII/Cyrillic and dot. */
    for(const unsigned char *p=(const unsigned char*)caption;*p;){
        uint32_t cp=*p++;if(cp>=0xc0){unsigned extra=cp<0xe0?1:cp<0xf0?2:3;cp&=extra==1?31:extra==2?15:7;while(extra--){assert((*p&0xc0)==0x80);cp=(cp<<6)|(*p++&63);}}
        nui_font_glyph_dsc_t glyph;assert(font->get_glyph_dsc(font,&glyph,cp,0));
    }
    nui_point_t caption_size;nui_text_get_size(&caption_size,caption,font,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);
    assert(caption_size.x<=nui_obj_get_width(caption_label)&&caption_size.y<=nui_obj_get_height(caption_label));
    printf("GRAPH_RAW_CAPTION theme=%u existing glyphs and bounds PASS\n",theme);
    nui_obj_t *chart=find_chart(nui_screen_active());bool found=false;
    for(nui_chart_series_t *ser=nui_chart_get_series_next(chart,NULL);ser;ser=nui_chart_get_series_next(chart,ser)){
        int32_t *v=nui_chart_get_series_y_array(chart,ser);if(v[299]==2250)found=true;
    }assert(found);assert(find_label(nui_screen_active(),"+22.50°"));
    /* No newer reply: latest cards persist, trailing plot cell stays empty. */
    s.now_us+=100000;input=s;tab5_ui_update(&s);pump();assert(!memcmp(&input,&s,sizeof(s)));
    assert(find_label(nui_screen_active(),"+22.50°"));click("30 с");
    for(nui_chart_series_t *ser=nui_chart_get_series_next(chart,NULL);ser;ser=nui_chart_get_series_next(chart,ser))assert(nui_chart_get_series_y_array(chart,ser)[299]==NUI_CHART_POINT_NONE);
    snprintf(name,sizeof(name),"chart_live_unfiltered_synthetic_%s",tag);capture(name);
    printf("LIVE_UNFILTERED theme=%u decoded-zero step / level raster / current graph / no synthetic tail / input and recording preserved PASS\n",theme);
    tab5_ui_destroy();
}

static unsigned plot_draws;
static void count_plot_draw(nui_event_t *e){(void)e;plot_draws++;}
static void graph_motion_benchmark(unsigned theme){
    tab5_ui_init(action,NULL);tab5_ui_state_t s={.light_theme=theme,.now_known=true,.now_us=400000000,.live_unfiltered=true};
    tab5_measure_config_default(&s.measurement.config);s.measurement.config.frequency_hz=10;
    /* Identical deterministic synthetic motion before/after UI optimization. */
    for(unsigned tick=0;tick<1200;tick++){
        s.now_us+=100000;for(unsigned j=0;j<2;j++)s.measurement.sensor[j]=(tab5_measure_sensor_t){.valid=true,.good=tick+1,.timestamp_us=s.now_us,.decoded_x=1+j+.5f*sinf(tick*.1f),.decoded_y=-.5f-j+.2f*cosf(tick*.1f)};
        tab5_ui_update(&s);
    }
    nav(1);click("30 с");pump();raster_overlay_audit();
    nui_obj_t *chart=find_chart(nui_screen_active());nui_obj_add_event_cb(chart,count_plot_draw,NUI_EVENT_DRAW_MAIN,NULL);
    plot_draws=0;flushes=flushed_pixels=refreshes=0;double started=bench_now_ms(),update_ms=0,render_ms=0,max_render_ms=0;flush_time_ms=0;
    for(unsigned tick=0;tick<120;tick++){
        s.now_us+=100000;for(unsigned j=0;j<2;j++){s.measurement.sensor[j].good++;s.measurement.sensor[j].timestamp_us=s.now_us;s.measurement.sensor[j].decoded_x=1+j+.5f*sinf((1200+tick)*.1f);s.measurement.sensor[j].decoded_y=-.5f-j+.2f*cosf((1200+tick)*.1f);}
        double t=bench_now_ms();tab5_ui_update(&s);update_ms+=bench_now_ms()-t;
        t=bench_now_ms();pump();double render=bench_now_ms()-t;render_ms+=render;if(render>max_render_ms)max_render_ms=render;
        char value[40];snprintf(value,sizeof(value),"+%.2f°",s.measurement.sensor[0].decoded_x);assert(find_label(nui_screen_active(),value));
    }
    double elapsed=bench_now_ms()-started;
    printf("DRAW_COST format=%s theme=%u update_ms=%.3f render_pump_ms=%.3f mock_flush_ms=%.3f max_render_ms=%.3f\n",rgb565_mode?"RGB565":"XRGB8888",theme,update_ms,render_ms,flush_time_ms,max_render_ms);
    printf("GRAPH_BENCH theme=%u updates=120 elapsed_ms=%.1f per_update_ms=%.3f plot_draw_events=%u pixels=%llu refreshes=%llu\n",theme,elapsed,elapsed/120,plot_draws,(unsigned long long)flushed_pixels,(unsigned long long)refreshes);
    tab5_ui_destroy();
}


static void measure_motion_benchmark(unsigned theme){
    tab5_ui_init(action,NULL);tab5_ui_state_t s={.light_theme=theme,.now_known=true,.now_us=600000000,.live_unfiltered=true};tab5_measure_config_default(&s.measurement.config);
    fresh(&s);pump();double started=bench_now_ms(),update_ms=0,render_ms=0,max_render_ms=0;flush_time_ms=0;flushed_pixels=refreshes=0;
    for(unsigned tick=0;tick<100;tick++){
        s.now_us+=100000;for(unsigned j=0;j<2;j++)s.measurement.sensor[j]=(tab5_measure_sensor_t){.valid=true,.good=tick+1,.timestamp_us=s.now_us,.decoded_x=20*sinf(tick*.7f+j),.decoded_y=20*cosf(tick*.9f+j),.roll_x=(float)((tick*37)%180),.roll_y=(float)((tick*53)%180)};
        double t=bench_now_ms();tab5_ui_update(&s);update_ms+=bench_now_ms()-t;
        t=bench_now_ms();pump();double render=bench_now_ms()-t;render_ms+=render;if(render>max_render_ms)max_render_ms=render;
    }
    double elapsed=bench_now_ms()-started;
    printf("DRAW_COST format=%s theme=%u update_ms=%.3f render_pump_ms=%.3f mock_flush_ms=%.3f max_render_ms=%.3f\n",rgb565_mode?"RGB565":"XRGB8888",theme,update_ms,render_ms,flush_time_ms,max_render_ms);
    printf("MEASURE_BENCH theme=%u updates=100 elapsed_ms=%.1f per_update_ms=%.3f pixels=%llu refreshes=%llu\n",theme,elapsed,elapsed/100,(unsigned long long)flushed_pixels,(unsigned long long)refreshes);
    tab5_ui_destroy();
}


/* Native public notifications describe actual synchronous primitives, with
 * their owning object. They remain valid when opaque-cover skips parent draw
 * events; no parent MAIN/POST pairing or accumulated global scope is assumed. */
static unsigned level_images,level_vectors,level_masks;
static void level_task_audit(nui_event_t *e){
    nui_draw_task_t *task=nui_event_get_draw_task(e);nui_draw_task_type_t type=nui_draw_task_get_type(task);
    if(type==NUI_DRAW_TASK_TYPE_IMAGE)level_images++;
    if(type==NUI_DRAW_TASK_TYPE_BORDER||type==NUI_DRAW_TASK_TYPE_ARC||type==NUI_DRAW_TASK_TYPE_LINE)level_vectors++;
    if(type==NUI_DRAW_TASK_TYPE_MASK_RECTANGLE||type==NUI_DRAW_TASK_TYPE_MASK_BITMAP)level_masks++;
}
static void collect_level_images(nui_obj_t *o,nui_obj_t **images,unsigned *count){
    if(nui_obj_check_type(o,&nui_image_class)&&nui_obj_get_width(o)==200&&nui_obj_get_height(o)==200){assert(*count<2);images[(*count)++]=o;return;}
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++)collect_level_images(nui_obj_get_child(o,i),images,count);
}
static void level_overlay_regression(unsigned theme){
    tab5_ui_init(action,NULL);tab5_ui_state_t s={.light_theme=theme,.now_known=true,.now_us=800000000,.live_unfiltered=true};tab5_measure_config_default(&s.measurement.config);
    for(unsigned i=0;i<2;i++)s.measurement.sensor[i]=(tab5_measure_sensor_t){.valid=true,.good=1,.timestamp_us=s.now_us,.decoded_x=1,.decoded_y=2,.zero_x=.5f,.zero_y=.25f};
    tab5_ui_state_t input=s;tab5_ui_update(&s);pump();assert(!memcmp(&s,&input,sizeof(s)));
    nui_obj_t *images[2];unsigned count=0;collect_level_images(nui_screen_active(),images,&count);assert(count==2);
    for(unsigned i=0;i<2;i++){
        nui_obj_t *parent=nui_obj_get_parent(images[i]);assert(nui_obj_get_child(parent,0)==images[i]);
        const nui_image_dsc_t *d=nui_image_get_src(images[i]);assert(d&&d->header.cf==NUI_COLOR_FORMAT_RGB565&&d->header.w==200&&d->header.h==200&&d->header.stride==400&&d->data_size==80000);
        assert(nui_obj_get_style_opa(images[i],0)==255&&nui_obj_get_style_image_opa(images[i],0)==255);
        nui_area_t a,b;nui_obj_get_coords(images[i],&a);nui_obj_get_coords(parent,&b);assert(!memcmp(&a,&b,sizeof(a)));
        const uint16_t *rgb=(const uint16_t*)d->data;uint16_t sensor=nui_color_to_u16(nui_color_hex(theme?(i?0xE06B0D:0x1F6BCC):(i?0xFF9E40:0x5C9EFF)));
        int px,py;assert(tab5_level_position(.5f,1.75f,true,&px,&py));assert(rgb[py*200+px]==sensor);
        unsigned marker=0;for(unsigned j=0;j<40000;j++)marker+=rgb[j]==sensor;assert(marker>=140);
        assert(rgb[0]!=rgb[50*200+50]);assert(rgb[196*200+100]!=rgb[0]);
        const unsigned sample[][2]={{0,0},{50,50},{100,100},{100,196},{190,190}};
        for(unsigned j=0;j<5;j++){unsigned x=sample[j][0],y=sample[j][1];assert(nui_color_to_u16(nui_color_hex(pixels[(a.y1+y)*1280+a.x1+x]))==rgb[y*200+x]);}
        assert(nui_obj_get_style_prop(parent,0,NUI_STYLE_BORDER_WIDTH).num==0);
        assert(nui_obj_get_style_prop(parent,0,NUI_STYLE_RADIUS).num==0);
        nui_obj_add_flag(parent,NUI_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);nui_obj_add_event_cb(parent,level_task_audit,NUI_EVENT_DRAW_TASK_ADDED,NULL);
        nui_obj_add_flag(images[i],NUI_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);nui_obj_add_event_cb(images[i],level_task_audit,NUI_EVENT_DRAW_TASK_ADDED,NULL);
    }
    level_images=level_vectors=level_masks=0;
    for(unsigned tick=0;tick<20;tick++){
        s.now_us+=100000;for(unsigned i=0;i<2;i++){s.measurement.sensor[i].good++;s.measurement.sensor[i].timestamp_us=s.now_us;s.measurement.sensor[i].decoded_x=3*sinf(tick*.4f+i);s.measurement.sensor[i].decoded_y=3*cosf(tick*.4f+i);}
        input=s;tab5_ui_update(&s);pump();assert(!memcmp(&input,&s,sizeof(s)));
    }
#if NUI_PROFILE_FINE_COUNTERS
    assert(level_images>0&&level_vectors==0&&level_masks==0);
#else
    printf("RELEASE_CAPTURE primitive notifications not asserted; diagnostic fine1 is mandatory separately\n");
#endif
    printf("LEVEL_OVERLAY theme=%u RGB565 first-child / marker-color / flush pixels / input immutable: image_primitives=%u vector_ring_primitives=%u masks=%u PASS\n",theme,level_images,level_vectors,level_masks);
    /* Fresh good replies with sub-pixel angle movement cannot regenerate
     * or invalidate either level image. No timestamps are frozen here. */
    uint16_t before[2][40000];
    for(unsigned i=0;i<2;i++){const nui_image_dsc_t *d=nui_image_get_src(images[i]);memcpy(before[i],d->data,sizeof(before[i]));}
    level_images=level_vectors=level_masks=0;
    s.now_us+=100000;for(unsigned i=0;i<2;i++){s.measurement.sensor[i].good++;s.measurement.sensor[i].timestamp_us=s.now_us;s.measurement.sensor[i].decoded_x+=.000001f;}
    input=s;tab5_ui_update(&s);pump();assert(!memcmp(&input,&s,sizeof(s)));
    for(unsigned i=0;i<2;i++){const nui_image_dsc_t *d=nui_image_get_src(images[i]);assert(!memcmp(before[i],d->data,sizeof(before[i])));}
#if NUI_PROFILE_FINE_COUNTERS
    assert(!level_images&&!level_vectors&&!level_masks);
#endif
    printf("LEVEL_QUANTIZED theme=%u fresh subpixel movement image_primitives=0 buffer unchanged PASS\n",theme);
    char name[100];snprintf(name,sizeof(name),"dashboard_level_raster_synthetic_%s",theme?"light":"dark");capture(name);
    tab5_ui_destroy();
}


/* Extra input regressions run after the immutable 98 capture scenarios.
 * Their map/default contracts are independent JSONL, not baseline screenshots. */
static void hex_string(FILE *file,const char *text){for(const unsigned char *p=(const unsigned char*)text;*p;p++)fprintf(file,"%02x",*p);}
static void dump_keyboard_defaults(const char *context,unsigned theme){
    nui_obj_t *k=keyboard(nui_screen_active());assert(k);pump();nui_area_t a;nui_obj_get_coords(k,&a);
    nui_area_t areas[256];const char *texts[256];size_t n=nui_native_keyboard_get_keys(k,areas,texts,256);assert(n<=256);
    fprintf(contracts,"{\"kind\":\"keyboard\",\"context\":\"%s\",\"theme\":%u,\"keyboard_rect\":[%d,%d,%d,%d],\"keys\":[",context,theme,a.x1,a.y1,a.x2,a.y2);
    for(unsigned i=0;i<n;i++){nui_area_t b=areas[i];fprintf(contracts,"%s{\"id\":%u,\"screen_rect\":[%d,%d,%d,%d],\"width\":%d,\"height\":%d,\"text_hex\":\"",i?",":"",i,b.x1,b.y1,b.x2,b.y2,nui_area_get_width(&b),nui_area_get_height(&b));hex_string(contracts,texts[i]);fprintf(contracts,"\"}");}
    fprintf(contracts,"]}\n");fflush(contracts);
}
static void dump_textarea_defaults(nui_obj_t *o,const char *context,unsigned index,unsigned theme){
    nui_area_t a;nui_obj_get_coords(o,&a);const nui_font_t *f=nui_obj_get_style_text_font(o,0);
    fprintf(contracts,"{\"kind\":\"textarea\",\"context\":\"%s\",\"index\":%u,\"theme\":%u,\"rect\":[%d,%d,%d,%d],\"width\":%d,\"height\":%d",context,index,theme,a.x1,a.y1,a.x2,a.y2,nui_obj_get_width(o),nui_obj_get_height(o));
#define FIELD(name,prop) fprintf(contracts,",\"" name "\":%ld",(long)nui_obj_get_style_prop(o,0,prop).num)
    FIELD("pad_left",NUI_STYLE_PAD_LEFT);FIELD("pad_right",NUI_STYLE_PAD_RIGHT);FIELD("pad_top",NUI_STYLE_PAD_TOP);FIELD("pad_bottom",NUI_STYLE_PAD_BOTTOM);FIELD("border_width",NUI_STYLE_BORDER_WIDTH);FIELD("radius",NUI_STYLE_RADIUS);FIELD("opa",NUI_STYLE_OPA);FIELD("bg_opa",NUI_STYLE_BG_OPA);
#undef FIELD
    fprintf(contracts,",\"line_height\":%u,\"base_line\":%d,\"font_is_tab5_24\":%s}\n",f->line_height,f->base_line,f->dsc==font_tab5_24.dsc?"true":"false");fflush(contracts);
}
static void expect_editor(const char *text){assert(textarea(0)&&!strcmp(nui_textarea_get_text(textarea(0)),text));}
static void editor_open(unsigned field_index){
    tap(field_index<2?400:980,field_index%2?272:204);assert(textarea(0));
}
static void set_editor_draft(unsigned field_index,const char *text){editor_open(field_index);nui_textarea_set_text(textarea(0),text);click("Готово");assert(!keyboard(nui_screen_active()));}
static void acknowledge_network(tab5_ui_state_t *s){assert(last.kind==TAB5_UI_NETWORK);s->network=last.network;fresh(s);}
static nui_obj_t *visible_switch(nui_obj_t *o,unsigned *index){
    if(nui_obj_has_flag(o,NUI_OBJ_FLAG_HIDDEN))return NULL;
    if(nui_obj_check_type(o,&nui_switch_class)){if(!(*index)--)return o;}
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++){nui_obj_t *r=visible_switch(nui_obj_get_child(o,i),index);if(r)return r;}return NULL;
}
static nui_obj_t *network_switch(unsigned index){nui_obj_t *o=visible_switch(nui_screen_active(),&index);assert(o);return o;}
static void keyboard_interaction_regression(unsigned theme){
    tab5_ui_init(action,NULL);tab5_ui_state_t s={.light_theme=theme,.now_known=true,.now_us=900000000,.datetime_known=true,.datetime={2026,10,22,14,32,5}};
    tab5_measure_config_default(&s.measurement.config);s.network.ap_enabled=true;s.network.sta_enabled=true;
    strcpy(s.network.ap_ssid,"ParityAP");strcpy(s.network.sta_ssid,"ParitySTA");strcpy(s.network.ap_password,"valid-ap-123");strcpy(s.network.sta_password,"valid-sta-123");fresh(&s);nav(4);editor_open(0);
    if(find_label(nui_screen_active(),"РУС → EN"))click("РУС → EN");
    dump_textarea_defaults(textarea(0),"open_text",0,theme);dump_keyboard_defaults("keyboard_en_lower",theme);
    nui_textarea_set_text(textarea(0),"");key_click("q");key_click("ABC");dump_keyboard_defaults("keyboard_en_upper",theme);key_click("Q");key_click("abc");expect_editor("qQ");
    key_click("1#");dump_keyboard_defaults("keyboard_special",theme);key_click("&");key_click("abc");expect_editor("qQ&");
    nui_textarea_set_text(textarea(0),"");key_click("a");key_click(" ");key_click("b");expect_editor("a b");key_click(NUI_SYMBOL_LEFT);key_click("x");expect_editor("a xb");key_click(NUI_SYMBOL_RIGHT);key_click("y");expect_editor("a xby");
    nui_obj_t *k=keyboard(nui_screen_active());
    /* Public mode setter takes int; the currently reserved user slots 5..7
     * must default to the same lower map as baseline USER_2..USER_4. */
    for(unsigned i=0;i<3;i++){nui_keyboard_set_mode(k,5+(int)i);const char *names[]={"keyboard_user_2_default","keyboard_user_3_default","keyboard_user_4_default"};dump_keyboard_defaults(names[i],theme);}
    nui_keyboard_set_mode(k,NUI_KEYBOARD_MODE_TEXT_LOWER);click("EN → РУС");dump_keyboard_defaults("keyboard_custom_ru",theme);
    nui_textarea_set_text(textarea(0),"");key_click("я");key_click("ю");expect_editor("яю");key_click(NUI_SYMBOL_LEFT);key_click(NUI_SYMBOL_BACKSPACE);expect_editor("ю");key_click(NUI_SYMBOL_RIGHT);key_click("я");expect_editor("юя");key_click(NUI_SYMBOL_BACKSPACE);expect_editor("ю");
    unsigned before=callbacks;key_click(NUI_SYMBOL_OK);assert(!keyboard(nui_screen_active())&&callbacks==before);tap(1150,52);assert(callbacks==before+1&&last.kind==TAB5_UI_NETWORK&&!strcmp(last.network.ap_ssid,"ю"));acknowledge_network(&s);
    editor_open(0);nui_textarea_set_text(textarea(0),"DiscardReadyCancel");key_click(NUI_SYMBOL_CLOSE);assert(!keyboard(nui_screen_active())&&callbacks==before+1);editor_open(0);expect_editor("ю");click("Отмена");assert(callbacks==before+1);
    /* Every numeric date field is focused by actual touch and edited through
     * keyboard keys, including leading zeros and a Gregorian leap day. */
    nav(3);click("Изменить");dump_keyboard_defaults("keyboard_number",theme);
    const char *values[]={"2028","02","29","23","59","58"};
    for(unsigned i=0;i<6;i++){
        nui_obj_t *o=textarea(i);assert(o);dump_textarea_defaults(o,"open_time",i,theme);click_obj(o);
        size_t length=strlen(nui_textarea_get_text(o));for(size_t j=0;j<length;j++)key_click(NUI_SYMBOL_BACKSPACE);assert(!*nui_textarea_get_text(o));
        for(const char *p=values[i];*p;p++){char digit[]={*p,0};key_click(digit);}assert(!strcmp(nui_textarea_get_text(o),values[i]));
    }
    before=callbacks;click("Готово");assert(callbacks==before+1&&last.kind==TAB5_UI_TIME&&last.datetime.year==2028&&last.datetime.month==2&&last.datetime.day==29&&last.datetime.hour==23&&last.datetime.minute==59&&last.datetime.second==58);
    click("Изменить");click("Отмена");assert(callbacks==before+1&&!keyboard(nui_screen_active()));
    tab5_ui_destroy();printf("KEYBOARD_INPUT theme=%u uppercase/symbols/space/cursor/UTF8-backspace/ready/cancel/all-six-date-fields PASS\n",theme);
}
static void network_switch_regression(unsigned theme){
    tab5_ui_init(action,NULL);tab5_ui_state_t s={.light_theme=theme,.now_known=true,.now_us=950000000};tab5_measure_config_default(&s.measurement.config);
    s.network.ap_enabled=true;s.network.sta_enabled=false;strcpy(s.network.ap_ssid,"SwitchAP");strcpy(s.network.sta_ssid,"SwitchSTA");strcpy(s.network.ap_password,"valid-ap-123");strcpy(s.network.sta_password,"valid-sta-123");fresh(&s);nav(4);
    assert(nui_obj_has_state(network_switch(0),NUI_STATE_CHECKED)&&!nui_obj_has_state(network_switch(1),NUI_STATE_CHECKED));
    unsigned before=callbacks;click_obj(network_switch(0));click_obj(network_switch(1));assert(callbacks==before&&!nui_obj_has_state(network_switch(0),NUI_STATE_CHECKED)&&nui_obj_has_state(network_switch(1),NUI_STATE_CHECKED));
    set_editor_draft(3,"short");tap(1150,52);assert(callbacks==before);set_editor_draft(3,"");tap(1150,52);assert(callbacks==before+1&&last.kind==TAB5_UI_NETWORK&&!last.network.ap_enabled&&last.network.sta_enabled&&!last.network.sta_password[0]);acknowledge_network(&s);
    click_obj(network_switch(0));click_obj(network_switch(1));set_editor_draft(1,"short");before=callbacks;tap(1150,52);assert(callbacks==before);
    set_editor_draft(1,"fixed-ap-123");tap(1150,52);assert(callbacks==before+1&&last.kind==TAB5_UI_NETWORK&&last.network.ap_enabled&&!last.network.sta_enabled&&!strcmp(last.network.ap_password,"fixed-ap-123"));acknowledge_network(&s);
    tab5_ui_destroy();printf("NETWORK_SWITCH_INPUT theme=%u real-AP/STA-touch/no-premature-callback/STA-short-reject/open-STA/AP-short-reject/exact-apply-flags PASS\n",theme);
}


static void key_position(const char *text){
    nui_obj_t *k=keyboard(nui_screen_active());assert(k);nui_area_t areas[256];const char *texts[256];size_t n=nui_native_keyboard_get_keys(k,areas,texts,256);assert(n<=256);
    for(unsigned i=0;i<n;i++)if(!strcmp(texts[i],text)){point=(nui_point_t){(areas[i].x1+areas[i].x2)/2,(areas[i].y1+areas[i].y2)/2};return;}
    fprintf(stderr,"timing key absent: %s\n",text);abort();
}
static void input_step(unsigned ms){nui_native_tick(ms);nui_native_touch(point.x,point.y,pressed);render_now();}
static void key_press_only(const char *text){key_position(text);pressed=true;input_step(1);}
static void key_release_only(void){pressed=false;input_step(1);}
static void key_hold_ms(unsigned ms){while(ms){unsigned step=ms>20?20:ms;input_step(step);ms-=step;}}
static unsigned utf8_valid_count(const char *s){
    unsigned count=0;for(const unsigned char *p=(const unsigned char*)s;*p;count++){
        unsigned c=*p++,n=0;if(c<0x80)continue;assert(c>=0xc2&&c<=0xf4);n=c<0xe0?1:c<0xf0?2:3;
        while(n--){assert((*p&0xc0)==0x80);p++;}
    }return count;
}
static unsigned raw_ready,raw_cancel;
static void raw_keyboard_event(nui_event_t *e){if(nui_event_get_code(e)==NUI_EVENT_READY)raw_ready++;else if(nui_event_get_code(e)==NUI_EVENT_CANCEL)raw_cancel++;}
static void keyboard_timing_regression(unsigned theme){
    tab5_ui_init(action,NULL);tab5_ui_state_t s={.light_theme=theme,.now_known=true,.now_us=970000000};tab5_measure_config_default(&s.measurement.config);
    s.network.ap_enabled=true;strcpy(s.network.ap_ssid,"TimingAP");strcpy(s.network.ap_password,"valid-ap-123");fresh(&s);nav(4);editor_open(0);if(find_label(nui_screen_active(),"РУС → EN"))click("РУС → EN");
    nui_obj_t *ta=textarea(0);nui_textarea_set_text(ta,"");
    key_press_only("a");expect_editor("a");key_hold_ms(300);expect_editor("a");key_release_only();expect_editor("a");
    key_press_only("1#");assert(!strcmp(nui_buttonmatrix_get_button_text(keyboard(nui_screen_active()),1),"q"));key_hold_ms(900);assert(!strcmp(nui_buttonmatrix_get_button_text(keyboard(nui_screen_active()),1),"q"));key_release_only();assert(!strcmp(nui_buttonmatrix_get_button_text(keyboard(nui_screen_active()),0),"1"));key_click("abc");
    const char *cyrillic="абвгдежзийкл";nui_textarea_set_text(ta,cyrillic);unsigned original=utf8_valid_count(cyrillic);
    key_press_only(NUI_SYMBOL_BACKSPACE);assert(utf8_valid_count(nui_textarea_get_text(ta))==original-1);key_hold_ms(300);assert(utf8_valid_count(nui_textarea_get_text(ta))==original-1);key_hold_ms(600);unsigned repeated=utf8_valid_count(nui_textarea_get_text(ta));assert(repeated<original-1&&repeated>0);key_release_only();assert(utf8_valid_count(nui_textarea_get_text(ta))==repeated);
    nui_textarea_set_text(ta,cyrillic);key_press_only(NUI_SYMBOL_LEFT);key_hold_ms(900);key_release_only();key_click("x");const char *with_x=nui_textarea_get_text(ta),*x=strchr(with_x,'x');assert(x&&x-with_x<=20&&utf8_valid_count(with_x)==original+1);
    key_press_only(NUI_SYMBOL_RIGHT);key_hold_ms(900);key_release_only();key_click("y");const char *with_y=nui_textarea_get_text(ta);assert(strchr(with_y,'y')>strchr(with_y,'x')&&utf8_valid_count(with_y)==original+2);
    click("Отмена");tab5_ui_destroy();
    /* A standalone real keyboard retains its event owner after READY/CANCEL,
     * allowing exact one-event assertions even under a long NO_REPEAT hold. */
    ta=nui_textarea_create(nui_screen_active());nui_obj_set_pos(ta,20,20);nui_obj_set_size(ta,400,60);nui_textarea_set_one_line(ta,true);
    nui_obj_t *k=nui_keyboard_create(nui_screen_active());nui_obj_set_pos(k,0,360);nui_obj_set_size(k,1280,360);nui_keyboard_set_textarea(k,ta);nui_obj_add_event_cb(k,raw_keyboard_event,NUI_EVENT_READY,NULL);nui_obj_add_event_cb(k,raw_keyboard_event,NUI_EVENT_CANCEL,NULL);pump();
    raw_ready=raw_cancel=0;key_press_only(NUI_SYMBOL_OK);key_hold_ms(1000);assert(raw_ready==0);key_release_only();assert(raw_ready==1&&raw_cancel==0);
    key_press_only(NUI_SYMBOL_KEYBOARD);key_hold_ms(1000);assert(raw_cancel==0);key_release_only();assert(raw_cancel==1&&raw_ready==1);
    nui_obj_delete(k);nui_obj_delete(ta);printf("KEYBOARD_TIMING theme=%u press-only-letter/no-repeat-release-modifier/UTF8-held-backspace/repeated-arrows/one-ready-one-cancel PASS\n",theme);
}
#ifdef NUI_TEST_HAS_TEXTAREA_BOUNDS
static void caret_band(nui_obj_t *ta,uint16_t out[7*64]){
    nui_area_t a;assert(nui_native_textarea_get_text_bounds(ta,&a));const nui_font_t *f=nui_obj_get_style_text_font(ta,0);nui_point_t text;
    nui_text_get_size(&text,nui_textarea_get_text(ta),f,0,0,NUI_COORD_MAX,NUI_TEXT_FLAG_NONE);int x=a.x1+text.x-3,y=a.y1;assert(x>=0&&x+7<=1280&&y>=0&&y+64<=720);
    for(unsigned row=0;row<64;row++)memcpy(out+row*7,framebuffer+(size_t)(y+row)*1280+x,14);
}
static void caret_epoch_regression(unsigned theme){
    tab5_ui_init(action,NULL);tab5_ui_state_t s={.light_theme=theme,.now_known=true,.now_us=980000000};tab5_measure_config_default(&s.measurement.config);s.network.ap_enabled=true;strcpy(s.network.ap_ssid,"CaretAP");strcpy(s.network.ap_password,"valid-ap-123");fresh(&s);nav(4);editor_open(0);
    nui_obj_t *ta=textarea(0);nui_native_tick(137);render_now();click_obj(ta);
    for(unsigned edit=0;edit<2;edit++){
        nui_native_tick(73);nui_textarea_set_text(ta,edit?"abc":"ab");render_now();uint16_t start[7*64],before[7*64],hidden[7*64],visible[7*64];caret_band(ta,start);
        nui_native_tick(399);render_now();caret_band(ta,before);assert(!memcmp(start,before,sizeof(start)));
        nui_native_tick(2);render_now();caret_band(ta,hidden);assert(memcmp(start,hidden,sizeof(start)));
        nui_native_tick(400);render_now();caret_band(ta,visible);assert(!memcmp(start,visible,sizeof(start)));
    }
    click("Отмена");tab5_ui_destroy();printf("CARET_EPOCH theme=%u real-framebuffer/edit-reset/visible-399/hidden-401/visible-801 PASS\n",theme);
}
#endif


/* Optimization probes use the unmodified production RGB565 renderer. Golden
 * full-suite frames stay separate; cold rebuilds detect stale warm cache data. */
static uint16_t opt_before[1280*720],opt_warm[1280*720],opt_original[1280*720];
static unsigned opt_clicks;
static void opt_click(nui_event_t *e){(void)e;opt_clicks++;}
static void opt_init(void){nui_native_deinit();assert(nui_native_init(1280,720,framebuffer));nui_native_set_flush_cb(flush,NULL);pressed=false;point=(nui_point_t){0,0};}
static nui_obj_t *opt_box(nui_obj_t *parent,int x,int y,int w,int h,uint32_t color){
    nui_obj_t *o=nui_obj_create(parent);assert(o);nui_obj_remove_style_all(o);nui_obj_set_pos(o,x,y);nui_obj_set_size(o,w,h);nui_obj_set_style_bg_color(o,nui_color_hex(color),0);nui_obj_set_style_bg_opa(o,255,0);nui_obj_set_style_opa(o,255,0);return o;
}
static nui_obj_t *opt_button(bool visual_pressed){
    nui_obj_t *o=nui_button_create(nui_screen_active());assert(o);nui_obj_remove_style_all(o);nui_obj_set_pos(o,100,100);nui_obj_set_size(o,80,60);nui_obj_set_style_bg_opa(o,255,0);nui_obj_set_style_opa(o,255,0);nui_obj_set_style_bg_color(o,nui_color_hex(0x2864a0),0);
    if(visual_pressed)nui_obj_set_style_bg_color(o,nui_color_hex(0xf08020),NUI_STATE_PRESSED);
    nui_obj_add_event_cb(o,opt_click,NUI_EVENT_CLICKED,NULL);return o;
}
static size_t opt_dirty_area(const nui_area_t *bound){
    nui_area_t areas[512];size_t n=nui_native_get_dirty(areas,512),sum=0;assert(n<=512);
    for(size_t i=0;i<n;i++){if(bound)assert(contains(*bound,areas[i]));sum+=(size_t)nui_area_get_width(&areas[i])*nui_area_get_height(&areas[i]);}return sum;
}
static void opt_changed_covered(void){
    nui_area_t areas[512];size_t n=nui_native_get_dirty(areas,512);assert(n<=512);unsigned changed=0;
    for(int y=0;y<720;y++)for(int x=0;x<1280;x++)if(opt_before[y*1280+x]!=framebuffer[y*1280+x]){
        bool covered=false;for(size_t i=0;i<n;i++)if(x>=areas[i].x1&&x<=areas[i].x2&&y>=areas[i].y1&&y<=areas[i].y2){covered=true;break;}assert(covered);changed++;
    }assert(changed>0);
}
static void optimization_press(void){
    opt_init();nui_obj_t *button=opt_button(false);render_now();memcpy(opt_before,framebuffer,sizeof(opt_before));opt_clicks=0;nui_native_profile_reset();
    point=(nui_point_t){140,130};pressed=true;input_step(1);assert(!memcmp(opt_before,framebuffer,sizeof(opt_before)));assert(opt_dirty_area(NULL)==0);
    nui_native_stats_t stats;nui_native_profile_snapshot(&stats);assert(stats.frames==0&&stats.dirty_pixels==0);
    pressed=false;input_step(1);assert(opt_clicks==1&&!memcmp(opt_before,framebuffer,sizeof(opt_before)));assert(opt_dirty_area(NULL)==0);
    /* A real pressed selector must still render and release must restore it. */
    nui_obj_set_style_bg_color(button,nui_color_hex(0xf08020),NUI_STATE_PRESSED);render_now();memcpy(opt_before,framebuffer,sizeof(opt_before));
    pressed=true;input_step(1);assert(memcmp(opt_before,framebuffer,sizeof(opt_before)));nui_area_t bound={100,100,179,159};assert(opt_dirty_area(&bound)<=80*60);
    pressed=false;input_step(1);assert(opt_clicks==2&&!memcmp(opt_before,framebuffer,sizeof(opt_before)));
    nui_native_deinit();printf("OPT_PRESS PASS unchanged-style=zero-render/zero-dirty clicked-callback=preserved actual-pressed-style=redrawn/restored\n");
}
static void optimization_dirty(void){
    opt_init();nui_obj_t *o=opt_button(false);render_now();memcpy(opt_before,framebuffer,sizeof(opt_before));
    nui_obj_set_style_bg_color(o,nui_color_hex(0x40b070),0);render_now();nui_area_t local={100,100,179,159};assert(opt_dirty_area(&local)<=80*60);opt_changed_covered();
    memcpy(opt_before,framebuffer,sizeof(opt_before));nui_obj_set_pos(o,130,115);render_now();nui_area_t moved={100,100,209,174};assert(opt_dirty_area(&moved)<=110*75);opt_changed_covered();memcpy(opt_warm,framebuffer,sizeof(opt_warm));
    nui_native_invalidate(&(nui_area_t){0,0,1279,719});render_now();assert(!memcmp(opt_warm,framebuffer,sizeof(opt_warm)));
    memcpy(opt_before,framebuffer,sizeof(opt_before));nui_obj_delete(o);render_now();opt_changed_covered();
    o=opt_button(false);nui_obj_set_pos(o,130,115);nui_obj_set_style_bg_color(o,nui_color_hex(0x40b070),0);render_now();assert(!memcmp(opt_warm,framebuffer,sizeof(opt_warm)));
    nui_native_deinit();printf("OPT_DIRTY PASS tight-style/move/delete/recreate covered-all-changed-pixels partial-vs-full=exact\n");
}
typedef struct {int x,y,w,h,radius,shadow,offset,shadow_opa,parent_opa;bool light,modal;} opt_scene_state;
static const opt_scene_state opt_states[]={
    {180,120,260,180,16,28,10,80,255,false,false},
    {215,155,260,180,16,28,10,80,255,false,false},
    {180,120,301,197,16,28,10,80,255,false,false},
    {180,120,260,180,16,28,10,80,255,true,false},
    {180,120,260,180,16,46,-7,144,255,false,false},
    {180,120,260,180,16,28,10,80,128,false,false},
    {180,120,260,180,16,28,10,80,255,false,true},
    {-18,7,260,180,16,28,10,80,255,true,false},
    {180,120,401,137,33,9,3,160,255,true,false}
};
typedef struct {nui_obj_t *parent,*panel,*swatch,*veil,*modal;} opt_scene;
static void opt_apply(opt_scene *s,const opt_scene_state *v){
    nui_obj_set_style_bg_color(nui_screen_active(),nui_color_hex(v->light?0xf1e9dd:0x152033),0);
    nui_obj_set_style_opa(s->parent,v->parent_opa,0);nui_obj_set_pos(s->panel,v->x,v->y);nui_obj_set_size(s->panel,v->w,v->h);
    nui_obj_set_style_bg_color(s->panel,nui_color_hex(v->light?0xffffff:0x294064),0);nui_obj_set_style_radius(s->panel,v->radius,0);nui_obj_set_style_border_width(s->panel,2,0);nui_obj_set_style_border_color(s->panel,nui_color_hex(v->light?0x4080c0:0x80b0d0),0);
    nui_obj_set_style_shadow_width(s->panel,v->shadow,0);nui_obj_set_style_shadow_offset_y(s->panel,v->offset,0);nui_obj_set_style_shadow_opa(s->panel,v->shadow_opa,0);nui_obj_set_style_shadow_color(s->panel,nui_color_hex(v->light?0x102030:0x080b10),0);
    nui_obj_set_style_bg_color(s->swatch,nui_color_hex(v->light?0xc04020:0x50c080),0);
    if(v->modal)nui_obj_remove_flag(s->veil,NUI_OBJ_FLAG_HIDDEN);else nui_obj_add_flag(s->veil,NUI_OBJ_FLAG_HIDDEN);
}
static opt_scene opt_build(unsigned state){
    opt_init();nui_obj_t *root=nui_screen_active();opt_box(root,140,80,420,300,0x805020);opt_box(root,220,180,300,180,0x205870);
    opt_scene s={0};s.parent=opt_box(root,0,0,1280,720,0);nui_obj_set_style_bg_opa(s.parent,0,0);
    s.panel=opt_box(s.parent,180,120,260,180,0x294064);s.swatch=opt_box(s.panel,20,30,47,41,0x50c080);
    s.veil=opt_box(root,0,0,1280,720,0);nui_obj_set_style_bg_opa(s.veil,90,0);s.modal=opt_box(s.veil,300,200,360,220,0xc8d8e8);nui_obj_set_style_radius(s.modal,19,0);nui_obj_set_style_shadow_width(s.modal,36,0);nui_obj_set_style_shadow_opa(s.modal,100,0);nui_obj_set_style_shadow_offset_y(s.modal,8,0);
    opt_apply(&s,&opt_states[state]);return s;
}
static void optimization_cache_state(void){
    for(unsigned state=1;state<sizeof(opt_states)/sizeof(opt_states[0]);state++){
        opt_scene s=opt_build(0);render_now();memcpy(opt_original,framebuffer,sizeof(opt_original));
        nui_native_invalidate(&(nui_area_t){100,50,699,499});render_now();assert(!memcmp(opt_original,framebuffer,sizeof(opt_original)));
        memcpy(opt_before,framebuffer,sizeof(opt_before));opt_apply(&s,&opt_states[state]);render_now();opt_changed_covered();memcpy(opt_warm,framebuffer,sizeof(opt_warm));
        opt_apply(&s,&opt_states[0]);render_now();assert(!memcmp(opt_original,framebuffer,sizeof(opt_original)));
        opt_apply(&s,&opt_states[state]);render_now();assert(!memcmp(opt_warm,framebuffer,sizeof(opt_warm)));
        opt_build(state);render_now();assert(!memcmp(opt_warm,framebuffer,sizeof(opt_warm)));nui_native_deinit();
        printf("OPT_CACHE_STATE state=%u PASS warm/cold/roundtrip=exact dirty-coverage=complete\n",state);
    }
}
static void optimization_lifetime(void){
    nui_native_deinit();nui_native_stats_t initial;nui_native_profile_snapshot(&initial);assert(initial.allocation_bytes==0&&initial.allocation_count==0);
    uint64_t failures=initial.allocation_failures;
    for(unsigned cycle=0;cycle<64;cycle++){
        opt_scene s=opt_build(cycle%(sizeof(opt_states)/sizeof(opt_states[0])));render_now();
        nui_obj_delete(s.panel);render_now();s.panel=opt_box(s.parent,110,100,280,190,0x294064);s.swatch=opt_box(s.panel,20,30,47,41,0x50c080);opt_apply(&s,&opt_states[0]);render_now();
        nui_native_deinit();nui_native_stats_t after;nui_native_profile_snapshot(&after);assert(after.allocation_bytes==0&&after.allocation_count==0&&after.allocation_failures==failures);
    }printf("OPT_LIFETIME PASS 64-init/render/delete/recreate/deinit cycles live-bytes=0 live-allocations=0 failures=0\n");
}

#ifdef NUI_TEST_HAS_OPT_CACHE_STATS
static size_t opt_object_count(nui_obj_t *o){size_t n=1;for(unsigned i=0;i<nui_obj_get_child_count(o);i++)n+=opt_object_count(nui_obj_get_child(o,i));return n;}
static void opt_cache_bounds(nui_native_stats_t *out){
    nui_native_profile_snapshot(out);assert(out->shadow_cache_capacity_bytes>out->shadow_cache_static_bytes);
    assert(out->shadow_cache_bytes>=out->shadow_cache_static_bytes&&out->shadow_cache_bytes<=out->shadow_cache_capacity_bytes);
    assert(out->shadow_cache_peak_bytes<=out->shadow_cache_capacity_bytes);
    assert(out->object_cache_bytes_per_object>0&&out->object_cache_bytes==opt_object_count(nui_screen_active())*out->object_cache_bytes_per_object);
}
static void optimization_cache_budget(void){
    opt_scene s=opt_build(0);render_now();memcpy(opt_original,framebuffer,sizeof(opt_original));nui_native_stats_t warm,now;opt_cache_bounds(&warm);
    assert(warm.shadow_cache_bytes>warm.shadow_cache_static_bytes);
    for(unsigned i=0;i<8;i++){nui_native_invalidate(&(nui_area_t){100,50,699,499});render_now();assert(!memcmp(opt_original,framebuffer,sizeof(opt_original)));opt_cache_bounds(&now);}
    assert(now.shadow_cache_hits>warm.shadow_cache_hits&&now.shadow_cache_misses==warm.shadow_cache_misses);
    assert(now.allocation_bytes==warm.allocation_bytes&&now.allocation_count==warm.allocation_count);
    uint64_t evictions=now.shadow_cache_evictions;
    for(unsigned i=0;i<40;i++){
        nui_obj_set_style_radius(s.panel,2+(int)(i%36),0);nui_obj_set_style_shadow_width(s.panel,4+(int)(i%8)*4,0);nui_obj_set_style_shadow_offset_y(s.panel,(int)(i%11)-5,0);nui_obj_set_style_shadow_opa(s.panel,40+(int)i*3,0);render_now();opt_cache_bounds(&now);
    }
    assert(now.shadow_cache_evictions>evictions);opt_apply(&s,&opt_states[0]);render_now();assert(!memcmp(opt_original,framebuffer,sizeof(opt_original)));
    /* An oversized shadow must not allocate beyond the cache budget. */
    nui_obj_set_style_shadow_width(s.panel,128,0);nui_obj_set_style_radius(s.panel,90,0);nui_obj_set_style_shadow_offset_y(s.panel,40,0);render_now();opt_cache_bounds(&now);
    opt_apply(&s,&opt_states[0]);render_now();assert(!memcmp(opt_original,framebuffer,sizeof(opt_original)));
    size_t live_object_bytes=now.object_cache_bytes;nui_obj_delete(s.panel);render_now();opt_cache_bounds(&now);assert(now.object_cache_bytes<live_object_bytes);
    nui_native_deinit();nui_native_profile_snapshot(&now);assert(now.shadow_cache_bytes==now.shadow_cache_static_bytes&&now.object_cache_bytes==0&&now.allocation_bytes==0&&now.allocation_count==0);
    printf("OPT_CACHE_BUDGET PASS warm-hits/no-rebuild/no-allocation pressure-eviction oversized-fallback exact-restoration object-cache-bounded teardown-static-only capacity=%llu\n",(unsigned long long)now.shadow_cache_capacity_bytes);
}
#else
static void optimization_cache_budget(void){fprintf(stderr,"OPT_CACHE_BUDGET unavailable: public live/capacity/hit/miss/eviction/object-byte stats required\n");assert(!"Missing required optimization cache evidence");}
#endif
static void optimization_probes(const char *group){
    if(!strcmp(group,"press")||!strcmp(group,"all"))optimization_press();
    if(!strcmp(group,"dirty")||!strcmp(group,"all"))optimization_dirty();
    if(!strcmp(group,"cache-state")||!strcmp(group,"all"))optimization_cache_state();
    if(!strcmp(group,"lifetime")||!strcmp(group,"all"))optimization_lifetime();
    if(!strcmp(group,"cache-budget")||!strcmp(group,"all"))optimization_cache_budget();
}

int main(int argc,char **argv){
    assert(argc==2);output=argv[1];setbuf(stdout,NULL);setbuf(stderr,NULL);char path[1024];snprintf(path,sizeof(path),"%s/geometry.txt",output);report=fopen(path,"w");assert(report);setbuf(report,NULL);
    snprintf(path,sizeof(path),"%s/geometry_points.tsv",output);points=fopen(path,"w");assert(points);
    snprintf(path,sizeof(path),"%s/textarea_points.tsv",output);textarea_points=fopen(path,"w");assert(textarea_points);
    snprintf(path,sizeof(path),"%s/input_contracts.jsonl",output);contracts=fopen(path,"w");assert(contracts);
    rgb565_mode=true; /* Exercise the production RGB565 renderer unconditionally. */
    printf("HOST_MODE fine_counters=%d release_capture=%d\n",NUI_PROFILE_FINE_COUNTERS,1-NUI_PROFILE_FINE_COUNTERS);
    printf("NATIVE_DISPLAY format=%s NUI_COLOR_DEPTH=%d framebuffer=1280x720\n",rgb565_mode?"RGB565":"XRGB8888",16);
    assert(nui_native_init(1280,720,framebuffer));nui_native_set_flush_cb(flush,NULL);
    const char *opt_group=getenv("TAB5_OPT_GROUP");
    if(opt_group&&*opt_group){optimization_probes(opt_group);fclose(contracts);fclose(textarea_points);fclose(points);fclose(report);nui_native_deinit();return 0;}
    if(getenv("TAB5_TEST_BENCH_ONLY")&&!strcmp(getenv("TAB5_TEST_BENCH_ONLY"),"1")){
        for(unsigned theme=0;theme<2;theme++)graph_motion_benchmark(theme);
        for(unsigned theme=0;theme<2;theme++)measure_motion_benchmark(theme);
        fclose(contracts);fclose(textarea_points);fclose(points);fclose(report);nui_native_deinit();return 0;
    }
    for(unsigned theme=0;theme<2;theme++){
        tab5_ui_init(action,NULL);tab5_ui_state_t s={0};tab5_measure_config_default(&s.measurement.config);s.measurement.config.frequency_hz=20;s.measurement.config.gap_ms=20;s.measurement.config.alpha=.2;s.measurement.config.roll_calm_deg=.5;s.measurement.config.roll_rate_hz=2;s.measurement.config.roll_hysteresis_deg=.1;
        s.light_theme=theme;s.now_known=true;s.now_us=700000000;s.datetime_known=true;s.datetime=(tab5_ui_datetime_t){2026,10,22,14,32,5};s.clock=(tab5_ui_clock_t){.known=true,.time_valid=true};strcpy(s.clock.datetime,"22.10.2026 · 14:32:05");strcpy(s.panel,"ST7123 · определён автоматически");strcpy(s.firmware,"1.6.1-esp32");
        s.sd=(tab5_ui_medium_t){.known=true,.mounted=true};s.usb=(tab5_ui_medium_t){.known=true};s.tcp_clients_known=true;s.tcp_clients=1;
        s.link=(tab5_ui_link_t){.known=true,.ap_started=true,.sta_connected=true,.tcp_port=5000};strcpy(s.link.ap_ip,"192.168.4.1");strcpy(s.link.sta_ip,"10.20.4.117");
        s.network=(tab5_ui_network_t){.ap_enabled=true,.sta_enabled=true};strcpy(s.network.ap_ssid,"KRENOMER-7F3A");strcpy(s.network.sta_ssid,"SAFU-Lab");strcpy(s.network.ap_password,"test-pass-123");strcpy(s.network.sta_password,"secret-pass-123");
        for(unsigned i=0;i<2;i++)s.measurement.sensor[i]=(tab5_measure_sensor_t){.valid=true,.x=i?-.39f:-.42f,.y=i?1.41f:1.37f,.roll_x=i?.12f:.14f,.roll_y=i?.06f:.05f,.calm_x=true,.calm_y=true,.good=1,.timestamp_us=s.now_us,.roll_fill_s=20};s.measurement.stable=true;
        fresh(&s);char name[120];const char *tag=theme?"light":"dark";
        #define SHOT(stage) do{snprintf(name,sizeof(name),"%s_%s",stage,tag);capture(name);}while(0)
        resources_regression(&s,theme);SHOT("dashboard");idle_regression(&s,theme);assert(find_label(nui_screen_active(),"Ноль датчиков"));assert(find_label(nui_screen_active(),"14:32"));
        nav(1);
        for(unsigned i=0;i<250;i++){
            s.now_us+=500000;
            for(unsigned j=0;j<2;j++){float t=i*.5f;s.measurement.sensor[j].valid=!(i>205&&i<215);s.measurement.sensor[j].timestamp_us=s.now_us;s.measurement.sensor[j].good++;s.measurement.sensor[j].x=.2f+1.95f/(1+expf(-(t-62)/3))+.38f*sinf(t*.9f+j*.1f)*fminf(1,fmaxf(0,(t-55)/10));s.measurement.sensor[j].y=-.12f+.1f*sinf(t*.9f+j)*fminf(1,fmaxf(0,(t-55)/10));}
            tab5_ui_update(&s);
        }
        SHOT("chart");
        for(unsigned j=0;j<2;j++){s.measurement.sensor[j].x=j?-.39f:-.42f;s.measurement.sensor[j].y=j?1.41f:1.37f;}fresh(&s);nav(2);SHOT("recording");assert(find_label(nui_screen_active(),"История записей появится после подключения носителя"));nav(3);SHOT("settings");nav(4);SHOT("network");
        printf("Five native pages ready: %s\n",tag);
        nav(3);unsigned hold_before=callbacks;point=(nui_point_t){635,200};pressed=true;for(unsigned hold=0;hold<5;hold++)pump();pressed=false;pump();assert(callbacks==hold_before);tap(1140,52);assert(last.kind==TAB5_UI_SETTINGS&&last.config.frequency_hz>21);s.measurement.config=last.config;fresh(&s);SHOT("settings_longpress");
        click(theme?"Тёмная":"Светлая");assert(last.kind==TAB5_UI_SETTINGS&&last.light_theme!=s.light_theme);s.light_theme=last.light_theme;fresh(&s);click(theme?"Светлая":"Тёмная");s.light_theme=last.light_theme;fresh(&s);
        nav(0);unsigned before=callbacks;tap(1120,80);assert(callbacks==before+1&&last.kind==TAB5_UI_START);s.recording=true;s.rec_known=true;strcpy(s.rec_file,"2026-10-22_M001");s.rec_started_us=s.now_us-192000000;s.rec_duration_known=true;s.rec_duration_s=192;s.sd.recording=s.usb.recording=true;s.usb.mounted=true;s.sd.written=s.usb.written=3812;
        s.measurement.stable=false;s.measurement.sensor[0].x=2.18f;s.measurement.sensor[0].y=-.07f;s.measurement.sensor[0].roll_x=.86f;s.measurement.sensor[0].roll_y=.21f;s.measurement.sensor[0].calm_x=false;s.measurement.sensor[1].valid=false;s.measurement.sensor[1].fail_streak=3;s.measurement.sensor[1].errors=27;s.measurement.sensor[1].last_error=TAB5_MEASURE_TIMEOUT;fresh(&s);SHOT("dashboard_recording_error");
        before=callbacks;tap(220,655);assert(callbacks==before);SHOT("zero_confirm");click("Подтвердить");assert(last.kind==TAB5_UI_ZERO);fresh(&s);assert(find_label(nui_screen_active(),"не задан"));s.measurement.sensor[0].zero_x=2.18f;fresh(&s);assert(find_label(nui_screen_active(),"задан 14:32"));
        before=callbacks;tap(390,655);assert(callbacks==before);click("Отмена");assert(callbacks==before);tap(390,655);click("Подтвердить");assert(last.kind==TAB5_UI_ZERO_RESET);
        tap(1120,80);assert(last.kind==TAB5_UI_STOP);s.recording=false;s.sd.recording=s.usb.recording=false;fresh(&s);
        nav(3);tap(635,200);SHOT("settings_dirty");tap(535,200);click("C");click("7");click("5");SHOT("numeric");before=callbacks;click("Готово");assert(callbacks==before);click("C");click("2");click("0");SHOT("numeric_valid");click("Готово");
        /* Change another setting, reject navigation, then discard explicitly. */
        tap(635,276);nav(4);SHOT("dirty_navigation");click("Остаться");nav(4);click("Отбросить");
        nav(3);tap(635,200);tap(1140,52);assert(last.kind==TAB5_UI_SETTINGS);tab5_measure_config_t sent=last.config;fresh(&s);SHOT("settings_pending");s.measurement.config=sent;fresh(&s);SHOT("settings_ack");
        /* Invalid Gregorian dates do not produce a TIME action. */
        click("Изменить");nui_obj_t *date[6];for(unsigned i=0;i<6;i++){date[i]=textarea(i);assert(date[i]);}
        nui_textarea_set_text(date[0],"2025");nui_textarea_set_text(date[1],"2");nui_textarea_set_text(date[2],"29");before=callbacks;SHOT("time_invalid");click("Готово");assert(callbacks==before);
        nui_textarea_set_text(date[2],"31");click("Готово");assert(callbacks==before);nui_textarea_set_text(date[2],"29");nui_textarea_set_text(date[0],"2024");nui_textarea_set_text(date[3],"24");click("Готово");assert(callbacks==before);nui_textarea_set_text(date[3],"23");SHOT("time_valid");click("Готово");assert(last.kind==TAB5_UI_TIME&&last.datetime.year==2024&&last.datetime.day==29);
        nav(4);tap(400,204);if(find_label(nui_screen_active(),"РУС → EN"))click("РУС → EN");key_click("q");SHOT("network_keyboard_en");click("EN → РУС");key_click("я");SHOT("network_keyboard_ru");click("Готово");tap(1150,52);assert(last.kind==TAB5_UI_NETWORK);tab5_ui_network_t net_sent=last.network;fresh(&s);SHOT("network_pending");s.network=net_sent;fresh(&s);SHOT("network_ack");tap(400,272);nui_textarea_set_text(textarea(0),"short");click("Готово");unsigned rejected=callbacks;tap(1150,52);assert(callbacks==rejected);SHOT("network_invalid");tap(400,272);nui_textarea_set_text(textarea(0),s.network.ap_password);click("Готово");click("Показать пароли");SHOT("network_revealed");click("Скрыть пароли");
        nav(0);s.measurement.sensor[1].valid=true;for(unsigned i=0;i<2;i++){s.measurement.sensor[i].x=i?89.99f:-89.99f;s.measurement.sensor[i].y=i?-89.99f:89.99f;s.measurement.sensor[i].good++;}strcpy(s.link.ap_ip,"255.255.255.255");s.sd.written=s.usb.written=999999;s.sd.recording=true;s.measurement.sensor[0].roll_x=1.6;s.measurement.sensor[0].roll_y=.25;fresh(&s);SHOT("dashboard_extremes");assert(find_label(nui_screen_active(),"−89.99°"));assert(find_label(nui_screen_active(),"+89.99°"));assert(find_label(nui_screen_active(),"−179.98°"));assert(find_label(nui_screen_active(),"+179.98°"));
        nav(4);tap(400,204);char ssid[33];memset(ssid,'W',32);ssid[32]=0;nui_textarea_set_text(textarea(0),ssid);SHOT("network_keyboard_long");click("Готово");SHOT("network_long");nav(1);click("Отбросить");
        /* Only injected timestamped measurement samples populate chart. */
        for(unsigned i=0;i<1250;i++){s.now_us+=500000;for(unsigned j=0;j<2;j++){s.measurement.sensor[j].valid=!(i>1190&&i<1200);s.measurement.sensor[j].timestamp_us=s.now_us;s.measurement.sensor[j].good++;s.measurement.sensor[j].x=.2f+2.f*(float)i/1250+.35f*sinf(i*.5f+j*.1f);s.measurement.sensor[j].y=-.12f+.1f*sinf(i*.5f+j);}tab5_ui_update(&s);}
        SHOT("chart_samples_fixture");for(unsigned i=0;i<350;i++){s.now_us+=100000;for(unsigned j=0;j<2;j++){s.measurement.sensor[j].timestamp_us=s.now_us;s.measurement.sensor[j].x=2+.3f*sinf(i*.1f+j*.1f);s.measurement.sensor[j].y=.1f*sinf(i*.1f+j);}tab5_ui_update(&s);}click("30 с");SHOT("chart_30");click("10 мин");SHOT("chart_600");tap(668,52);assert(find_label(nui_screen_active(),"скрыт"));tap(800,300);SHOT("chart_touch_hidden");
        nav(0);s.measurement.sensor[0].valid=s.measurement.sensor[1].valid=false;fresh(&s);SHOT("dashboard_disconnected");
        s.action_result_known=true;s.action_result_serial=1;s.action_result_kind=TAB5_UI_START;s.action_result_success=false;
        strcpy(s.status,"Запись недоступна: ESP_FAIL");s.recording=false;fresh(&s);assert(find_label(nui_screen_active(),s.status));SHOT("backend_record_error");
        click("✗");fresh(&s);assert(!find_label(nui_screen_active(),s.status));
        s.action_result_serial++;fresh(&s);assert(find_label(nui_screen_active(),s.status));SHOT("backend_repeated_error");
        s.action_result_serial++;s.action_result_kind=TAB5_UI_TIME;strcpy(s.status,"Ошибка установки RTC: ESP_FAIL");fresh(&s);assert(find_label(nui_screen_active(),s.status));SHOT("backend_time_error");
        click("✗");nav(4);tap(400,204);nui_textarea_set_text(textarea(0),"BackendRetry");click("Готово");tap(1150,52);assert(last.kind==TAB5_UI_NETWORK);tab5_ui_network_t first_attempt=last.network;
        /* Configure failed: runtime state is unchanged, draft must remain retryable. */
        s.action_result_serial++;s.action_result_kind=TAB5_UI_NETWORK;s.action_result_success=false;
        strcpy(s.status,"Ошибка сохранения сети: ESP_FAIL");fresh(&s);assert(find_label(nui_screen_active(),s.status));nui_obj_t *net_apply=nui_obj_get_parent(find_label(nui_screen_active(),"Применить"));assert(!nui_obj_has_state(net_apply,NUI_STATE_DISABLED));SHOT("backend_network_error");
        unsigned retry_before=callbacks;tap(1150,52);assert(callbacks==retry_before+1&&!strcmp(last.network.ap_ssid,first_attempt.ap_ssid));
        /* Runtime configured, but NVS failed: equal state must NOT erase failure. */
        s.network=last.network;s.action_result_serial++;s.action_result_success=false;strcpy(s.status,"Сеть применена, ошибка NVS: ESP_FAIL");fresh(&s);assert(!nui_obj_has_state(net_apply,NUI_STATE_DISABLED));SHOT("backend_network_nvs_error");
        fresh(&s);assert(!nui_obj_has_state(net_apply,NUI_STATE_DISABLED));
        retry_before=callbacks;tap(1150,52);assert(callbacks==retry_before+1);fresh(&s);assert(nui_obj_has_state(net_apply,NUI_STATE_DISABLED));
        s.action_result_serial++;s.action_result_success=true;strcpy(s.status,"Настройки сети сохранены");fresh(&s);assert(nui_obj_has_state(net_apply,NUI_STATE_DISABLED));SHOT("backend_network_retry_success");
        click("✗");nav(3);tap(635,200);tap(1140,52);assert(last.kind==TAB5_UI_SETTINGS);s.measurement.config=last.config;
        s.action_result_serial++;s.action_result_kind=TAB5_UI_SETTINGS;s.action_result_success=false;strcpy(s.status,"Ошибка сохранения настроек NVS: ESP_FAIL");fresh(&s);assert(find_label(nui_screen_active(),s.status));nui_obj_t *set_apply=nui_obj_get_parent(find_label(nui_screen_active(),"Применить"));assert(!nui_obj_has_state(set_apply,NUI_STATE_DISABLED));SHOT("backend_nvs_error");
        retry_before=callbacks;tap(1140,52);assert(callbacks==retry_before+1);fresh(&s);assert(nui_obj_has_state(set_apply,NUI_STATE_DISABLED));
        s.action_result_serial++;s.action_result_success=true;strcpy(s.status,"Настройки сохранены");fresh(&s);assert(nui_obj_has_state(set_apply,NUI_STATE_DISABLED));SHOT("backend_settings_retry_success");
        tab5_ui_destroy();
        #undef SHOT
    }
    for(unsigned theme=0;theme<2;theme++)graph_idle_regression(theme);
    for(unsigned theme=0;theme<2;theme++)graph_5hz_regression(theme);
    for(unsigned theme=0;theme<2;theme++)live_unfiltered_regression(theme);
    for(unsigned theme=0;theme<2;theme++)graph_motion_benchmark(theme);
    for(unsigned theme=0;theme<2;theme++)measure_motion_benchmark(theme);
    for(unsigned theme=0;theme<2;theme++)level_overlay_regression(theme);
    for(unsigned theme=0;theme<2;theme++)keyboard_interaction_regression(theme);
    for(unsigned theme=0;theme<2;theme++)network_switch_regression(theme);
    for(unsigned theme=0;theme<2;theme++)keyboard_timing_regression(theme);
#ifdef NUI_TEST_HAS_TEXTAREA_BOUNDS
    for(unsigned theme=0;theme<2;theme++)caret_epoch_regression(theme);
#endif
    /* UI reinitialization must reuse its property, not exhaust LVGL registry. */
    unsigned props=nui_style_get_num_custom_props();
    for(unsigned i=0;i<32;i++){tab5_ui_init(action,NULL);tab5_ui_destroy();assert(nui_style_get_num_custom_props()==props);}
    printf("role property lifecycle: 32 reinitializations, no registry growth\n");
    fprintf(report,"RESULT cases=%u callbacks=%u layout_guardrail_failures=%u\n",cases,callbacks,failures);fclose(contracts);fclose(textarea_points);fclose(points);fclose(report);printf("headless native UI: %u cases, %u layout guardrail failures; exact fidelity pending comparator\n",cases,failures);nui_native_deinit();return failures?1:0;
}
