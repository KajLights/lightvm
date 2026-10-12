#ifndef DEMO_VM_ALLOCATOR_H
#define DEMO_VM_ALLOCATOR_H
#include <stddef.h>
#include "config.h"
size_t vm_heap_used(void);
size_t vm_heap_peak(void);
#endif
