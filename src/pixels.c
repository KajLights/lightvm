#include "pixels.h"
#include <string.h>
#ifdef DEMO_PICO
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"
static PIO pixel_pio = pio0;
static uint pixel_sm;
static bool initialized;
#endif

// One packed RGB buffer. The driver packs GRB words as it feeds the PIO FIFO.
static uint8_t rgb[PIXEL_BYTES];

void pixels_clear(void) {
    memset(rgb, 0, sizeof(rgb));
}

void pixels_init(void) {
    pixels_clear();
#ifdef DEMO_PICO
    pixel_sm = pio_claim_unused_sm(pixel_pio, true);
    uint offset = pio_add_program(pixel_pio, &ws2812_program);
    ws2812_program_init(pixel_pio, pixel_sm, offset, PIXEL_PIN, 800000, false);
    initialized = true;
    pixels_show();
#endif
}

const uint8_t *pixels_rgb(void) {
    return rgb;
}

bool pixels_set_rgb(uint32_t index, uint32_t red, uint32_t green, uint32_t blue) {
    if (index >= PIXEL_COUNT || red > 255 || green > 255 || blue > 255)
        return false;
    rgb[index * 3] = (uint8_t)red;
    rgb[index * 3 + 1] = (uint8_t)green;
    rgb[index * 3 + 2] = (uint8_t)blue;
    return true;
}

bool pixels_set_hsv(uint32_t index, uint32_t hue, uint32_t saturation, uint32_t value) {
    if (index >= PIXEL_COUNT || saturation > 255 || value > 255)
        return false;
    uint32_t scaled = (hue & 65535u) * 6u;
    uint32_t sector = scaled >> 16;
    uint32_t fraction = scaled & 65535u;
    const uint32_t denominator = 255u * 65535u;
    // Multiply before division and round once to avoid 1-LSB primary-color bias.
    uint32_t p = (value * (255u - saturation) + 127u) / 255u;
    uint32_t q = value - (value * saturation * fraction + denominator / 2u) / denominator;
    uint32_t t = value - (value * saturation * (65535u - fraction) + denominator / 2u) / denominator;
    switch (sector) {
    case 0: return pixels_set_rgb(index, value, t, p);
    case 1: return pixels_set_rgb(index, q, value, p);
    case 2: return pixels_set_rgb(index, p, value, t);
    case 3: return pixels_set_rgb(index, p, q, value);
    case 4: return pixels_set_rgb(index, t, p, value);
    default: return pixels_set_rgb(index, value, p, q);
    }
}

void pixels_rainbow(uint32_t phase) {
    for (uint32_t index = 0; index < PIXEL_COUNT; ++index)
        pixels_set_hsv(index, phase + index * 65536u / PIXEL_COUNT, 255, 255);
}

void pixels_show(void) {
#ifdef DEMO_PICO
    if (!initialized)
        return;
    for (uint32_t index = 0; index < PIXEL_COUNT; ++index) {
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
#endif
}
