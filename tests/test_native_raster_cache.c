/* Frozen RGB565 reference math for the pre-span/pre-AA-LUT raster contract
 * (2313731, 2026-10-27). Do not regenerate references from the implementation.
 * White-box include compiles the real production C file, exposing cache lifetime
 * and allocation-failure paths without adding test hooks to firmware.
 * References below use no production blend/coverage/shadow/cache helpers.
 */
#include <stdbool.h>
#include <stdlib.h>
static bool fail_alloc;
static unsigned alloc_calls, failed_allocs;
static void *raster_calloc(size_t count, size_t size) {
    alloc_calls++;
    if(fail_alloc) { failed_allocs++; return NULL; }
    return calloc(count, size);
}
#define calloc raster_calloc
#include "../tab5/main/native/native_ui.c"
#undef calloc

enum { W=256, H=220 };
static uint16_t actual[W*H], expected[W*H], background[W*H];
static unsigned rng;
static unsigned random_u32(void) { rng=rng*1664525u+1013904223u; return rng; }
static int ref_min(int a,int b) { return a<b?a:b; }
static int ref_max(int a,int b) { return a>b?a:b; }
static uint16_t rgb565(unsigned c) {
    return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x07e0)|((c>>3)&31));
}
/* Frozen light/dark SETTINGS colors: card, frame, border, outline, shadow. */
static const unsigned palettes[2][5]={
    {0x1B2233,0x2B3340,0x2E3850,0x77829C,0x000000},
    {0xFFFFFF,0xF6F7FA,0xD5DBE6,0x7D879C,0x1A294D}
};
static const unsigned opacities[]={0,1,22,70,160,254,255};
static unsigned current_theme, pixel_checks, shadow_checks, coverage_checks;

static uint16_t ref_blend(uint16_t dst,uint16_t src,unsigned alpha) {
    if(alpha>=255) return src;
    if(!alpha) return dst;
    unsigned mix=(alpha+4)>>3, inv=32-mix;
    unsigned r=(((src>>11)&31)*mix+((dst>>11)&31)*inv)>>5;
    unsigned g=(((src>>5)&63)*mix+((dst>>5)&63)*inv)>>5;
    unsigned b=((src&31)*mix+(dst&31)*inv)>>5;
    return (uint16_t)((r<<11)|(g<<5)|b);
}
static bool ref_intersect(nui_area_t *out,nui_area_t a,nui_area_t b) {
    *out=(nui_area_t){ref_max(a.x1,b.x1),ref_max(a.y1,b.y1),
                      ref_min(a.x2,b.x2),ref_min(a.y2,b.y2)};
    return out->x1<=out->x2&&out->y1<=out->y2;
}
static void ref_pixel(nui_layer_t *l,int x,int y,uint16_t c,unsigned alpha) {
    if(x<l->clip.x1||x>l->clip.x2||y<l->clip.y1||y>l->clip.y2) return;
    assert(x>=0&&x<W&&y>=0&&y<H);
    alpha=alpha*l->opa/255;
    expected[y*W+x]=ref_blend(expected[y*W+x],c,alpha);
}
static void ref_span(nui_layer_t *l,int from,int to,int y,uint16_t c,unsigned alpha) {
    for(int x=from;x<=to;x++) ref_pixel(l,x,y,c,alpha);
}
static unsigned ref_round(nui_area_t a,int r,int x,int y) {
    if(r<=0) return 255;
    int cx=x<a.x1+r?a.x1+r:x>a.x2-r?a.x2-r:x;
    int cy=y<a.y1+r?a.y1+r:y>a.y2-r?a.y2-r:y;
    if(cx==x||cy==y) return 255;
    unsigned hit=0;int64_t rr=(int64_t)(r*4)*(r*4);
    for(int sy=1;sy<=3;sy+=2) for(int sx=1;sx<=3;sx+=2) {
        int64_t dx=4*(x-cx)+sx-2,dy=4*(y-cy)+sy-2;
        if(dx*dx+dy*dy<=rr) hit++;
    }
    return hit*255/4;
}
static int ref_radius(nui_area_t a,int r) {
    return ref_max(0,ref_min(r,ref_min(a.x2-a.x1+1,a.y2-a.y1+1)/2));
}
static void ref_rectangle(nui_layer_t *l,nui_area_t a,int r,uint16_t c,unsigned alpha) {
    nui_area_t clip;if(!ref_intersect(&clip,a,l->clip)) return;
    r=ref_radius(a,r);
    for(int y=clip.y1;y<=clip.y2;y++) for(int x=clip.x1;x<=clip.x2;x++)
        ref_pixel(l,x,y,c,ref_round(a,r,x,y)*alpha/255);
}
static void ref_border_pixel(nui_layer_t *l,nui_area_t a,nui_area_t inner,
                             int r,int width,int x,int y,uint16_t c,unsigned alpha) {
    unsigned outer=ref_round(a,r,x,y),inside=0;
    if(x>=inner.x1&&x<=inner.x2&&y>=inner.y1&&y<=inner.y2)
        inside=ref_round(inner,ref_max(0,r-width),x,y);
    if(outer>inside) ref_pixel(l,x,y,c,(outer-inside)*alpha/255);
}
/* Preserve original interval order, including overlapping edge strips when
 * border width exceeds the width of a narrow shape (alpha composes twice). */
static void ref_border(nui_layer_t *l,nui_area_t a,int r,int width,uint16_t c,unsigned alpha) {
    if(width<=0) return;
    nui_area_t clip;if(!ref_intersect(&clip,a,l->clip)) return;
    r=ref_radius(a,r);
    nui_area_t inner={a.x1+width,a.y1+width,a.x2-width,a.y2-width};
    int edge=ref_max(r,width);
    for(int y=clip.y1;y<=clip.y2;y++) {
        if(y<a.y1+edge||y>a.y2-edge) {
            if(y<a.y1+width||y>a.y2-width) {
                int left=ref_max(clip.x1,a.x1+r),right=ref_min(clip.x2,a.x2-r);
                if(left<=right) ref_span(l,left,right,y,c,alpha);
                for(int x=clip.x1;x<left;x++) ref_border_pixel(l,a,inner,r,width,x,y,c,alpha);
                for(int x=ref_max(right+1,clip.x1);x<=clip.x2;x++) ref_border_pixel(l,a,inner,r,width,x,y,c,alpha);
            } else {
                for(int x=clip.x1;x<=ref_min(clip.x2,a.x1+edge);x++) ref_border_pixel(l,a,inner,r,width,x,y,c,alpha);
                for(int x=ref_max(clip.x1,a.x2-edge);x<=clip.x2;x++) ref_border_pixel(l,a,inner,r,width,x,y,c,alpha);
            }
        } else {
            ref_span(l,clip.x1,ref_min(clip.x2,a.x1+width-1),y,c,alpha);
            ref_span(l,ref_max(clip.x1,a.x2-width+1),clip.x2,y,c,alpha);
        }
    }
}
static bool ref_inside(nui_area_t a,int r,int x,int y) {
    if(x<a.x1||x>a.x2||y<a.y1||y>a.y2) return false;
    r=ref_radius(a,r);
    if(!r||(x>=a.x1+r&&x<=a.x2-r)||(y>=a.y1+r&&y<=a.y2-r)) return true;
    int cx=x<a.x1+r?a.x1+r:a.x2-r,cy=y<a.y1+r?a.y1+r:a.y2-r;
    int64_t dx=x-cx,dy=y-cy;
    return dx*dx+dy*dy<=(int64_t)r*r;
}
static const unsigned weights[9]={1,8,28,56,70,56,28,8,1};
static unsigned ref_blur_axis(int at,int low,int high,int blur) {
    unsigned sum=0;
    for(int k=-4;k<=4;k++) {
        int pos=at+k*blur/4;if(pos>=low&&pos<=high) sum+=weights[k+4];
    }
    return sum;
}
static unsigned ref_shadow_alpha(nui_area_t a,int r,int blur,int offset,unsigned opa,int x,int y) {
    if(ref_inside(a,ref_max(0,r-1),x,y)) return 0;
    nui_area_t shape={a.x1,a.y1+offset,a.x2,a.y2+offset};unsigned coverage=0;
    if(x>=shape.x1+r+blur&&x<=shape.x2-r-blur)
        coverage=ref_blur_axis(y,shape.y1,shape.y2,blur)*256;
    else if(y>=shape.y1+r+blur&&y<=shape.y2-r-blur)
        coverage=ref_blur_axis(x,shape.x1,shape.x2,blur)*256;
    else for(int iy=-4;iy<=4;iy++) for(int ix=-4;ix<=4;ix++)
        if(ref_inside(shape,r,x+ix*blur/4,y+iy*blur/4)) coverage+=weights[ix+4]*weights[iy+4];
    unsigned alpha=(coverage*opa+32768)>>16;
    return alpha<(opa+1)/2?alpha:(opa+1)/2;
}
static void ref_shadow(nui_layer_t *l,nui_area_t a,int r,int width,int offset,uint16_t c,unsigned opa) {
    int blur=ref_max(1,(width+1)/2);
    nui_area_t expanded={a.x1-blur,a.y1+offset-blur,a.x2+blur,a.y2+offset+blur},clip;
    if(!ref_intersect(&clip,expanded,l->clip)) return;
    r=ref_radius(a,r);int edge=ref_max(r+blur+abs(offset),blur+1);
    for(int y=clip.y1;y<=clip.y2;y++) {
        int intervals[4]={clip.x1,clip.x2,1,0};
        if(y>=a.y1+edge&&y<=a.y2-edge) {
            intervals[1]=ref_min(clip.x2,a.x1+edge);
            intervals[2]=ref_max(clip.x1,a.x2-edge);intervals[3]=clip.x2;
            if(intervals[2]<=intervals[1]) { intervals[1]=clip.x2;intervals[2]=1;intervals[3]=0; }
        }
        for(unsigned run=0;run<2;run++) for(int x=intervals[run*2];x<=intervals[run*2+1];x++)
            ref_pixel(l,x,y,c,ref_shadow_alpha(a,r,blur,offset,opa,x,y));
    }
}
static void reset_pixels(void) {
    memcpy(actual,background,sizeof(actual));memcpy(expected,background,sizeof(expected));n.fb=actual;
}
static void check_pixels(const char *kind,unsigned test) {
    if(memcmp(actual,expected,sizeof(actual))) {
        for(unsigned i=0;i<W*H;i++) if(actual[i]!=expected[i]) {
            fprintf(stderr,"FAIL %s theme=%u case=%u at (%u,%u): actual=%04x reference=%04x\n",
                    kind,current_theme,test,i%W,i/W,actual[i],expected[i]);exit(1);
        }
    }
    pixel_checks++;
}
static void seed_background(unsigned theme) {
    rng=0x2710174u;
    for(unsigned i=0;i<W*H;i++)
        background[i]=i%3?(uint16_t)random_u32():rgb565(palettes[theme][0]);
}
static void check_lut(void) {
    for(int r=1;r<=32;r++) {
        nui_area_t a={3,5,3+2*r+7,5+2*r+11};const uint8_t *table=round_table(r);assert(table);
        for(int y=a.y1;y<=a.y2;y++) for(int x=a.x1;x<=a.x2;x++) {
            assert(cached_round_coverage(a,r,x,y,table)==ref_round(a,r,x,y));coverage_checks++;
        }
        unsigned calls=alloc_calls;assert(round_table(r)==table);assert(alloc_calls==calls);
        assert(cached_round_coverage(a,r,a.x1-1,a.y1-1,table)==ref_round(a,r,a.x1-1,a.y1-1));
    }
    size_t bytes=0;for(int r=1;r<=32;r++) if(round_cache[r]) bytes+=(size_t)r*r;
    assert(bytes==11440);
    unsigned calls=alloc_calls;assert(!round_table(0)&&!round_table(-1)&&!round_table(33)&&!round_table(1000));assert(alloc_calls==calls);
    for(int r=0;r<80;r++) {
        nui_area_t a={0,0,2*r+1,2*r+1};
        for(int y=0;y<=2*r+1;y++) for(int x=0;x<=2*r+1;x++)
            assert(cached_round_coverage(a,r,x,y,NULL)==ref_round(a,r,x,y));
    }
}
static void check_round_pixels(unsigned theme) {
    rng=0x2710174u;
    for(unsigned test=0;test<2400;test++) {
        int w=1+random_u32()%240,h=1+random_u32()%180,r=(int)(random_u32()%100)-4;
        if(test%19==0) r=NUI_RADIUS_CIRCLE;
        int width=(int)(random_u32()%50)-2;unsigned opa=opacities[test%7];
        uint16_t c=test%2?rgb565(palettes[theme][test%4]):(uint16_t)random_u32();
        nui_area_t a={8,18,8+w-1,18+h-1};nui_layer_t layer={{0,0,W-1,H-1},(uint8_t)opacities[(test/7)%7]};
        if(test%3) {
            layer.clip.x1=random_u32()%210;layer.clip.x2=layer.clip.x1+random_u32()%(W-layer.clip.x1);
            layer.clip.y1=random_u32()%160;layer.clip.y2=layer.clip.y1+random_u32()%(H-layer.clip.y1);
        }
        reset_pixels();rectangle(&layer,a,r,c,opa);ref_rectangle(&layer,a,r,c,opa);check_pixels("fill",test);
        reset_pixels();border(&layer,a,r,width,c,opa);ref_border(&layer,a,r,width,c,opa);check_pixels("border",test);
    }
}
static void check_shadow_pixels(unsigned theme) {
    static const unsigned widths[]={1,8,31,64,127,193,194,240};rng=0x191727;
    for(unsigned test=0;test<1200;test++) {
        int w=widths[test%8],h=1+random_u32()%170,r=random_u32()%48;
        int width=test%11==0?33:1+random_u32()%16,offset=(int)(random_u32()%25)-12;
        unsigned opa=test%4?opacities[test%7]:(theme?22:70);
        uint16_t c=test%2?rgb565(palettes[theme][4]):(uint16_t)random_u32();
        nui_area_t a={8,18,8+w-1,18+h-1};nui_layer_t layer={{0,0,W-1,H-1},(uint8_t)opacities[(test/7)%7]};
        if(test%3) {
            layer.clip.x1=random_u32()%100;layer.clip.x2=120+random_u32()%136;
            layer.clip.y1=random_u32()%80;layer.clip.y2=100+random_u32()%120;
        }
        reset_pixels();shadow_render(&layer,a,r,width,offset,c,opa);ref_shadow(&layer,a,r,width,offset,c,opa);
        check_pixels("shadow",test);shadow_checks++;
    }
}
static void check_fallbacks(unsigned theme) {
    nui_area_t a={8,18,170,180};nui_layer_t l={{0,0,W-1,H-1},160};uint16_t c=rgb565(palettes[theme][3]);
    round_cache_clear();fail_alloc=true;unsigned failures=failed_allocs;
    reset_pixels();rectangle(&l,a,14,c,160);border(&l,a,14,2,c,254);
    ref_rectangle(&l,a,14,c,160);ref_border(&l,a,14,2,c,254);check_pixels("round allocation failure",0);
    assert(failed_allocs==failures+3);for(int r=1;r<=32;r++) assert(!round_cache[r]);
    fail_alloc=false;assert(round_table(14));
    shadow_cache_clear();fail_alloc=true;failures=failed_allocs;
    reset_pixels();shadow_render(&l,a,20,8,2,c,70);ref_shadow(&l,a,20,8,2,c,70);check_pixels("shadow allocation failure",0);
    assert(failed_allocs==failures+1&&!shadow_cache_bytes);fail_alloc=false;
    assert(shadow_mask(240,180,20,8,2,70));
    unsigned calls=alloc_calls;assert(shadow_mask(240,180,20,8,2,70));assert(alloc_calls==calls);
    /* Unsupported cache keys exercise direct math: width, offset, cap guards. */
    assert(!shadow_mask(240,180,20,33,2,70));assert(!shadow_mask(240,180,20,8,33,70));
    assert(!shadow_mask(240,180,96,8,2,70));
    static const int fallback[3][3]={{20,33,2},{20,8,33},{96,8,2}};
    for(unsigned i=0;i<3;i++) {
        nui_area_t shape=i==2?(nui_area_t){8,18,247,197}:a;
        reset_pixels();shadow_render(&l,shape,fallback[i][0],fallback[i][1],fallback[i][2],c,70);
        ref_shadow(&l,shape,fallback[i][0],fallback[i][1],fallback[i][2],c,70);check_pixels("shadow key fallback",i);
    }
    /* More keys than slots, plus byte-budget pressure and live table accounting. */
    shadow_cache_clear();
    for(int r=1;r<=60;r++) {
        assert(shadow_mask(240,180,r,8,2,70));size_t total=0;unsigned slots=0;
        for(unsigned i=0;i<SHADOW_CACHE_SLOTS;i++) if(shadow_cache[i].alpha) { total+=shadow_cache[i].bytes;slots++; }
        assert(slots<=8&&total==shadow_cache_bytes&&total<=65536);
        if(r==60) assert(slots<8); /* byte limit evicted before slot limit */
    }
    nui_area_t wide={8,18,247,197};reset_pixels();
    shadow_render(&l,wide,60,8,2,c,70);ref_shadow(&l,wide,60,8,2,c,70);
    check_pixels("shadow after budget eviction",0);
    /* Invalid clips have no writes and allocate nothing. */
    l.clip=(nui_area_t){20,20,19,19};calls=alloc_calls;reset_pixels();
    rectangle(&l,a,14,c,255);border(&l,a,14,2,c,255);shadow_render(&l,a,20,8,2,c,70);
    check_pixels("empty clip",0);assert(alloc_calls==calls);
}
int main(void) {
    for(unsigned theme=0;theme<2;theme++) {
        current_theme=theme;seed_background(theme);assert(nui_native_init(W,H,actual));
        check_lut();check_round_pixels(theme);check_shadow_pixels(theme);check_fallbacks(theme);
        n.fb=actual;nui_native_deinit();nui_native_deinit();
        assert(!n.stats.allocation_count&&!n.stats.allocation_bytes&&!shadow_cache_bytes);
        for(int r=1;r<=32;r++) assert(!round_cache[r]);
        for(unsigned i=0;i<SHADOW_CACHE_SLOTS;i++) assert(!shadow_cache[i].alpha);
    }
    assert(coverage_checks==139904&&shadow_checks==2400);
    printf("PASS dark+light RGB565: 9600 fill/border + 2400 shadow cases; %u total bitmap checks; "
           "139904 LUT samples; calloc failure/retry, cache guards/hits/eviction/bounds, double deinit/reinit\n",pixel_checks);
    return 0;
}
