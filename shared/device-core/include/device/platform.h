#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* One application supplies these SDK-independent hooks. */
uint64_t platform_time_us(void);
void *platform_calloc(size_t,size_t);
void *platform_realloc(void *,size_t);
void platform_free(void *);
void device_checkpoint(void);
bool platform_storage_available(void);
uint32_t platform_storage_capacity(void);
bool platform_storage_read(uint32_t,void *,size_t);
bool platform_storage_write(uint32_t,const void *,size_t);
bool platform_storage_erase(uint32_t,size_t);
bool platform_storage_sync(void);
