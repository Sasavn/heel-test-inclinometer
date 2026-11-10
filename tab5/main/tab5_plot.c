#include "tab5_plot.h"

static void pixel(uint16_t *p,size_t stride,int x,int y,uint16_t color)
{
    if((unsigned)x<TAB5_PLOT_WIDTH&&(unsigned)y<TAB5_PLOT_HEIGHT)
        p[(size_t)y*stride+(unsigned)x]=color;
}

/* x is always on-screen. Mapping an int32 extreme against a one-unit y
 * range can produce +/-1.56e12: clip in int64 BEFORE bounded Bresenham.
 * dx<=1039, so clipping products are below 3.3e15 (well inside int64). */
static void segment(uint16_t *p,size_t stride,int ax,int64_t ay,int bx,int64_t by,uint16_t color)
{
    const int64_t top=-1,bottom=TAB5_PLOT_HEIGHT-1;
    if((ay<top&&by<top)||(ay>bottom&&by>bottom))return;
    int64_t x0=ax,x1=bx,y0=ay,y1=by;
    if(ay<top||ay>bottom){y0=ay<top?top:bottom;x0=ax+(int64_t)(bx-ax)*(y0-ay)/(by-ay);}
    if(by<top||by>bottom){y1=by<top?top:bottom;x1=ax+(int64_t)(bx-ax)*(y1-ay)/(by-ay);}
    int x=(int)x0,y=(int)y0,end_x=(int)x1,end_y=(int)y1;
    int dx=end_x-x;if(dx<0)dx=-dx;
    int dy=end_y-y;if(dy<0)dy=-dy;
    int sx=x<end_x?1:-1,sy=y<end_y?1:-1,error=dx-dy;
    bool steep=dy>dx;
    for(;;){
        pixel(p,stride,x,y,color);
        pixel(p,stride,x+(steep?1:0),y+(steep?0:1),color);
        if(x==end_x&&y==end_y)break;
        int twice=2*error;
        if(twice>-dy){error-=dy;x+=sx;}
        if(twice<dx){error+=dx;y+=sy;}
    }
}

bool tab5_plot_draw(uint16_t *pixels,size_t stride,const tab5_plot_t *plot)
{
    if(!pixels||!plot||stride<TAB5_PLOT_WIDTH||
       stride>SIZE_MAX/sizeof(*pixels)/TAB5_PLOT_HEIGHT||
       plot->n>TAB5_PLOT_MAX_POINTS||plot->y_min>=plot->y_max)return false;
    for(unsigned y=0;y<TAB5_PLOT_HEIGHT;y++){
        uint16_t *row=pixels+(size_t)y*stride;
        for(unsigned x=0;x<TAB5_PLOT_WIDTH;x++)row[x]=plot->background;
    }
    unsigned ticks=plot->ticks;
    if(ticks>TAB5_PLOT_HEIGHT)ticks=TAB5_PLOT_HEIGHT;
    for(unsigned i=0;i<ticks;i++){
        unsigned y=ticks==1?TAB5_PLOT_HEIGHT-1:i*(TAB5_PLOT_HEIGHT-1)/(ticks-1);
        uint16_t *row=pixels+(size_t)y*stride;
        for(unsigned x=0;x<TAB5_PLOT_WIDTH;x++)row[x]=plot->grid;
    }
    if(plot->n<2)return true;
    int64_t span=(int64_t)plot->y_max-plot->y_min;
    for(unsigned k=0;k<TAB5_PLOT_SERIES;k++){
        const int32_t *values=plot->series[k];
        if(!plot->visible[k]||!values)continue;
        bool previous=false;int px=0;int64_t py=0;
        for(unsigned i=0;i<plot->n;i++){
            int32_t value=values[i];
            if(value==plot->none){previous=false;continue;}
            int x=(int)(i*(TAB5_PLOT_WIDTH-1)/(plot->n-1));
            int64_t y=((int64_t)plot->y_max-value)*(TAB5_PLOT_HEIGHT-1)/span;
            if(previous)segment(pixels,stride,px,py,x,y,plot->colors[k]);
            previous=true;px=x;py=y;
        }
    }
    return true;
}
