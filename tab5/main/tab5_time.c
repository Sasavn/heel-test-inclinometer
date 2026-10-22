#include "tab5_time.h"
bool tab5_time_fields_valid(int y,int m,int d,int h,int mi,int s) {
    static const unsigned char days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(y<2000 || y>2099 || m<1 || m>12 || d<1 || h<0 || h>23 || mi<0 || mi>59 || s<0 || s>59)return false;
    unsigned limit=days[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0));
    return (unsigned)d<=limit;
}
