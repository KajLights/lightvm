#include "session.h"
#include "port.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    bool failed;
    int64_t write_deadline;
} UsbTransport;

static void output_bytes(void *context, const uint8_t *bytes, size_t size) {
    UsbTransport *transport = context;
    while (size && !transport->failed) {
        int64_t remaining = transport->write_deadline - esp_timer_get_time();
        if (remaining <= 0) {
            transport->failed = true;
            break;
        }
        TickType_t wait = pdMS_TO_TICKS((remaining + 999) / 1000);
        int written = usb_serial_jtag_write_bytes(bytes, size, wait);
        if (written <= 0) {
            transport->failed = true;
            break;
        }
        bytes += written;
        size -= (size_t)written;
    }
}
static void output_message(void *context, uint8_t type, uint16_t id,
                           const uint8_t *payload, size_t size) {
    UsbTransport *transport = context;
    if (transport->failed)
        return;
    // No terminal or a disconnected host must not suspend the engine forever.
    transport->write_deadline = esp_timer_get_time() + 100000;
    write_frame(output_bytes, context, type, id, payload, size);
    if (!transport->failed && usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(100)) != ESP_OK)
        transport->failed = true;
}
void app_main(void) {
    static Session session;
    UsbTransport transport = {0};
    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size = 2048, .rx_buffer_size = 2048};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    esp32_port_init();
    session_init(&session, output_message, &transport);
    for (;;) {
        session_tick(&session);
        uint8_t input[256];
        int size = usb_serial_jtag_read_bytes(input, sizeof(input), 0);
        if (size > 0) {
            transport.failed = false;
            session_feed(&session, input, (size_t)size);
        }
        // Yield even under continuous traffic so idle/watchdog tasks run.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
