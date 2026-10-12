#ifndef LIGHTVM_PIXELS_H
#define LIGHTVM_PIXELS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"

#define PIXEL_BYTES (PIXEL_COUNT * 3u)

void pixels_init(void);
void pixels_clear(void);
bool pixels_set_rgb(uint32_t index, uint32_t red, uint32_t green, uint32_t blue);
bool pixels_set_hsv(uint32_t index, uint32_t hue, uint32_t saturation, uint32_t value);
void pixels_rainbow(uint32_t phase);
void pixels_show(void);
const uint8_t *pixels_rgb(void);

#endif
