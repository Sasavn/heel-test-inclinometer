#include "tab5_level.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#define STRIDE 207
#define GUARD 16
static uint16_t memory[GUARD+STRIDE*200+GUARD];
static uint16_t *const p=memory+GUARD;
static void init(void){for(unsigned i=0;i<sizeof(memory)/sizeof(*memory);i++)memory[i]=0xDEAD;}
static void guards(void){
    for(unsigned i=0;i<GUARD;i++){assert(memory[i]==0xDEAD);assert(memory[GUARD+STRIDE*200+i]==0xDEAD);}
    for(unsigned y=0;y<200;y++)for(unsigned x=200;x<STRIDE;x++)assert(p[y*STRIDE+x]==0xDEAD);
}
static unsigned color_count(uint16_t c){unsigned n=0;for(unsigned y=0;y<200;y++)for(unsigned x=0;x<200;x++)n+=p[y*STRIDE+x]==c;return n;}
int main(void){
    int x=-1,y=-1;
    assert(!tab5_level_position(NAN,0,true,&x,&y)&&x==-1&&y==-1);
    assert(!tab5_level_position(0,INFINITY,true,&x,&y));assert(!tab5_level_position(0,0,false,&x,&y));
    assert(!tab5_level_position(0,0,true,NULL,&y));assert(!tab5_level_position(0,0,true,&x,NULL));
    assert(tab5_level_position(0,0,true,&x,&y)&&x==100&&y==100);
    assert(tab5_level_position(5,0,true,&x,&y)&&x==190&&y==100);
    assert(tab5_level_position(0,50,true,&x,&y)&&x==100&&y==10);
    assert(tab5_level_position(3,4,true,&x,&y)&&x==154&&y==28);
    assert(tab5_level_position(30,40,true,&x,&y)&&x==154&&y==28);
    assert(tab5_level_position(.01f,.01f,true,&x,&y)&&x==100&&y==100);
    assert(tab5_level_position(FLT_MAX,-FLT_MAX,true,&x,&y)&&x>=10&&x<=190&&y>=10&&y<=190);
    tab5_level_t level={.card_bg=1,.alt_circle_bg=2,.border=3,.sensor_color=4};
    init();assert(tab5_level_draw(p,STRIDE,&level));guards();
    assert(p[0]==1&&p[100*STRIDE+5]==2&&p[100*STRIDE+4]==3);
    assert(p[10*STRIDE+100]==3&&p[64*STRIDE+100]==3&&p[82*STRIDE+100]==3);
    assert(p[100*STRIDE+6]==3&&!color_count(4));
    level.valid=true;assert(tab5_level_draw(p,STRIDE,&level));assert(color_count(4)==149);guards();
    level.x=100;assert(tab5_level_draw(p,STRIDE,&level));assert(p[100*STRIDE+190]==4&&p[100*STRIDE+197]==4);guards();
    level.x=NAN;assert(tab5_level_draw(p,STRIDE,&level));assert(!color_count(4));guards();
    level.x=FLT_MAX;level.y=FLT_MAX;assert(tab5_level_draw(p,STRIDE,&level));guards();
    init();assert(!tab5_level_draw(p,199,&level));assert(!tab5_level_draw(p,SIZE_MAX,&level));
    assert(!tab5_level_draw(NULL,STRIDE,&level));assert(!tab5_level_draw(p,STRIDE,NULL));
    for(unsigned i=0;i<sizeof(memory)/sizeof(*memory);i++)assert(memory[i]==0xDEAD);
    init();clock_t start=clock();const unsigned frames=1000;
    for(unsigned i=0;i<frames;i++){level.x=4*sinf(i*.01f);level.y=4*cosf(i*.01f);assert(tab5_level_draw(p,STRIDE,&level));}
    clock_t elapsed=clock()-start;guards();
    printf("PASS level geometry, quantization, clamp, solid marker, invalid/NaN, extremes, guards, padding, invalid arguments\n");
    printf("BENCH native -O2: %u moving frames, %.3f ms/frame\n",frames,1000.0*elapsed/CLOCKS_PER_SEC/frames);return 0;
}
