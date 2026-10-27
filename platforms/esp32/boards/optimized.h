#ifndef LIGHTVM_ESP32_OPTIMIZED_BOARD_CONFIG_H
#define LIGHTVM_ESP32_OPTIMIZED_BOARD_CONFIG_H
#define LIGHTVM_PLATFORM_HEAP_BUDGET 1
#define LIGHTVM_CAPABILITY_HEAP 1
/* IDF networking uses dynamic task stacks and packet buffers. Keep room for
 * connection/reconnection and HTTP tasks even before they have been created. */
#if LIGHTVM_WIFI && !defined(LIGHTVM_NATIVE_HEADROOM_BYTES)
#define LIGHTVM_NATIVE_HEADROOM_BYTES (64u * 1024u)
#endif
#if LIGHTVM_ESP32_OPTIMIZED
#include "esp_attr.h"
/* Same bounded executor on all ESP32 targets; explicit resources override it. */
#define LIGHTVM_DRAW_CODE IRAM_ATTR
#ifndef LIGHTVM_RENDER_WORKERS
#define LIGHTVM_RENDER_WORKERS 1u
#endif
#ifndef LIGHTVM_RENDER_SLICE_US
#define LIGHTVM_RENDER_SLICE_US 2000u
#endif
#ifndef LIGHTVM_RENDER_EPOCH_US
#define LIGHTVM_RENDER_EPOCH_US 2000u
#endif
#endif
#endif
