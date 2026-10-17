#ifndef LIGHTVM_DRAW_H
#define LIGHTVM_DRAW_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { DRAW_RGBA = 12,
       DRAW_IMAGE_RGB = 14, DRAW_IMAGE_RGBA, DRAW_IMAGE_RGBW, DRAW_IMAGE_RGBWA,
       DRAW_BATCH, DRAW_STYLE };
typedef struct { float translate, scale, origin, alpha, brightness; uint32_t tint; float hue, saturation; } DrawStyle;
/* Generic instance storage. No placement, lifetime, fade or expiry policy. */
typedef struct { uint64_t born, previous; uint32_t active; } DrawInstance;
typedef struct { uint32_t active, reserved; } DrawBatch;
typedef enum { DRAW_BLEND_OVER, DRAW_BLEND_ADD, DRAW_BLEND_SCREEN, DRAW_BLEND_MULTIPLY, DRAW_BLEND_COUNT } DrawBlend;
#define DRAW_OPACITY_ONE 65536u
const char *draw_blend_name(DrawBlend);
/* Common compositing contract: encoded RGB, premultiplied alpha, Q16 opacity
 * in 0..65536 and one final rounding for source-over. Strides are 3 (opaque
 * RGB) or 4 (premultiplied RGBA). A NULL input is black (RGB) or clear (RGBA).
 * Inputs may alias output only at the same start and stride. Non-over modes expect
 * an opaque lower surface, as in the scene blend contract. */
uint32_t draw_opacity(unsigned alpha);
static inline uint32_t draw_opacity_float(float opacity) {
    return (uint32_t)(opacity * (float)DRAW_OPACITY_ONE + 0.5f);
}
/* Opaque-input specialization of the same compositor; output is RGB/RGBA. */
void draw_composite_rgb(uint8_t *out, unsigned out_stride, const uint8_t *lower,
                        const uint8_t *upper, unsigned count, uint32_t opacity);
void draw_composite(uint8_t *out, unsigned out_stride,
                    const uint8_t *lower, unsigned lower_stride,
                    const uint8_t *upper, unsigned upper_stride,
                    unsigned count, uint32_t opacity, DrawBlend);
/* Premultiplied RGBA / RGBWA pixels with alpha after 3 / 4 color channels. */
void draw_over_channels(uint8_t *dst, const uint8_t *src, unsigned channels, uint32_t opacity);
/* Scene blending over an opaque destination; input is premultiplied RGBA. */
void draw_blend(uint8_t *dst, const uint8_t *src, unsigned opacity, DrawBlend);
size_t draw_batch_bytes(unsigned capacity, unsigned fields);
DrawInstance *draw_instance(DrawBatch *, unsigned slot, unsigned fields);
uint32_t *draw_instance_fields(DrawInstance *);
unsigned draw_channels(unsigned format);
int draw_channel(unsigned format, unsigned channel);
unsigned draw_stride(unsigned format);
uint8_t draw_mul255(unsigned value, unsigned alpha);
void draw_rgba(uint8_t *out, uint32_t color, unsigned alpha);
void draw_over(uint8_t *dst, const uint8_t *src, unsigned opacity);
uint32_t draw_colorwheel(float hue);
#endif
