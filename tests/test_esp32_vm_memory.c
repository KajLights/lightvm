#include "platform.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef union {max_align_t align;struct {size_t size;unsigned pool;} info;} Block;
static size_t pools[2];
size_t heap_caps_get_free_size(uint32_t caps){return pools[(caps&MALLOC_CAP_SPIRAM)?1:0];}
void *heap_caps_malloc(size_t size,uint32_t caps){
    unsigned pool=(caps&MALLOC_CAP_SPIRAM)?1:0;
    if(size+sizeof(Block)>pools[pool])return NULL;
    Block *b=malloc(sizeof(*b)+size);assert(b);
    b->info.size=size;b->info.pool=pool;pools[pool]-=size+sizeof(*b);return b+1;
}
void heap_caps_free(void *p){
    if(!p)return;
    Block *b=(Block *)p-1;pools[b->info.pool]+=b->info.size+sizeof(*b);free(b);
}
size_t heap_caps_get_allocated_size(void *p){return ((Block *)p-1)->info.size;}
int main(void){
    // Independent expected S3 reservation: four RMT channels plus 16 KiB native.
    const size_t reserve=16384+4*(1024*3+8192);
    pools[0]=reserve+10000;pools[1]=20000;
    assert(platform_vm_heap_limit(123)==30123);
    void *p=platform_vm_realloc(NULL,1000);assert(p);memset(p,0x6a,1000);
    assert(pools[0]==reserve+10000); // PSRAM is preferred over internal RAM.
    size_t external_after=pools[1];pools[1]=0;
    void *next=platform_vm_realloc(p,2000);assert(next);
    assert(((unsigned char *)next)[999]==0x6a);p=next;
    // Emulate PSRAM unavailable and internal memory consumed by a native task.
    pools[1]=0;pools[0]=reserve;
    assert(platform_vm_heap_limit(2000)==2000);
    assert(!platform_vm_realloc(p,3000));assert(((unsigned char *)p)[999]==0x6a);
    pools[0]=reserve+2000; // Allocator metadata must not eat the reserve either.
    assert(!platform_vm_realloc(NULL,2000));assert(pools[0]==reserve+2000);
    pools[0]=reserve-100;
    assert(platform_vm_heap_limit(2000)==2000); // No unsigned wraparound.
    heap_caps_free(p);
    pools[1]=external_after; // Enough PSRAM still works with low internal RAM.
    pools[0]=reserve-100;p=platform_vm_realloc(NULL,128);assert(p);heap_caps_free(p);
    return 0;
}
