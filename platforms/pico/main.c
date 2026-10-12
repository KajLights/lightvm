#include "session.h"
#include "port.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/stdio/driver.h"

static void output_bytes(void *context, const uint8_t *bytes, size_t size) {
    (void)context;
    stdio_usb.out_chars((const char *)bytes, (int)size);
}
static void output_message(void *context, uint8_t type, uint16_t id,
                           const uint8_t *payload, size_t size) {
    write_frame(output_bytes, context, type, id, payload, size);
    stdio_flush();
}
int main(void) {
    static Session session;
    stdio_init_all();
    stdio_set_translate_crlf(&stdio_usb, false);
    pico_port_init();
    session_init(&session, output_message, NULL);
    for (;;) {
        session_tick(&session);
        int byte = getchar_timeout_us(1000);
        if (byte == PICO_ERROR_TIMEOUT)
            continue;
        uint8_t input = (uint8_t)byte;
        session_feed(&session, &input, 1);
    }
}
