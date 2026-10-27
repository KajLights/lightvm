#include "vm_allocator.h"
#include "platform.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if LIGHTVM_CAPABILITY_HEAP
#define vm_realloc platform_vm_realloc
#else
#define vm_realloc platform_realloc
#endif

typedef union { max_align_t alignment; size_t size; } Allocation;
static size_t used, peak, allocated;
size_t vm_heap_limit(void) {
#if LIGHTVM_PLATFORM_HEAP_BUDGET
    size_t limit=platform_vm_heap_limit(allocated);
#if !LIGHTVM_AUTOMATIC_HEAP
    if(limit>VM_HEAP_LIMIT)limit=VM_HEAP_LIMIT;
#endif
    return limit;
#else
    return VM_HEAP_LIMIT;
#endif
}
static bool admits(size_t retained,size_t bytes) {
    size_t limit=vm_heap_limit();
    return retained<=limit && bytes<=limit-retained;
}
size_t demo_allocation_bytes(const void *p){return p?((const Allocation *)p-1)->size:0;}
size_t vm_heap_used(void) { return used; }
size_t vm_heap_peak(void) { return peak; }
bool vm_heap_charge(size_t bytes) {
    if (!admits(used,bytes)) return false;
    used+=bytes; if (used>peak) peak=used; return true;
}
void vm_heap_uncharge(size_t bytes) { used-=bytes; }
void *demo_alloc(size_t size) {
    if (size>SIZE_MAX-sizeof(Allocation) || !admits(used,sizeof(Allocation)+size)) return NULL;
    Allocation *a=vm_realloc(NULL,sizeof(*a)+size);
    if (!a) return NULL;
    a->size=sizeof(*a)+size; used+=a->size; allocated+=a->size;
    if (used>peak) peak=used;
    return a+1;
}
void *demo_calloc(size_t count, size_t size) {
    if (count && size>SIZE_MAX/count) return NULL;
    size*=count;
    void *p=demo_alloc(size);if(p)memset(p,0,size);return p;
}
void demo_free(void *p) {
    if (!p) return;
    Allocation *a=(Allocation *)p-1;
    used-=a->size; allocated-=a->size; platform_free(a);
}
void *demo_realloc(void *p, size_t size) {
    if (!p) return demo_calloc(1,size);
    Allocation *a=(Allocation *)p-1;
    size_t other=used-a->size;
    if (size>SIZE_MAX-sizeof(*a)) return NULL;
    size_t old=a->size, total=sizeof(*a)+size;
    // A native service can lower capacity below current use. Still permit shrink.
    if (total>old && !admits(other,total)) return NULL;
    Allocation *next=vm_realloc(a,sizeof(*a)+size);
    if (!next) return NULL;
    next->size=total; used=other+total; allocated=allocated-old+total;
    if (used>peak) peak=used;
    return next+1;
}
