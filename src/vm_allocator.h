#ifndef DEMO_VM_ALLOCATOR_H
#define DEMO_VM_ALLOCATOR_H
#include <stddef.h>
#define VM_HEAP_LIMIT (128u*1024u)
size_t vm_heap_used(void);
size_t vm_heap_peak(void);
#endif
