#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include "config.h"
#include <stdlib.h>
#include <time.h>

static const PlatformInfo info = {"host", LIGHTVM_BOARD_NAME, "memory", -1, 255};
const PlatformInfo *platform_info(void) { return &info; }
uint64_t platform_time_us(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}
void platform_show_rgb(const uint8_t *rgb, size_t count) {
    // The host uses the engine's logical buffer for FRAME snapshots.
    (void)rgb; (void)count;
}
void *platform_calloc(size_t count, size_t size) { return calloc(count, size); }
void *platform_realloc(void *memory, size_t size) { return realloc(memory, size); }
void platform_free(void *memory) { free(memory); }
