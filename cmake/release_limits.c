/* Compile the actual resource macros into an unlinked packaging object. */
#include "config.h"
#include <stdint.h>
__attribute__((used,section(".kajlights_limits")))
const uint32_t lightvm_release_limits[] = {
    CONFIG_LIMIT, MODULE_LIMIT, PIXEL_CAPACITY, STORAGE_BYTES,
    VM_HEAP_LIMIT, LIGHTVM_AUTOMATIC_HEAP, LIGHTVM_NATIVE_HEADROOM_BYTES
};
