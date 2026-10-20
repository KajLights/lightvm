#ifndef LIGHTVM_PIXELS_H
#define LIGHTVM_PIXELS_H

#include "outputs.h"
#include "inputs.h"
// Engine-owned packed buffer; physical driver ownership is separate.
typedef struct {
    uint8_t *rgb;
    uint8_t *dimmed;
    LightControls light;
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
bool pixels_show_frame(PixelBuffer *,const uint8_t *,size_t);
bool pixels_submit(PixelBuffer *,const uint8_t *,size_t);

#endif
