/* One bounded 1D RGB565 gradient. Values use exactly the uncached renderer's
 * color mixing/quantization. No image/frame cache, allocation or background
 * blending is stored here; callers still rasterize current clipped geometry. */
#ifndef NATIVE_GRADIENT_H
#define NATIVE_GRADIENT_H
#include "native_ui.h"
#define NUI_GRADIENT_MAX_LENGTH 1280
typedef struct {
    int length;
    nui_color_t begin,end;
    bool valid;
    uint64_t hits,misses,build_pixels;
    uint16_t pixels[NUI_GRADIENT_MAX_LENGTH];
} nui_gradient_cache_t;

static inline const uint16_t *nui_gradient_prepare(nui_gradient_cache_t *cache,int length,
                                                   nui_color_t begin,nui_color_t end){
    if(!cache||length<1||length>NUI_GRADIENT_MAX_LENGTH)return NULL;
    if(cache->valid&&cache->length==length&&cache->begin==begin&&cache->end==end){
#if NUI_PROFILE_FINE_COUNTERS
        cache->hits++;
#endif
        return cache->pixels;
    }
    for(int pos=0;pos<length;pos++){
        uint8_t mix=(uint8_t)(pos*255/(length>1?length-1:1));
        cache->pixels[pos]=nui_color_to_u16(nui_color_mix(end,begin,mix));
    }
    cache->begin=begin;cache->end=end;cache->length=length;cache->valid=true;
#if NUI_PROFILE_FINE_COUNTERS
    cache->misses++;cache->build_pixels+=(uint64_t)length;
#endif
    return cache->pixels;
}
#endif
