#include "apa102.h"
#include <string.h>

size_t apa102_frame_size(size_t count) { return 4+4*count+(count+15)/16; }
bool apa102_encode(const PixelConfig *c,const uint8_t *rgb,size_t count,uint8_t *wire,size_t capacity) {
    if(!c||!rgb||!wire||count!=c->count||capacity<apa102_frame_size(count))return false;
    memset(wire,0,4);
    for(size_t i=0;i<count;i++){
        wire[4+i*4]=255; // Full five-bit global brightness; scale RGB only once.
        for(unsigned n=0;n<3;n++){
            unsigned channel=c->order[n]=='R'?0:c->order[n]=='G'?1:2;
            wire[5+i*4+n]=(rgb[i*3+channel]*c->brightness+127u)/255u;
        }
    }
    // Each additional chip needs half a clock to shift the final pixel through.
    memset(wire+4+4*count,255,(count+15)/16);
    return true;
}

__attribute__((weak)) bool platform_apa102_supported(void) { return false; }
__attribute__((weak)) bool platform_apa102_open(const PixelConfig *c,void **p) { (void)c;(void)p;return false; }
__attribute__((weak)) void platform_apa102_close(void *p) { (void)p; }
__attribute__((weak)) bool platform_apa102_submit(void *p,const uint8_t *r,size_t n) { (void)p;(void)r;(void)n;return false; }
__attribute__((weak)) int platform_apa102_poll(void *p) { (void)p;return -1; }
__attribute__((weak)) void platform_apa102_cancel(void *p) { (void)p; }
__attribute__((weak)) bool platform_apa102_show(void *p,const uint8_t *r,size_t n) { (void)p;(void)r;(void)n;return false; }
