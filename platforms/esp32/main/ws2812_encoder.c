/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adapted from ESP-IDF v5.5.3 examples/peripherals/rmt/led_strip/main/
 * led_strip_encoder.c. LightVM changes: integer 800 kHz timings, 300 us reset,
 * and a fixed 20 MHz resolution. Preserve encoder state across RMT refills.
 */
#include "ws2812_encoder.h"
#include <stddef.h>
#include <stdlib.h>

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_handle_t bytes;
    rmt_encoder_handle_t copy;
    int state;
    rmt_symbol_word_t reset;
} StripEncoder;

static inline StripEncoder *strip_encoder(rmt_encoder_t *base) {
    return (StripEncoder *)((char *)base - offsetof(StripEncoder, base));
}
static RMT_ENCODER_FUNC_ATTR size_t encode(rmt_encoder_t *base, rmt_channel_handle_t channel,
                     const void *data, size_t size, rmt_encode_state_t *result) {
    StripEncoder *strip = strip_encoder(base);
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    *result = RMT_ENCODING_RESET;
    size_t symbols = 0;
    if (strip->state == 0) {
        symbols += strip->bytes->encode(strip->bytes, channel, data, size, &state);
        if (state & RMT_ENCODING_COMPLETE)
            strip->state = 1;
        if (state & RMT_ENCODING_MEM_FULL) {
            *result = RMT_ENCODING_MEM_FULL;
            return symbols;
        }
    }
    symbols += strip->copy->encode(strip->copy, channel, &strip->reset,
                                  sizeof(strip->reset), &state);
    if (state & RMT_ENCODING_COMPLETE) {
        strip->state = 0;
        *result |= RMT_ENCODING_COMPLETE;
    }
    if (state & RMT_ENCODING_MEM_FULL)
        *result |= RMT_ENCODING_MEM_FULL;
    return symbols;
}
static RMT_ENCODER_FUNC_ATTR esp_err_t reset(rmt_encoder_t *base) {
    StripEncoder *strip = strip_encoder(base);
    rmt_encoder_reset(strip->bytes);
    rmt_encoder_reset(strip->copy);
    strip->state = 0;
    return ESP_OK;
}
static esp_err_t destroy(rmt_encoder_t *base) {
    StripEncoder *strip = strip_encoder(base);
    rmt_del_encoder(strip->bytes);
    rmt_del_encoder(strip->copy);
    free(strip);
    return ESP_OK;
}
esp_err_t lightvm_new_ws2812_encoder(rmt_encoder_handle_t *encoder) {
    if (!encoder)
        return ESP_ERR_INVALID_ARG;
    StripEncoder *strip = rmt_alloc_encoder_mem(sizeof(*strip));
    if (!strip)
        return ESP_ERR_NO_MEM;
    strip->base.encode = encode;
    strip->base.reset = reset;
    strip->base.del = destroy;
    strip->reset = (rmt_symbol_word_t) {
        .level0 = 0, .duration0 = 3000, .level1 = 0, .duration1 = 3000
    };
    const rmt_bytes_encoder_config_t bytes = {
        .bit0 = {.level0 = 1, .duration0 = 7, .level1 = 0, .duration1 = 18},
        .bit1 = {.level0 = 1, .duration0 = 14, .level1 = 0, .duration1 = 11},
        .flags.msb_first = 1
    };
    esp_err_t error = rmt_new_bytes_encoder(&bytes, &strip->bytes);
    if (error != ESP_OK) {
        free(strip);
        return error;
    }
    const rmt_copy_encoder_config_t copy = {};
    error = rmt_new_copy_encoder(&copy, &strip->copy);
    if (error != ESP_OK) {
        rmt_del_encoder(strip->bytes);
        free(strip);
        return error;
    }
    *encoder = &strip->base;
    return ESP_OK;
}
