#include "vm_allocator.h"
#include "platform.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t capacity=200000, observed_allocated;
static bool fail;
size_t platform_vm_heap_limit(size_t allocated) {
    observed_allocated=allocated;
    return capacity;
}
void *platform_realloc(void *p,size_t size) {return fail?NULL:realloc(p,size);}
void platform_free(void *p) {free(p);}
int main(void) {
    size_t limit=vm_heap_limit();
#if LIGHTVM_AUTOMATIC_HEAP
    assert(limit==capacity);
#else
    assert(limit==LIGHTVM_VM_HEAP_BYTES);
#endif
    void *p=demo_alloc(32);assert(p);memset(p,0x5a,32);
    size_t physical=demo_allocation_bytes(p);
    assert(vm_heap_used()==physical);
    assert(vm_heap_charge(100));
    (void)vm_heap_limit();assert(observed_allocated==physical);
    assert(vm_heap_used()==physical+100);
    // Exhaust accounting without requiring a huge host allocation.
    size_t charge=limit-vm_heap_used();assert(vm_heap_charge(charge));
    assert(!demo_alloc(0));assert(!vm_heap_charge(1));
    assert(!demo_realloc(p,33));assert(((unsigned char *)p)[0]==0x5a);
    assert(vm_heap_used()==limit);
    vm_heap_uncharge(charge);
    fail=true;assert(!demo_realloc(p,1000));assert(!demo_alloc(1));fail=false;
    assert(vm_heap_used()==physical+100);assert(((unsigned char *)p)[31]==0x5a);
    assert(!demo_alloc(SIZE_MAX));assert(!demo_calloc(SIZE_MAX,2));
    assert(!demo_realloc(p,SIZE_MAX));
    // A native allocation can reduce the runtime limit below existing usage.
    // Growth/charges must fail without unsigned wraparound; shrink/free work.
    capacity=1;
    assert(!demo_alloc(1));assert(!vm_heap_charge(1));assert(!demo_realloc(p,33));
    p=demo_realloc(p,16);assert(p);assert(((unsigned char *)p)[15]==0x5a);
    vm_heap_uncharge(100);demo_free(p);assert(vm_heap_used()==0);
    (void)vm_heap_limit();assert(observed_allocated==0);
    capacity=200000;assert(vm_heap_peak()==limit);
    p=demo_realloc(NULL,24);assert(p);
    for(size_t n=0;n<24;n++)assert(((unsigned char *)p)[n]==0);
    demo_free(p);assert(vm_heap_used()==0);
    return 0;
}
