#ifndef NATIVE_BASELINE_CONTRACT_H
#define NATIVE_BASELINE_CONTRACT_H
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
/* v1 contract: 1280x720 native-endian RGB565; opaque full framebuffer.
 * Scene input generation is outside timing. Backend update+input+render and
 * synchronous RGB565-to-RGB888 mock flush/copy are inside timing. No PNG/log IO.
 * All scenes run both themes, fresh init, then identical warmup/frame counts.
 * Touch is one press/release at the given pixel, followed by completion.
 * No input means x/y=-1. UI nominal input cadence is 100ms, wall time is free.
 * Future native adapter must consume the same fields and logical touch steps.
 */
#define NB_CONTRACT_VERSION 1
#define NB_WIDTH 1280
#define NB_HEIGHT 720
enum nb_scene {NB_IDLE,NB_ANGLES,NB_GRAPH,NB_MODAL,NB_TRANSITION,NB_SCENES};
static const char *const nb_names[]={"idle","angles","graph","modal","transition"};
/* Exact frozen flush quantization; candidate adapter must use this expansion,
 * not bit replication (which differs at some channel values). */
static inline uint32_t nb_rgb565_expand(uint16_t v){
    unsigned r=(v>>11)&31,g=(v>>5)&63,b=v&31;
    return ((r*255/31)<<16)|((g*255/63)<<8)|(b*255/31);
}
typedef struct {
    uint64_t now_us,good;
    bool light;
    float x[2],y[2],roll_x[2],roll_y[2];
    int touch_x,touch_y;
} nb_frame_t;
static inline nb_frame_t nb_frame(enum nb_scene scene,bool light,unsigned tick){
    nb_frame_t f={.now_us=1000000000ULL+(uint64_t)tick*100000,.good=(uint64_t)tick+2000,
                  .light=light,.touch_x=-1,.touch_y=-1};
    for(unsigned j=0;j<2;j++){
        float t=scene==NB_IDLE?0:(float)tick;
        f.x[j]=2*sinf(t*.07f+j*.4f);f.y[j]=2*cosf(t*.09f+j*.4f);
        f.roll_x[j]=.1f;f.roll_y[j]=.1f;
    }
    if(scene==NB_MODAL){f.touch_x=(tick&1)?846:432;f.touch_y=302;}
    if(scene==NB_TRANSITION){f.touch_x=52;f.touch_y=132+(tick%5)*88;}
    return f;
}
#endif
