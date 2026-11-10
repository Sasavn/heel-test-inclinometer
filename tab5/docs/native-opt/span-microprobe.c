#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
static uint16_t a[1400],b[1400];
__attribute__((noinline,noclone)) static void old(uint16_t*p,unsigned n,uint16_t c){for(unsigned x=0;x<n;x++)p[x]=c;}
__attribute__((noinline,noclone)) static void opt(uint16_t*p,unsigned n,uint16_t c){if((c&255)==(c>>8))memset(p,c&255,n*2);else for(unsigned x=0;x<n;x++)p[x]=c;}
static double stamp(void){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return t.QuadPart/(double)f.QuadPart;}
int main(void){unsigned widths[]={1,3,16,31,127,512,1280};uint16_t colors[]={0,65535,0x1234};volatile unsigned sink=0;for(unsigned c=0;c<3;c++)for(unsigned w=0;w<7;w++)for(unsigned off=0;off<4;off++){memset(a,0x77,sizeof a);memset(b,0x77,sizeof b);old(a+off,widths[w],colors[c]);opt(b+off,widths[w],colors[c]);assert(!memcmp(a,b,sizeof a));}for(unsigned c=0;c<3;c++){double t=stamp();for(unsigned r=0;r<400000;r++)old(a+(r&3),1280,colors[c]);double x=stamp()-t;t=stamp();for(unsigned r=0;r<400000;r++)opt(b+(r&3),1280,colors[c]);double y=stamp()-t;sink+=a[4]+b[4];printf("%04x old_ms=%.3f opt_ms=%.3f ratio=%.3f\n",colors[c],x*1000,y*1000,x/y);}printf("PASS exact 84 clipped/aligned cases sink=%u\n",sink);}
