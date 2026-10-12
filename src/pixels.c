#include "pixels.h"
#include "platform.h"
#include <string.h>

bool pixels_init(PixelBuffer *p, const PixelConfig *config, const DeviceType *type) {
    memset(p, 0, sizeof(*p));
    p->rgb = platform_calloc(config->count, 3);
    if (!p->rgb) return false;
    p->count = config->count; p->config = *config; p->type = type;
    if (!type->open(config, &p->driver) || !pixels_show(p)) {
        pixels_destroy(p); return false;
    }
    return true;
}
void pixels_destroy(PixelBuffer *p) {
    if (!p) return;
    if (p->driver) { pixels_clear(p); (void)pixels_show(p); p->type->close(p->driver); }
    platform_free(p->rgb); memset(p, 0, sizeof(*p));
}
void pixels_clear(PixelBuffer *p) {
    if (p && p->rgb) memset(p->rgb, 0, (size_t)p->count * 3);
}

bool pixels_set_rgb(PixelBuffer *p, uint32_t index, uint32_t red, uint32_t green, uint32_t blue) {
    if (!p || index >= p->count || red > 255 || green > 255 || blue > 255)
        return false;
    p->rgb[index * 3] = (uint8_t)red;
    p->rgb[index * 3 + 1] = (uint8_t)green;
    p->rgb[index * 3 + 2] = (uint8_t)blue;
    return true;
}

bool pixels_set_hsv(PixelBuffer *p, uint32_t index, uint32_t hue, uint32_t saturation, uint32_t value) {
    if (!p || index >= p->count || saturation > 255 || value > 255)
        return false;
    uint32_t scaled = (hue & 65535u) * 6u;
    uint32_t sector = scaled >> 16;
    uint32_t fraction = scaled & 65535u;
    const uint32_t denominator = 255u * 65535u;
    // Multiply before division and round once to avoid 1-LSB primary-color bias.
    uint32_t minimum = (value * (255u - saturation) + 127u) / 255u;
    uint32_t q = value - (value * saturation * fraction + denominator / 2u) / denominator;
    uint32_t t = value - (value * saturation * (65535u - fraction) + denominator / 2u) / denominator;
    switch (sector) {
    case 0: return pixels_set_rgb(p, index, value, t, minimum);
    case 1: return pixels_set_rgb(p, index, q, value, minimum);
    case 2: return pixels_set_rgb(p, index, minimum, value, t);
    case 3: return pixels_set_rgb(p, index, minimum, q, value);
    case 4: return pixels_set_rgb(p, index, t, minimum, value);
    default: return pixels_set_rgb(p, index, value, minimum, q);
    }
}

bool pixels_show(PixelBuffer *p) {
    return !p || !p->count || (p->driver && p->type->show(p->driver, p->rgb, p->count));
}
