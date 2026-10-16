#ifndef LIGHTVM_PORTABLE_DRAW_H
#define LIGHTVM_PORTABLE_DRAW_H
#include "portable.h"
bool pbc_drawing_valid(Portable *, const PbcCall *, unsigned rights);
bool pbc_drawing_call(Portable *, const PbcCall *, const PbcValue *, PbcValue *);
bool pbc_image_read(Portable *, PbcResource *, unsigned index, uint8_t pixel[5]);
#endif
