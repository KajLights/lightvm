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
} PlatformInfo;
const PlatformInfo *platform_info(void);
// Stable native controller identity and a fresh non-authentication boot nonce.
void platform_identity(char *controller, size_t controller_capacity, char *boot, size_t boot_capacity);
uint64_t platform_time_us(void); // Monotonic microseconds, including idle time.

typedef struct { const char *name; int32_t gpio; } BoardPin;
typedef struct {
    int32_t pin;
    uint32_t count, brightness;
    char order[4];
    int32_t clock_pin, enable_pin;
    uint32_t frequency;
} PixelConfig;
const BoardPin *platform_pins(size_t *count);
bool platform_pixels_open(const PixelConfig *config, void **driver);
void platform_pixels_close(void *driver);
// Synchronous management convenience. Autonomous output uses submit/poll below.
// submit borrows immutable RGB until poll returns 1 (complete) or -1 (fault).
// cancel synchronously releases all references before returning.
bool platform_pixels_submit(void *,const uint8_t *,size_t);
int platform_pixels_poll(void *);
void platform_pixels_cancel(void *);
bool platform_pixels_show(void *driver, const uint8_t *rgb, size_t count);

// Read-only native board measurements, sampled by the engine owner. Unsupported
// measurements must not return nominal/configured values as physical readings.
typedef enum {
    PLATFORM_METRIC_TEMPERATURE, PLATFORM_METRIC_SUPPLY_VOLTAGE,
    PLATFORM_METRIC_CPU_FREQUENCY, PLATFORM_METRIC_HEAP_FREE,
    PLATFORM_METRIC_HEAP_MIN, PLATFORM_METRIC_WIFI_RSSI, PLATFORM_METRIC_COUNT
} PlatformMetric;
bool platform_metric_supported(PlatformMetric);
bool platform_metric_read(PlatformMetric,double *);

// Optional native peripherals. Unsupported ports reject configuration before apply.
bool platform_apa102_supported(void);
bool platform_apa102_open(const PixelConfig *, void **);
void platform_apa102_close(void *);
bool platform_apa102_submit(void *, const uint8_t *, size_t);
int platform_apa102_poll(void *);
void platform_apa102_cancel(void *);
bool platform_apa102_show(void *, const uint8_t *, size_t);
bool platform_i2c_valid(int32_t sda, int32_t scl);
bool platform_i2c_open(int32_t sda, int32_t scl, void **);
void platform_i2c_close(void *);
// One bounded register transaction, including repeated start, at 400 kHz.
bool platform_i2c_transfer(void *, uint8_t address, const uint8_t *, size_t, uint8_t *, size_t);

// Native bounded block storage. Writes require erased bytes; sync is durable.
// All ports expose 4096-byte erase units and 256-byte program units.
bool platform_storage_available(void);
uint32_t platform_storage_capacity(void);
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

// Compatible, aligned C allocation semantics. All interpreter allocations use these
// hooks through the shared budget accounting. A port may choose a memory pool.
void *platform_calloc(size_t count, size_t size);
void *platform_realloc(void *memory, size_t size);
void platform_free(void *memory);
#endif
