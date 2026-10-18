#ifndef LIGHTVM_APA102_H
#define LIGHTVM_APA102_H
#include "platform.h"
size_t apa102_frame_size(size_t count);
bool apa102_encode(const PixelConfig *, const uint8_t *, size_t, uint8_t *, size_t);
#endif
