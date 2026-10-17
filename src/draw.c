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
uint32_t draw_opacity(unsigned alpha) {
    return (alpha * DRAW_OPACITY_ONE + 127u) / 255u;
}
/* Both the opaque bulk path and premultiplied alpha path use this sum. Keep
 * the complementary terms together: independently rounding them loses a bit. */
static inline uint8_t composite_channel(unsigned lower, unsigned upper, uint32_t opacity, uint32_t inverse, uint32_t denominator) {
    /* Unsigned differences deliberately wrap and cancel in this sum. For
       complementary opaque weights, opacity+inverse folds to 65536: one
       multiply per channel, with exactly the same rounding as the full sum. */
    uint32_t sum=lower*(opacity+inverse)+((uint32_t)upper-lower)*opacity;
    return (uint8_t)((sum+denominator/2u)/denominator);
}
static inline void over_channels(uint8_t *dst, const uint8_t *src, unsigned channels, uint32_t opacity) {
    if(!opacity || !src[channels])return;
    if(src[channels]==255){
        if(opacity==DRAW_OPACITY_ONE){if(dst!=src)memcpy(dst,src,channels+1);return;}
        uint32_t inverse=DRAW_OPACITY_ONE-opacity;
        for(unsigned c=0;c<=channels;c++)dst[c]=composite_channel(dst[c],src[c],opacity,inverse,DRAW_OPACITY_ONE);
        return;
    }
    /* Carry alpha's /255 through to the final rounding. Valid premultiplied
       channels bound the sum, including rounding, below UINT32_MAX. */
    uint32_t inverse=255u*DRAW_OPACITY_ONE-src[channels]*opacity;
    for(unsigned c=0;c<=channels;c++)dst[c]=composite_channel(dst[c],src[c],opacity*255u,inverse,255u*DRAW_OPACITY_ONE);
}
void draw_over_channels(uint8_t *dst, const uint8_t *src, unsigned channels, uint32_t opacity) {
    over_channels(dst,src,channels,opacity);
}
void draw_over(uint8_t *dst, const uint8_t *src, unsigned opacity) {
    over_channels(dst,src,3,draw_opacity(opacity));
}
const char *draw_blend_name(DrawBlend mode) {
    static const char *const names[]={"over","add","screen","multiply"};
    return mode<DRAW_BLEND_COUNT?names[mode]:"over";
}
void draw_blend(uint8_t *dst,const uint8_t *src,unsigned opacity,DrawBlend mode) {
    draw_composite(dst,4,dst,4,src,4,1,draw_opacity(opacity),mode);
}
static inline void blend_pixel(uint8_t *dst,const uint8_t *src,uint32_t opacity,DrawBlend mode) {
    if(mode==DRAW_BLEND_OVER){over_channels(dst,src,3,opacity);return;}
    unsigned alpha=composite_channel(0,src[3],opacity,0,DRAW_OPACITY_ONE);
    for(unsigned c=0;c<3;c++){
        unsigned paint=composite_channel(0,src[c],opacity,0,DRAW_OPACITY_ONE),value;
        if(mode==DRAW_BLEND_ADD){value=dst[c]+paint;if(value>255)value=255;}
        else if(mode==DRAW_BLEND_SCREEN)value=255u-draw_mul255(255u-dst[c],255u-paint);
        else value=draw_mul255(dst[c],255u-alpha+paint);
        dst[c]=(uint8_t)value;
    }
    dst[3]=255;
}
static inline void rgb_span(uint8_t *out,unsigned stride,const uint8_t *lower,const uint8_t *upper,unsigned count,uint32_t opacity) {
    uint32_t inverse=DRAW_OPACITY_ONE-opacity;
    const uint8_t *end=lower+(size_t)count*3;
    while(lower!=end){
        out[0]=composite_channel(lower[0],upper[0],opacity,inverse,DRAW_OPACITY_ONE);
        out[1]=composite_channel(lower[1],upper[1],opacity,inverse,DRAW_OPACITY_ONE);
        out[2]=composite_channel(lower[2],upper[2],opacity,inverse,DRAW_OPACITY_ONE);
        if(stride==4)out[3]=255;
        lower+=3;upper+=3;out+=stride;
    }
}
void draw_composite_rgb(uint8_t *out,unsigned out_stride,const uint8_t *lower,const uint8_t *upper,unsigned count,uint32_t opacity) {
    const uint8_t *copy=opacity==DRAW_OPACITY_ONE?upper:lower;
    if(!opacity || opacity==DRAW_OPACITY_ONE){
        if(out_stride==3){if(copy){if(out!=copy)memcpy(out,copy,(size_t)count*3);}else memset(out,0,(size_t)count*3);return;}
        for(unsigned i=0;i<count;i++,out+=out_stride){
            if(copy){memcpy(out,copy+(size_t)i*3,3);}else memset(out,0,3);
            out[3]=255;
        }
        return;
    }
    /* Keep the hot loop in a small function, with fixed output strides.
       Generic blend modes must not spill its weights to the Pico stack. */
    if(lower&&upper){
        if(out_stride==3)rgb_span(out,3,lower,upper,count,opacity);
        else rgb_span(out,4,lower,upper,count,opacity);
        return;
    }
    uint32_t inverse=DRAW_OPACITY_ONE-opacity;
    for(unsigned i=0;i<count;i++,out+=out_stride){
        for(unsigned c=0;c<3;c++)out[c]=composite_channel(lower?lower[(size_t)i*3+c]:0,upper?upper[(size_t)i*3+c]:0,opacity,inverse,DRAW_OPACITY_ONE);
        if(out_stride==4)out[3]=255;
    }
}
void draw_composite(uint8_t *out,unsigned out_stride,
                    const uint8_t *lower,unsigned lower_stride,
                    const uint8_t *upper,unsigned upper_stride,
                    unsigned count,uint32_t opacity,DrawBlend mode) {
    if(lower_stride==3 && upper_stride==3 && mode==DRAW_BLEND_OVER){
        draw_composite_rgb(out,out_stride,lower,upper,count,opacity);return;
    }
    for(unsigned i=0;i<count;i++,out+=out_stride){
        const uint8_t *a=lower?lower+(size_t)i*lower_stride:NULL,*b=upper?upper+(size_t)i*upper_stride:NULL;
        uint8_t dst[4]={0,0,0,lower_stride==3?255:0},src[4]={0,0,0,upper_stride==3?255:0};
        if(a){dst[0]=a[0];dst[1]=a[1];dst[2]=a[2];if(lower_stride==4)dst[3]=a[3];}
        if(b){src[0]=b[0];src[1]=b[1];src[2]=b[2];if(upper_stride==4)src[3]=b[3];}
        blend_pixel(dst,src,opacity,mode);
        out[0]=dst[0];out[1]=dst[1];out[2]=dst[2];if(out_stride==4)out[3]=dst[3];
    }
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
