#ifndef LIGHTVM_WS2812_ENCODER_H
#define LIGHTVM_WS2812_ENCODER_H
#include "driver/rmt_encoder.h"
esp_err_t lightvm_new_ws2812_encoder(rmt_encoder_handle_t *encoder);
#define LIGHTVM_RMT_RESOLUTION_HZ 20000000u
#endif
