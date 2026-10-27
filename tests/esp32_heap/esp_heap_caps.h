#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_INTERNAL 1u
#define MALLOC_CAP_SPIRAM 2u
#define MALLOC_CAP_8BIT 4u
size_t heap_caps_get_free_size(uint32_t caps);
void *heap_caps_malloc(size_t size,uint32_t caps);
void heap_caps_free(void *p);
size_t heap_caps_get_allocated_size(void *p);
