#include "tab5_plot.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define STRIDE (TAB5_PLOT_WIDTH+7)
#define GUARD 32
static uint16_t memory[GUARD+STRIDE*TAB5_PLOT_HEIGHT+GUARD];
static uint16_t *const image=memory+GUARD;
static int32_t points[4][1200];
static tab5_plot_t plot;
static void prepare(unsigned n){
    for(unsigned i=0;i<sizeof(memory)/sizeof(*memory);i++)memory[i]=0xDEAD;
    plot=(tab5_plot_t){.y_min=-100,.y_max=100,.none=INT32_MAX,.n=n,.background=0xFFFF,.grid=0x1234};
    for(unsigned k=0;k<4;k++){plot.series[k]=points[k];plot.colors[k]=(uint16_t)(k+1);plot.visible[k]=true;for(unsigned i=0;i<1200;i++)points[k][i]=INT32_MAX;}
}
static void guards(void){
    for(unsigned i=0;i<GUARD;i++){assert(memory[i]==0xDEAD);assert(memory[GUARD+STRIDE*TAB5_PLOT_HEIGHT+i]==0xDEAD);}
    for(unsigned y=0;y<TAB5_PLOT_HEIGHT;y++)for(unsigned x=TAB5_PLOT_WIDTH;x<STRIDE;x++)assert(image[y*STRIDE+x]==0xDEAD);
}
static unsigned count(uint16_t color){unsigned n=0;for(unsigned y=0;y<TAB5_PLOT_HEIGHT;y++)for(unsigned x=0;x<TAB5_PLOT_WIDTH;x++)n+=image[y*STRIDE+x]==color;return n;}
static void tests(void){
    prepare(2);points[0][0]=points[0][1]=0;assert(tab5_plot_draw(image,STRIDE,&plot));
    assert(count(1)==2*TAB5_PLOT_WIDTH);assert(image[181*STRIDE+500]==1&&image[182*STRIDE+500]==1);guards();
    prepare(2);for(unsigned k=0;k<4;k++)points[k][0]=points[k][1]=(int32_t)k*20;
    assert(tab5_plot_draw(image,STRIDE,&plot));for(unsigned k=0;k<4;k++)assert(count((uint16_t)(k+1))==2*TAB5_PLOT_WIDTH);guards();
    plot.visible[1]=false;plot.series[2]=NULL;assert(tab5_plot_draw(image,STRIDE,&plot));assert(!count(2)&&!count(3));
    prepare(5);points[0][0]=points[0][1]=points[0][3]=points[0][4]=0;
    assert(tab5_plot_draw(image,STRIDE,&plot));assert(image[181*STRIDE+500]==plot.background);assert(image[181*STRIDE+50]==1);assert(image[181*STRIDE+1000]==1);guards();
    prepare(3);points[0][1]=0;assert(tab5_plot_draw(image,STRIDE,&plot));assert(!count(1)); /* No head/tail or isolated point segments. */
    prepare(1200);plot.y_min=0;plot.y_max=1;
    for(unsigned k=0;k<4;k++)for(unsigned i=0;i<1200;i++)points[k][i]=(i&1)?INT32_MIN:INT32_MAX-1;
    assert(tab5_plot_draw(image,STRIDE,&plot));assert(count(4)>0);guards(); /* Extreme steep clipped crossings. */
    for(unsigned k=0;k<4;k++)for(unsigned i=0;i<1200;i++)points[k][i]=INT32_MIN;
    assert(tab5_plot_draw(image,STRIDE,&plot));assert(!count(4));guards();
    prepare(0);plot.ticks=3;assert(tab5_plot_draw(image,STRIDE,&plot));assert(count(plot.grid)==3*TAB5_PLOT_WIDTH);guards();
    plot.ticks=UINT_MAX;assert(tab5_plot_draw(image,STRIDE,&plot));assert(count(plot.grid)==TAB5_PLOT_WIDTH*TAB5_PLOT_HEIGHT);guards();
    prepare(2);assert(!tab5_plot_draw(NULL,STRIDE,&plot));assert(!tab5_plot_draw(image,TAB5_PLOT_WIDTH-1,&plot));
    assert(!tab5_plot_draw(image,SIZE_MAX,&plot));plot.y_max=plot.y_min;assert(!tab5_plot_draw(image,STRIDE,&plot));
    plot.y_max=100;plot.n=1201;assert(!tab5_plot_draw(image,STRIDE,&plot));for(unsigned i=0;i<sizeof(memory)/sizeof(*memory);i++)assert(memory[i]==0xDEAD);
    prepare(2);plot.none=-999;points[0][0]=points[0][1]=-999;assert(tab5_plot_draw(image,STRIDE,&plot));assert(!count(1));guards();
    prepare(2);plot.y_min=INT32_MIN;plot.y_max=INT32_MAX;points[0][0]=INT32_MIN;points[0][1]=INT32_MAX-1;
    assert(tab5_plot_draw(image,STRIDE,&plot));assert(count(1)>0);guards();
    /* Deterministic extreme-coordinate/stride stress; no LVGL or heap. */
    uint32_t random=12345;
    for(unsigned trial=0;trial<64;trial++){
        prepare(1200);plot.y_min=-1;plot.y_max=1;
        for(unsigned k=0;k<4;k++)for(unsigned i=0;i<1200;i++){
            random=random*1664525u+1013904223u;
            points[k][i]=(i%11==0)?plot.none:(int32_t)(random&INT32_MAX)*(random&1?-1:1);
        }
        assert(tab5_plot_draw(image,STRIDE,&plot));guards();
    }
    puts("PASS guards, stride padding, solid 2px, colors, hidden/null, None, no extrapolation, extremes, grid, invalid arguments");
}
static void benchmark(unsigned n){
    prepare(n);plot.y_min=-250;plot.y_max=250;plot.ticks=7;
    for(unsigned k=0;k<4;k++)for(unsigned i=0;i<n;i++)points[k][i]=(int32_t)(180*sin(i*.025+k));
    const unsigned frames=500;clock_t start=clock();
    for(unsigned i=0;i<frames;i++){points[0][n-1]=(int32_t)(i%200)-100;assert(tab5_plot_draw(image,STRIDE,&plot));}
    clock_t elapsed=clock()-start;
    double seconds=(double)elapsed/CLOCKS_PER_SEC;
    printf("BENCH native -O2: 4 x %u points, %u frames, %.3f ms/frame, %.4f clock ticks/frame (CLOCKS_PER_SEC=%ld)\n",n,frames,seconds*1000/frames,(double)elapsed/frames,(long)CLOCKS_PER_SEC);
    guards();
}
int main(void){tests();benchmark(300);benchmark(1200);return 0;}
