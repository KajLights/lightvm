#include "draw.h"
#include <math.h>
#include <string.h>

unsigned draw_stride(unsigned format) {
    return format == DRAW_IMAGE_RGBWA ? 5u :
        format == DRAW_RGBA || format == DRAW_IMAGE_RGBA || format == DRAW_IMAGE_RGBW ? 4u : 3u;
}
unsigned draw_channels(unsigned format) {
    return format == DRAW_IMAGE_RGB ? 7u :
        format == DRAW_RGBA || format == DRAW_IMAGE_RGBA ? 23u :
        format == DRAW_IMAGE_RGBW ? 15u : format == DRAW_IMAGE_RGBWA ? 31u : 0u;
}
int draw_channel(unsigned format, unsigned channel) {
    if (channel > 4 || !(draw_channels(format) & (1u << channel))) return -1;
    return channel == 4 && format != DRAW_IMAGE_RGBWA ? 3 : (int)channel;
}
size_t draw_batch_bytes(unsigned capacity, unsigned fields) {
    size_t stride=(sizeof(DrawInstance)+fields*4u+7u)&~(size_t)7u;
    return sizeof(DrawBatch) + (size_t)capacity * stride;
}
DrawInstance *draw_instance(DrawBatch *batch, unsigned slot, unsigned fields) {
    size_t stride=(sizeof(DrawInstance)+fields*4u+7u)&~(size_t)7u;
    return (DrawInstance *)((uint8_t *)(batch + 1) + (size_t)slot * stride);
}
uint32_t *draw_instance_fields(DrawInstance *instance) { return (uint32_t *)(instance + 1); }
uint8_t draw_mul255(unsigned value, unsigned alpha) { return (uint8_t)((value * alpha + 127u) / 255u); }
void draw_rgba(uint8_t *out, uint32_t color, unsigned alpha) {
    out[0] = draw_mul255(color >> 16, alpha);
    out[1] = draw_mul255((color >> 8) & 255u, alpha);
    out[2] = draw_mul255(color & 255u, alpha);
    out[3] = (uint8_t)alpha;
}
void draw_over(uint8_t *dst, const uint8_t *src, unsigned opacity) {
    unsigned alpha = draw_mul255(src[3], opacity);
    for (unsigned c = 0; c < 4; ++c)
        dst[c] = (uint8_t)(draw_mul255(src[c], opacity) + draw_mul255(dst[c], 255u - alpha));
}
const char *draw_blend_name(DrawBlend mode) {
    static const char *const names[]={"over","add","screen","multiply"};
    return mode<DRAW_BLEND_COUNT?names[mode]:"over";
}
void draw_blend(uint8_t *dst,const uint8_t *src,unsigned opacity,DrawBlend mode) {
    if(mode==DRAW_BLEND_OVER){draw_over(dst,src,opacity);return;}
    unsigned alpha=draw_mul255(src[3],opacity);
    for(unsigned c=0;c<3;c++){
        unsigned paint=draw_mul255(src[c],opacity),value;
        if(mode==DRAW_BLEND_ADD){value=dst[c]+paint;if(value>255)value=255;}
        else if(mode==DRAW_BLEND_SCREEN)value=255u-draw_mul255(255u-dst[c],255u-paint);
        else value=draw_mul255(dst[c],255u-alpha+paint);
        dst[c]=(uint8_t)value;
    }
    dst[3]=255;
}
uint32_t draw_colorwheel(float hue) {
    hue = fmodf(hue, 256.0f);
    if (hue < 0) hue += 256.0f;
    unsigned sector = hue < 85 ? 0 : hue < 170 ? 1 : 2;
    unsigned rising = (unsigned)((hue - (float)(sector * 85)) * 3);
    if (rising > 255) rising = 255;
    unsigned falling = 255 - rising;
    return sector == 0 ? (falling << 16 | rising << 8) : sector == 1 ? (falling << 8 | rising) : (rising << 16 | falling);
}
