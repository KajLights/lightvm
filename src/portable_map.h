#ifndef LIGHTVM_PORTABLE_MAP_H
#define LIGHTVM_PORTABLE_MAP_H
#include "portable.h"
/* Verified forward control flow with one final store. Plans are admitted once,
 * charged to the VM heap, immutable during rendering and freed with their VM. */
bool pbc_map_valid(Portable *,unsigned first,unsigned end,unsigned dst,unsigned index,unsigned bound);
bool pbc_map_run(Portable *,unsigned first,unsigned end,unsigned dst,unsigned index,unsigned bound);
int pbc_map_prepare(Portable *,unsigned *pc,unsigned *index);
void pbc_map_destroy(Portable *);
#endif
