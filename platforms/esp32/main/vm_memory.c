#include "platform.h"
#include "esp_heap_caps.h"
#include "soc/soc_caps.h"
#include <string.h>

static size_t vm_native_reserve(void) {
    // Wire buffers plus RMT encoder/channel bookkeeping and up to 1024 DMA
    // symbols per channel. Retain this even while no output is configured.
    return LIGHTVM_NATIVE_HEADROOM_BYTES +
        SOC_RMT_GROUPS*SOC_RMT_TX_CANDIDATES_PER_GROUP*((size_t)PIXEL_CAPACITY*3+8192);
}
size_t platform_vm_heap_limit(size_t allocated) {
    size_t internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    size_t external=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    size_t reserve=vm_native_reserve();
    return allocated+external+(internal>reserve?internal-reserve:0);
}
void *platform_vm_realloc(void *memory,size_t size) {
    // Prefer PSRAM when enabled; never count it as internal operating reserve.
    // Allocate/copy/free preserves the old block on failure, including reserve
    // failures. Growth can require both blocks temporarily, just like realloc.
    void *next=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!next){
        size_t free_bytes=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        size_t reserve=vm_native_reserve();
        if(free_bytes<=reserve || size>free_bytes-reserve)return NULL;
        next=heap_caps_malloc(size,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        if(next && heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<reserve){
            heap_caps_free(next);return NULL;
        }
    }
    if(!next)return NULL;
    if(memory){
        size_t old=heap_caps_get_allocated_size(memory);
        memcpy(next,memory,old<size?old:size);heap_caps_free(memory);
    }
    return next;
}
