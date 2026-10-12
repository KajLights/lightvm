#include "platform.h"
#include "config.h"
#include "port.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"
#include <stdlib.h>

#if PIXEL_BRIGHTNESS > 255
#error "PIXEL_BRIGHTNESS must be 0..255"
#endif
static PIO pixel_pio = pio0;
static uint pixel_sm;
static const PlatformInfo info = {
    "pico", LIGHTVM_BOARD_NAME, "ws2812", PIXEL_PIN, PIXEL_BRIGHTNESS
};
const PlatformInfo *platform_info(void) { return &info; }
uint64_t platform_time_us(void) { return time_us_64(); }
void pico_port_init(void) {
    pixel_sm = pio_claim_unused_sm(pixel_pio, true);
    uint offset = pio_add_program(pixel_pio, &ws2812_program);
    ws2812_program_init(pixel_pio, pixel_sm, offset, PIXEL_PIN, PIXEL_FREQUENCY, false);
}
void platform_show_rgb(const uint8_t *rgb, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        const uint8_t *pixel = rgb + index * 3;
        uint32_t red = (pixel[0] * PIXEL_BRIGHTNESS + 127u) / 255u;
        uint32_t green = (pixel[1] * PIXEL_BRIGHTNESS + 127u) / 255u;
        uint32_t blue = (pixel[2] * PIXEL_BRIGHTNESS + 127u) / 255u;
        pio_sm_put_blocking(pixel_pio, pixel_sm, (green << 24) | (red << 16) | (blue << 8));
    }
    // Empty FIFO still leaves the last 24-bit word in the shift register.
    // Allow its 30 us transmission plus a conservative 300 us reset interval.
    while (!pio_sm_is_tx_fifo_empty(pixel_pio, pixel_sm))
        tight_loop_contents();
    sleep_us(330);
}
void *platform_calloc(size_t count, size_t size) { return calloc(count, size); }
void *platform_realloc(void *memory, size_t size) { return realloc(memory, size); }
void platform_free(void *memory) { free(memory); }
