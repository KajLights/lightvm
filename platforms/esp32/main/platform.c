#include "platform.h"
#include "config.h"
#include "port.h"
#include "ws2812_encoder.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_timer.h"
#include "soc/soc_caps.h"
#include <stdlib.h>

#if PIXEL_BRIGHTNESS > 255
#error "PIXEL_BRIGHTNESS must be 0..255"
#endif
#if PIXEL_FREQUENCY != 800000
#error "The ESP32 backend currently supports only 800 kHz WS2812"
#endif
static rmt_channel_handle_t channel;
static rmt_encoder_handle_t encoder;
static uint8_t wire_rgb[PIXEL_COUNT * 3u];
static const PlatformInfo info = {
    "esp32", LIGHTVM_BOARD_NAME, "ws2812", PIXEL_PIN, PIXEL_BRIGHTNESS
};
const PlatformInfo *platform_info(void) { return &info; }
uint64_t platform_time_us(void) { return (uint64_t)esp_timer_get_time(); }
void esp32_port_init(void) {
    // Reject an invalid output or a pin occupied by native USB.
    if (!GPIO_IS_VALID_OUTPUT_GPIO(PIXEL_PIN) ||
#if CONFIG_IDF_TARGET_ESP32S3
        PIXEL_PIN == 19 || PIXEL_PIN == 20
#else
        PIXEL_PIN == 12 || PIXEL_PIN == 13
#endif
    )
        abort();
    const rmt_tx_channel_config_t config = {
        .gpio_num = PIXEL_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LIGHTVM_RMT_RESOLUTION_HZ,
        .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,
        .trans_queue_depth = 1,
        .flags.with_dma = false
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&config, &channel));
    ESP_ERROR_CHECK(lightvm_new_ws2812_encoder(&encoder));
    ESP_ERROR_CHECK(rmt_enable(channel));
}
void platform_show_rgb(const uint8_t *rgb, size_t count) {
    if (count > PIXEL_COUNT)
        abort();
    for (size_t index = 0; index < count; ++index) {
        const uint8_t *pixel = rgb + index * 3;
        wire_rgb[index * 3] = (pixel[1] * PIXEL_BRIGHTNESS + 127u) / 255u;
        wire_rgb[index * 3 + 1] = (pixel[0] * PIXEL_BRIGHTNESS + 127u) / 255u;
        wire_rgb[index * 3 + 2] = (pixel[2] * PIXEL_BRIGHTNESS + 127u) / 255u;
    }
    const rmt_transmit_config_t transmit = {.loop_count = 0};
    ESP_ERROR_CHECK(rmt_transmit(channel, encoder, wire_rgb, count * 3, &transmit));
    // Include transmission, latch and scheduling margin. The retained buffer
    // cannot be reused until the hardware has consumed the complete frame.
    int timeout_ms = (int)((count * 30u + 300u + 999u) / 1000u + 100u);
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(channel, timeout_ms));
}
void *platform_calloc(size_t count, size_t size) { return calloc(count, size); }
void *platform_realloc(void *memory, size_t size) { return realloc(memory, size); }
void platform_free(void *memory) { free(memory); }
