/* Compiler-output model: explicit continuation state, no C/WASM stack retained.
 * This file is a small target-language fixture, not a source-language frontend. */
#include <stdint.h>
#define EXPORT __attribute__((visibility("default")))
#define PIXELS 300u
#ifndef COMPONENT_SEED_XOR
#define COMPONENT_SEED_XOR 0u
#endif
static uint8_t rgb[PIXELS * 3] __attribute__((aligned(16)));
static uint32_t seed, cursor, x, epoch, sealed, cancelled, endless, init_cursor;

EXPORT uint32_t configure(uint32_t value) {
    seed = value ^ COMPONENT_SEED_XOR; cursor = PIXELS; sealed = cancelled = endless = init_cursor = 0;
    epoch = 1;
    return 1;
}
EXPORT uint32_t initialize_step(uint32_t quota) {
    if (quota > 64) quota = 64;
    while (quota-- && init_cursor < sizeof(rgb)) rgb[init_cursor++] = 0;
    return init_cursor == sizeof(rgb);
}
EXPORT uint32_t buffer_offset(void) { return (uint32_t)(uintptr_t)rgb; }
EXPORT uint32_t buffer_epoch(void) { return epoch; }
EXPORT uint32_t position(void) { return cursor; }
EXPORT uint32_t start(uint32_t frame) {
    if (sealed || cancelled || init_cursor != sizeof(rgb)) return 0;
    cursor = 0; x = seed ^ (frame * UINT32_C(0x9e3779b9));
    return 1;
}
EXPORT uint32_t step(uint32_t quota) {
    if (sealed || cancelled) return UINT32_MAX;
    if (quota > 64) quota = 64;
    while (quota-- && (cursor < PIXELS || endless)) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        x += cursor + UINT32_C(0x6d2b79f5);
        if (!endless) {
            rgb[cursor * 3] = x;
            rgb[cursor * 3 + 1] = x >> 8;
            rgb[cursor * 3 + 2] = x >> 16;
            cursor++;
        }
    }
    return !endless && cursor == PIXELS;
}
EXPORT uint32_t write_checked(uint32_t generation, uint32_t index, uint32_t value) {
    if (generation != epoch || sealed || cancelled || index >= sizeof(rgb)) return 0;
    rgb[index] = value;
    return 1;
}
EXPORT uint32_t publish(void) {
    if (cursor != PIXELS || sealed || cancelled) return 0;
    sealed = 1; epoch++; return 1;
}
EXPORT uint32_t release(void) { if (!sealed) return 0; sealed = 0; epoch++; return 1; }
EXPORT uint32_t cancel(void) { cancelled = 1; epoch++; return 1; }
EXPORT uint32_t runaway(void) { endless = 1; return 1; }
/* An intentionally invalid linear-memory store exercises WAMR AOT bounds checks.
 * The address is a parameter so the C optimizer cannot erase an obvious UB path. */
EXPORT uint32_t raw_store(uint32_t offset, uint32_t value) {
    *(volatile uint8_t *)(uintptr_t)offset = value; return 1;
}
