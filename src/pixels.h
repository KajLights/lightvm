#ifndef LIGHTVM_PIXELS_H
#define LIGHTVM_PIXELS_H

#include "outputs.h"
// Engine-owned packed buffer; physical driver ownership is separate.
typedef struct {
    uint8_t *rgb;
    uint32_t count;
    PixelConfig config;
    const OutputType *type;
    void *driver;
} PixelBuffer;
bool pixels_init(PixelBuffer *pixels, const PixelConfig *config, const OutputType *type);
void pixels_destroy(PixelBuffer *pixels);
void pixels_clear(PixelBuffer *pixels);
bool pixels_set_rgb(PixelBuffer *pixels, uint32_t index, uint32_t red, uint32_t green, uint32_t blue);
bool pixels_set_hsv(PixelBuffer *pixels, uint32_t index, uint32_t hue, uint32_t saturation, uint32_t value);
bool pixels_show(PixelBuffer *pixels);

#endif
