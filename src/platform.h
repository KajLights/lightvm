#ifndef LIGHTVM_PLATFORM_H
#define LIGHTVM_PLATFORM_H
#include <stddef.h>
#include <stdint.h>

// Implemented by the selected native port, never by the engine. Identifiers
// are short, whitespace-free strings so STATUS remains machine-readable.
typedef struct {
    const char *platform;
    const char *board;
    const char *output;
    int32_t pixel_pin; // -1 when a GPIO pin is not applicable.
    uint32_t brightness;
} PlatformInfo;
const PlatformInfo *platform_info(void);
uint64_t platform_time_us(void); // Monotonic microseconds, including idle time.

// Packed logical RGB, count pixels. Apply physical brightness/wire order here.
// Consume/copy bytes before returning; the engine reuses its buffer afterward.
void platform_show_rgb(const uint8_t *rgb, size_t count);

// Compatible, aligned C allocation semantics. All wasm3 allocations use these
// hooks through the shared budget accounting. A port may choose a memory pool.
void *platform_calloc(size_t count, size_t size);
void *platform_realloc(void *memory, size_t size);
void platform_free(void *memory);
#endif
