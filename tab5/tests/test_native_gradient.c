#include "native/native_gradient.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static nui_gradient_cache_t cache;
static uint32_t random_color(uint32_t *seed){*seed=*seed*1664525u+1013904223u;return *seed&0xffffff;}
int main(void){
    const int lengths[]={1,2,7,31,128,364,720,1144,1280};
    uint32_t seed=0x4e554931;unsigned comparisons=0;
    for(unsigned colors=0;colors<32;colors++){
        nui_color_t begin=random_color(&seed),end=random_color(&seed);
        for(unsigned i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++){
            int length=lengths[i];const uint16_t *row=nui_gradient_prepare(&cache,length,begin,end);assert(row);
            for(int pos=0;pos<length;pos++){
                uint8_t mix=(uint8_t)(pos*255/(length>1?length-1:1));
                assert(row[pos]==nui_color_to_u16(nui_color_mix(end,begin,mix)));comparisons++;
            }
            uint16_t copy[NUI_GRADIENT_MAX_LENGTH];memcpy(copy,row,(size_t)length*2);
            assert(nui_gradient_prepare(&cache,length,begin,end)==row);
            assert(!memcmp(copy,row,(size_t)length*2));
            assert(!nui_gradient_prepare(&cache,0,begin,end));
            assert(!nui_gradient_prepare(&cache,-1,begin,end));
            assert(!nui_gradient_prepare(&cache,NUI_GRADIENT_MAX_LENGTH+1,begin,end));
            assert(nui_gradient_prepare(&cache,length,begin,end)==row);
            assert(!memcmp(copy,row,(size_t)length*2));
            /* Same width after theme/color change must not reuse stale RGB. */
            row=nui_gradient_prepare(&cache,length,begin^0xffffff,end^0xffffff);assert(row);
            for(int pos=0;pos<length;pos++){
                uint8_t mix=(uint8_t)(pos*255/(length>1?length-1:1));
                assert(row[pos]==nui_color_to_u16(nui_color_mix(end^0xffffff,begin^0xffffff,mix)));comparisons++;
            }
        }
    }
    assert(!nui_gradient_prepare(NULL,1,0,0));
    printf("native gradient: %u exact RGB565 samples; width/theme/reuse/invalid-key checks PASS; fixed cache=%zu bytes\n",comparisons,sizeof(cache));
    return 0;
}
