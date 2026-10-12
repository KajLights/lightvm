#include "app.h"
#include "pixels.h"
#include <stdio.h>
#ifdef DEMO_PICO
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/stdio/driver.h"
#else
#include <errno.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>
#endif

uint64_t platform_time_us(void) {
#ifdef DEMO_PICO
    return time_us_64();
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
#endif
}

static void output_bytes(void *context, const uint8_t *bytes, size_t size) {
    (void)context;
#ifdef DEMO_PICO
    stdio_usb.out_chars((const char *)bytes, (int)size);
#else
    if (fwrite(bytes, 1, size, stdout) != size)
        return;
#endif
}

static void output_message(void *context, uint8_t type, uint16_t id,
                           const uint8_t *payload, size_t size) {
    write_frame(output_bytes, context, type, id, payload, size);
#ifdef DEMO_PICO
    stdio_flush();
#else
    fflush(stdout);
#endif
}

int main(void) {
    static App app;
    static Decoder decoder;
#ifdef DEMO_PICO
    stdio_init_all();
    stdio_set_translate_crlf(&stdio_usb, false);
#endif
    pixels_init();
    app_init(&app, output_message, NULL);
    decoder_init(&decoder, app_command, output_message, &app);
#ifdef DEMO_PICO
    // The LED smoke test runs without waiting for a USB host or a WASM upload.
    app_start_native_demo(&app);
#endif
    for (;;) {
        app_tick(&app);
#ifdef DEMO_PICO
        int byte = getchar_timeout_us(1000);
        if (byte == PICO_ERROR_TIMEOUT)
            continue;
        decoder_feed(&decoder, (uint8_t)byte);
#else
        fd_set input;
        FD_ZERO(&input);
        FD_SET(STDIN_FILENO, &input);
        struct timeval timeout = {0, 1000};
        int ready = select(STDIN_FILENO + 1, &input, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (!ready)
            continue;
        uint8_t byte;
        ssize_t size = read(STDIN_FILENO, &byte, 1);
        if (size <= 0)
            break;
        decoder_feed(&decoder, byte);
#endif
    }
    app_destroy(&app);
    return 0;
}
