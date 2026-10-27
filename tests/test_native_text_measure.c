/* Actual production measurement, UI and fonts. The runner's second build only
 * disables the measurement cache, forcing the original measuring path. */
#include <stdlib.h>
#include <stdbool.h>
static bool fail_alloc;
static void *test_calloc(size_t count,size_t size){return fail_alloc?NULL:calloc(count,size);}
#define calloc test_calloc
#ifndef TEXT_MEASURE_RENDERER
#define TEXT_MEASURE_RENDERER "../tab5/main/native/native_ui.c"
#endif
#include TEXT_MEASURE_RENDERER
#undef calloc
#include "tab5_ui.h"
#include "tab5_native_port.h"
#ifndef TEST_MEASURE_FORCED
#define TEST_MEASURE_FORCED 0
#endif
static uint16_t fb[1280*720],proof[1280*720];
static FILE *raw,*csv;
static unsigned glyph_calls,unit_checks,callbacks,ms;
static double stamp(void){
#ifdef _WIN32
    static LARGE_INTEGER freq;LARGE_INTEGER t;if(!freq.QuadPart)QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);return (double)t.QuadPart*1e6/freq.QuadPart;
#else
    struct timespec t;timespec_get(&t,TIME_UTC);return (double)t.tv_sec*1e6+t.tv_nsec/1000.;
#endif
}
static uint64_t clock_cb(void){return (uint64_t)stamp();}
static bool counted_glyph(const nui_font_t *f,nui_font_glyph_dsc_t *d,uint32_t cp,uint32_t next){
    glyph_calls++;return nui_font_get_glyph_dsc_fmt_txt(f,d,cp,next);
}
static void check_measure(nui_obj_t *o){
    nui_point_t expected;int w,h;
    nui_text_get_size(&expected,o->text?o->text:"",font(o,0),num(o,NUI_STYLE_TEXT_LETTER_SPACE,0),
        num(o,NUI_STYLE_TEXT_LINE_SPACE,0),o->w>0?MAX(1,o->w-num(o,NUI_STYLE_PAD_LEFT,0)-num(o,NUI_STYLE_PAD_RIGHT,0)):NUI_COORD_MAX,
        (o->w==NUI_SIZE_CONTENT||o->one_line)?NUI_TEXT_FLAG_EXPAND:0);
    expected.x+=num(o,NUI_STYLE_PAD_LEFT,0)+num(o,NUI_STYLE_PAD_RIGHT,0)+2*num(o,NUI_STYLE_BORDER_WIDTH,0);
    expected.y+=num(o,NUI_STYLE_PAD_TOP,0)+num(o,NUI_STYLE_PAD_BOTTOM,0)+2*num(o,NUI_STYLE_BORDER_WIDTH,0);
    content_size(o,&w,&h);assert(w==expected.x&&h==expected.y);
    unsigned calls=glyph_calls;content_size(o,&w,&h);
    if(!TEST_MEASURE_FORCED)assert(glyph_calls==calls);
    nui_area_t a;nui_obj_get_coords(o,&a);
    assert(nui_area_get_width(&a)==(o->w==NUI_SIZE_CONTENT?w:o->w));
    assert(nui_area_get_height(&a)==(o->h==NUI_SIZE_CONTENT?h:o->h));
    if(raw){
        assert(fwrite(&a,sizeof(a),1,raw)==1);assert(fwrite(&expected,sizeof(expected),1,raw)==1);
        nui_area_t all={0,0,n.width-1,n.height-1};nui_native_invalidate(&all);nui_native_render();
        assert(fwrite(fb,(size_t)n.width*n.height*2,1,raw)==1);
    }
    unit_checks++;
}
static void unit(void){
    assert(nui_native_init(240,160,fb));
    nui_font_t a=nui_font_fallback_14,b=nui_font_fallback_14;
    a.get_glyph_dsc=b.get_glyph_dsc=counted_glyph;b.line_height+=5;
    nui_obj_t *parent=nui_obj_create(nui_screen_active()),*label=nui_label_create(parent);
    nui_obj_set_size(parent,220,150);nui_obj_set_style_text_font(label,&a,0);
    nui_label_set_text(label,"Measurement AV: UTF-8 текст\nsecond line");check_measure(label);
    assert(sizeof(label->measure_geometry)+sizeof(label->measure_size)==16);
    size_t bytes=n.stats.allocation_bytes,count=n.stats.allocation_count;
    unsigned calls=glyph_calls;
    for(int i=0;i<32;i++){int w,h;nui_obj_set_pos(parent,i%4,i%7);content_size(label,&w,&h);}
    if(!TEST_MEASURE_FORCED)assert(glyph_calls==calls);
    assert(n.stats.allocation_bytes==bytes&&n.stats.allocation_count==count);
    nui_obj_set_style_text_color(label,0x998877,0);check_measure(label);
    nui_obj_set_width(label,80);check_measure(label);
    nui_obj_set_width(label,25);check_measure(label);
    nui_obj_set_height(label,30);check_measure(label);
    nui_obj_set_height(label,NUI_SIZE_CONTENT);check_measure(label);
    nui_obj_set_style_text_font(label,&b,0);check_measure(label);
    nui_obj_set_style_text_letter_space(label,3,0);check_measure(label);
    nui_obj_set_style_text_letter_space(label,-1,0);check_measure(label);
    nui_obj_set_style_text_line_space(label,7,0);check_measure(label);
    nui_obj_set_style_pad_left(label,8,0);nui_obj_set_style_pad_right(label,5,0);check_measure(label);
    nui_obj_set_style_pad_top(label,4,0);nui_obj_set_style_pad_bottom(label,3,0);check_measure(label);
    nui_obj_set_style_border_width(label,2,0);check_measure(label);
    nui_obj_set_style_text_align(label,NUI_TEXT_ALIGN_RIGHT,0);check_measure(label);
    nui_label_set_long_mode(label,NUI_LABEL_LONG_WRAP);check_measure(label);
    nui_label_set_long_mode(label,NUI_LABEL_LONG_DOT);check_measure(label);
    nui_label_set_long_mode(label,NUI_LABEL_LONG_SCROLL_CIRCULAR);check_measure(label);
    nui_textarea_set_one_line(label,true);check_measure(label);
    nui_textarea_set_one_line(label,false);check_measure(label);
    nui_label_set_text(label,"");check_measure(label);
    nui_label_set_text(label,"more text after empty");check_measure(label);
    /* Failures retain old text/style/cache; retry must measure new inputs. */
    fail_alloc=true;nui_label_set_text(label,"RETRY");assert(!strcmp(label->text,"more text after empty"));check_measure(label);
    assert(!nui_label_create(parent));fail_alloc=false;
    nui_label_set_text(label,"RETRY");check_measure(label);
    nui_obj_remove_style_all(label);nui_obj_set_style_text_font(label,&a,0);check_measure(label);
    fail_alloc=true;nui_obj_set_style_pad_left(label,17,0);assert(num(label,NUI_STYLE_PAD_LEFT,0)==0);check_measure(label);
    fail_alloc=false;nui_obj_set_style_pad_left(label,17,0);check_measure(label);
    /* Shared, inherited and state-dependent fonts/spacing. */
    nui_style_t s,t;nui_style_init(&s);nui_style_init(&t);
    nui_style_set_text_font(&s,&a);nui_style_set_text_font(&t,&b);
    nui_obj_remove_style_all(label);nui_obj_add_style(parent,&s,0);check_measure(label);
    nui_style_set_text_letter_space(&s,4);check_measure(label);
    nui_style_set_text_font(&s,&b);check_measure(label);
    nui_obj_set_style_text_font(parent,&a,NUI_STATE_CHECKED);nui_obj_add_state(parent,NUI_STATE_CHECKED);check_measure(label);
    nui_obj_remove_state(parent,NUI_STATE_CHECKED);check_measure(label);
    nui_obj_set_style_text_font(label,&a,NUI_STATE_CHECKED);nui_obj_add_state(label,NUI_STATE_CHECKED);check_measure(label);
    nui_obj_remove_state(label,NUI_STATE_CHECKED);check_measure(label);
    nui_obj_remove_style_all(label);nui_obj_add_style(label,&s,0);check_measure(label);
    assert(nui_obj_replace_style_atomic(label,&s,&t,0));check_measure(label);
    b.line_height+=2;nui_obj_report_style_change(NULL);check_measure(label);
    nui_obj_remove_style(label,&t,0);check_measure(label);
    nui_style_reset(&s);check_measure(label);
    nui_native_deinit();nui_style_reset(&s);nui_style_reset(&t);nui_native_deinit();
    assert(!n.stats.allocation_count&&!n.stats.allocation_bytes);
}
static void action(const tab5_ui_action_t *a,void *user){(void)a;(void)user;callbacks++;}
static void touch(int x,int y,bool down){ms+=16;assert(tab5_native_port_post_touch(x,y,down,ms));tab5_native_port_input_dispatch(ms);}
static nui_obj_t *find_content(nui_obj_t *o){
    if(nui_obj_get_width(o)==1094&&nui_obj_get_height(o)==1490)return o;
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++){nui_obj_t *c=find_content(nui_obj_get_child(o,i));if(c)return c;}
    return NULL;
}
static void scroll(unsigned cycles){
    for(unsigned theme=0;theme<2;theme++){
        ms=1000;assert(nui_native_init(1280,720,fb));nui_native_set_clock_cb(clock_cb);
        tab5_ui_init(action,NULL);tab5_native_port_input_reset();
        tab5_ui_state_t s={0};tab5_measure_config_default(&s.measurement.config);
        s.light_theme=theme!=0;s.brightness_pct=60;s.now_known=true;s.now_us=1000000;
        strcpy(s.firmware,"text-measure-fixture");tab5_ui_update(&s);nui_native_render();
        touch(76,570,true);touch(76,570,false);assert(nui_native_render());
        nui_obj_t *content=find_content(nui_screen_active());assert(content);
        nui_area_t initial;nui_obj_get_coords(content,&initial);
        for(unsigned i=0;i<cycles*52;i++){
            int delta=i%52<26?37:-37;touch(600,450,true);
            double a=stamp();touch(600,450-delta,true);double b=stamp();
            size_t dirty=nui_native_render();double c=stamp();assert(dirty);
            nui_area_t ca;nui_obj_get_coords(content,&ca);int offset=initial.y1-ca.y1;assert(offset>=0&&offset<=950);
            if(csv)fprintf(csv,"%u,%u,%d,%.6f,%.6f\n",theme,i,offset,b-a,c-b);
            if(raw){
                assert(fwrite(&ca,sizeof(ca),1,raw)==1);assert(fwrite(fb,sizeof(fb),1,raw)==1);
                memcpy(proof,fb,sizeof(fb));nui_area_t full={0,0,1279,719};nui_native_invalidate(&full);nui_native_render();
                assert(!memcmp(proof,fb,sizeof(fb)));
            }
            touch(600,450-delta,false);nui_native_render();
        }
        assert(!callbacks);tab5_native_port_input_close();tab5_ui_destroy();nui_native_deinit();
        assert(!n.stats.allocation_count&&!n.stats.allocation_bytes);
    }
}
int main(int argc,char **argv){
    assert(argc==3||argc==4);setbuf(stdout,NULL);
    if(argc==3){raw=fopen(argv[1],"wb");assert(raw);unit();scroll(1);fclose(raw);
        printf("PASS %u measurement mutations, 104 SETTINGS scroll frames, both themes, partial==full, OOM/retry and deinit\n",unit_checks);
    }else{csv=fopen(argv[1],"w");assert(csv);fprintf(csv,"theme,frame,offset,input_us,render_us\n");scroll(4);fclose(csv);puts("PASS host scroll timing");}
    return 0;
}
