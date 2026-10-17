#ifndef LIGHTVM_PORTABLE_MAP_H
#define LIGHTVM_PORTABLE_MAP_H
#include "portable.h"
/* A straight-line, pure, typed pixel loop. At most 32 operations x 32 pixels. */
bool pbc_map_valid(Portable *,unsigned first,unsigned end,unsigned dst,unsigned index,unsigned bound);
bool pbc_map_run(Portable *,unsigned first,unsigned end,unsigned dst,unsigned index,unsigned bound);
#endif
