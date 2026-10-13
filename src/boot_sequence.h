#ifndef LIGHTVM_BOOT_SEQUENCE_H
#define LIGHTVM_BOOT_SEQUENCE_H
#include "config.h"
#include <stdbool.h>
#include <stdint.h>

#if LIGHTVM_BOOT_SEQUENCE
#define BOOT_SEQUENCE_THRESHOLD 3u
#define BOOT_SEQUENCE_WINDOW_US 5000000u
typedef struct {
    uint64_t started_us, sequence;
    uint32_t sector, next_page, count;
    bool triggered, reset_pending;
    const char *error;
} BootSequence;

// Call once per device boot, before applying saved startup policy.
bool boot_sequence_init(BootSequence *state);
// Clears the durable counter once; the current boot's trigger remains latched.
void boot_sequence_tick(BootSequence *state, uint64_t now);
#endif
#endif
