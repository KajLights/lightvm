#ifndef POC_PLATFORM_H
#define POC_PLATFORM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef POC_PIXELS
#define POC_PIXELS 200
#endif
typedef struct { uint32_t offset, size; const void *native; } PocFlash;
typedef struct {
    const char *name, *order;
    unsigned gpio, brightness, cpu_mhz;
} PocPlatform;
extern const PocFlash *poc_slots[2];
extern PocPlatform poc_platform;
void poc_init(void);
uint64_t poc_now(void);
void poc_idle(void);
void poc_yield(void);
void poc_restart(void);
int poc_read(uint8_t *bytes, size_t size);
int poc_write(const char *bytes, size_t size);
void poc_prepare_pixels(uint8_t *pixels, size_t size);
bool poc_output(const uint8_t *pixels, size_t size);
bool poc_output_done(void);
uint32_t poc_crc32(uint32_t crc, const void *bytes, size_t size);
bool poc_flash_read(const PocFlash *slot, uint32_t offset, void *bytes, size_t size);
bool poc_flash_write(const PocFlash *slot, uint32_t offset, const void *bytes, size_t size);
bool poc_flash_erase(const PocFlash *slot, uint32_t offset, size_t size);
const uint8_t *poc_flash_map(const PocFlash *slot, uint32_t offset, size_t size, uintptr_t *handle);
void poc_flash_unmap(uintptr_t handle);
size_t poc_heap_free(void);
size_t poc_heap_min(void);
size_t poc_heap_total(void);
void poc_run(void);
#endif
