#ifndef LIGHTVM_PLATFORM_H
#define LIGHTVM_PLATFORM_H
#include "config.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Implemented by the selected native port, never by the engine. Identifiers
// are short, whitespace-free strings so STATUS remains machine-readable.
typedef struct {
    const char *platform;
    const char *board;
    const char *output;
    int32_t pixel_pin; // Legacy placeholder; active wiring is in PixelConfig.
    uint32_t brightness; // Legacy placeholder; active limit is in PixelConfig.
} PlatformInfo;
const PlatformInfo *platform_info(void);
// Stable native device identity and a fresh non-authentication boot nonce.
void platform_identity(char *device, size_t device_capacity, char *boot, size_t boot_capacity);
uint64_t platform_time_us(void); // Monotonic microseconds, including idle time.

typedef struct { const char *name; int32_t gpio; } BoardPin;
typedef struct {
    int32_t pin;
    uint32_t count, brightness;
    char order[4];
} PixelConfig;
const BoardPin *platform_pins(size_t *count);
bool platform_pixels_open(const PixelConfig *config, void **driver);
void platform_pixels_close(void *driver);
// Consume/copy the complete packed RGB frame before returning.
bool platform_pixels_show(void *driver, const uint8_t *rgb, size_t count);

// Native bounded block storage. Writes require erased bytes; sync is durable.
// All ports expose 4096-byte erase units and 256-byte program units.
bool platform_storage_available(void);
bool platform_storage_read(uint32_t offset, void *bytes, size_t size);
bool platform_storage_write(uint32_t offset, const void *bytes, size_t size);
bool platform_storage_erase(uint32_t offset, size_t size);
bool platform_storage_sync(void);

#if LIGHTVM_BOOT_SEQUENCE
// Independent 8 KiB boot journal; same erase/program/durability contract.
bool platform_boot_storage_available(void);
bool platform_boot_storage_read(uint32_t offset, void *bytes, size_t size);
bool platform_boot_storage_write(uint32_t offset, const void *bytes, size_t size);
bool platform_boot_storage_erase(uint32_t offset, size_t size);
bool platform_boot_storage_sync(void);
#endif

// Compatible, aligned C allocation semantics. All wasm3 allocations use these
// hooks through the shared budget accounting. A port may choose a memory pool.
void *platform_calloc(size_t count, size_t size);
void *platform_realloc(void *memory, size_t size);
void platform_free(void *memory);
#endif
