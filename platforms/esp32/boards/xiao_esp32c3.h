#ifndef LIGHTVM_XIAO_ESP32C3_BOARD_CONFIG_H
#define LIGHTVM_XIAO_ESP32C3_BOARD_CONFIG_H
#define LIGHTVM_BOARD_NAME "xiao_esp32c3"
#if LIGHTVM_ESP32_C3_PROFILE
#include "esp_attr.h"
/* Measured C3 profile: keep the drawing kernels in instruction RAM and bound
   the single-core render turn. Explicit resource options still take priority. */
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
