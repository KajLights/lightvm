#ifndef DEMO_VM_ALLOCATOR_H
#define DEMO_VM_ALLOCATOR_H
#include <stddef.h>
#include <stdbool.h>
#include "config.h"
size_t vm_heap_used(void);
size_t demo_allocation_bytes(const void *);
size_t vm_heap_peak(void);
void *demo_alloc(size_t bytes);
void *demo_calloc(size_t count, size_t size);
void demo_free(void *memory);
bool vm_heap_charge(size_t bytes);
void vm_heap_uncharge(size_t bytes);
#endif
