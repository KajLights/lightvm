#include "vm_allocator.h"
#include "platform.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef union { max_align_t alignment; size_t size; } Allocation;
static size_t used, peak, window_peak;
size_t vm_heap_window_peak(void) { return window_peak; }
void vm_heap_window_reset(void) { window_peak=used; }
static void record_peak(void) { if (used>peak) peak=used; if (used>window_peak) window_peak=used; }
size_t vm_heap_used(void) { return used; }
size_t vm_heap_peak(void) { return peak; }
bool vm_heap_charge(size_t bytes) {
    if (bytes>VM_HEAP_LIMIT-used) return false;
    used+=bytes; record_peak(); return true;
}
void vm_heap_uncharge(size_t bytes) { used-=bytes; }
void *demo_calloc(size_t count, size_t size) {
    if (count && size>SIZE_MAX/count) return NULL;
    size*=count;
    if (size>VM_HEAP_LIMIT-used || sizeof(Allocation)>VM_HEAP_LIMIT-used-size) return NULL;
    Allocation *a=platform_calloc(1,sizeof(*a)+size);
    if (!a) return NULL;
    a->size=sizeof(*a)+size; used+=a->size;
    record_peak();
    return a+1;
}
void demo_free(void *p) {
    if (!p) return;
    Allocation *a=(Allocation *)p-1;
    used-=a->size; platform_free(a);
}
void *demo_realloc(void *p, size_t size) {
    if (!p) return demo_calloc(1,size);
    Allocation *a=(Allocation *)p-1;
    size_t other=used-a->size;
    if (size>VM_HEAP_LIMIT-other || sizeof(*a)>VM_HEAP_LIMIT-other-size) return NULL;
    Allocation *next=platform_realloc(a,sizeof(*a)+size);
    if (!next) return NULL;
    next->size=sizeof(*next)+size; used=other+next->size;
    record_peak();
    return next+1;
}
