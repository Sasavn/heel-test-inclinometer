#include "tab5_level.h"
#include <math.h>

bool tab5_level_position(float x,float y,bool valid,int *px,int *py)
{
    if(!valid||!px||!py||!isfinite(x)||!isfinite(y))return false;
    /* Double intermediates avoid overflow even for finite FLT_MAX inputs. */
    double dx=x,dy=y,r=hypot(dx,dy);
    if(r>5){dx*=5/r;dy*=5/r;}
    *px=100+(int)lround(dx*18);*py=100-(int)lround(dy*18);
    return true;
}
static void pixel(uint16_t *p,size_t stride,int x,int y,uint16_t color)
{
    if((unsigned)x<200&&(unsigned)y<200)p[(size_t)y*stride+(unsigned)x]=color;
}
static void ring(uint16_t *p,size_t stride,int radius,uint16_t color)
{
    int x=radius,y=0,error=1-radius;
    while(x>=y){
        pixel(p,stride,100+x,100+y,color);pixel(p,stride,100+y,100+x,color);
        pixel(p,stride,100-y,100+x,color);pixel(p,stride,100-x,100+y,color);
        pixel(p,stride,100-x,100-y,color);pixel(p,stride,100-y,100-x,color);
        pixel(p,stride,100+y,100-x,color);pixel(p,stride,100+x,100-y,color);
        y++;
        if(error<0)error+=2*y+1;else{x--;error+=2*(y-x)+1;}
    }
}
static void filled(uint16_t *p,size_t stride,int cx,int cy,int radius,uint16_t color)
{
    for(int y=-radius;y<=radius;y++){
        int half=radius;while(half*half+y*y>radius*radius)half--;
        for(int x=-half;x<=half;x++)pixel(p,stride,cx+x,cy+y,color);
    }
}
static void line(uint16_t *p,size_t stride,int x,int y,uint16_t color)
{
    int end_x=x,end_y=y;x=100;y=100;
    int dx=end_x-x;if(dx<0)dx=-dx;
    int dy=end_y-y;if(dy<0)dy=-dy;
    int sx=x<end_x?1:-1,sy=y<end_y?1:-1,error=dx-dy;
    for(;;){
        pixel(p,stride,x,y,color);if(x==end_x&&y==end_y)break;
        int twice=2*error;
        if(twice>-dy){error-=dy;x+=sx;}
        if(twice<dx){error+=dx;y+=sy;}
    }
}
bool tab5_level_draw(uint16_t *pixels,size_t stride,const tab5_level_t *level)
{
    if(!pixels||!level||stride<200||stride>SIZE_MAX/sizeof(*pixels)/200)return false;
    for(unsigned y=0;y<200;y++)for(unsigned x=0;x<200;x++)pixels[(size_t)y*stride+x]=level->card_bg;
    filled(pixels,stride,100,100,96,level->alt_circle_bg);
    ring(pixels,stride,96,level->border);ring(pixels,stride,90,level->border);
    ring(pixels,stride,36,level->border);ring(pixels,stride,18,level->border);
    for(int i=6;i<=194;i++){pixel(pixels,stride,i,100,level->border);pixel(pixels,stride,100,i,level->border);}
    int px,py;
    if(tab5_level_position(level->x,level->y,level->valid,&px,&py)){
        line(pixels,stride,px,py,level->sensor_color);
        filled(pixels,stride,px,py,7,level->sensor_color);
    }
    return true;
}
