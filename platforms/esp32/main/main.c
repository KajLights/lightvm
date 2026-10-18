#include "session.h"
#include "port.h"
#if LIGHTVM_MQTT
#include "home_assistant.h"
#include "mqtt_posix.h"
#endif
#if LIGHTVM_WIFI
#include "network_port.h"
#endif
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
static void service_io(void *context) {
    (void)context;
#if LIGHTVM_WIFI
    esp32_network_poll();
#endif
    taskYIELD();
}
void app_main(void) {
    static App app;
    static Session session;
    UsbTransport transport = {0};
    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size = 2048, .rx_buffer_size = 2048};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    esp32_port_init();
    app_init(&app, output_message, &transport);
    session_init(&session, &app, 1, output_message, &transport);
#if LIGHTVM_WIFI
    esp32_network_init(&app);
#endif
#if LIGHTVM_MQTT
    static HomeAssistant ha;
    static MqttSocket mqtt_socket;
    ha_init(&ha,&app,mqtt_socket_stream(&mqtt_socket));
#endif
    app.service_io=service_io;
    bool connected=usb_serial_jtag_is_connected();
    for (;;) {
#if LIGHTVM_WIFI
        esp32_network_poll();
#endif
        bool current=usb_serial_jtag_is_connected();
        if (connected && !current) { session_destroy(&session); session_init(&session,&app,1,output_message,&transport); }
        connected=current;
        app_tick(&app);
#if LIGHTVM_MQTT
        ha_tick(&ha,network_online(&app.network),platform_time_us());
#endif
        uint8_t input[256];
        int size = usb_serial_jtag_read_bytes(input, sizeof(input), 0);
        if (size > 0) {
            transport.failed = false;
            session_feed(&session, input, (size_t)size); app_reply_delivered(&app,1);
        }
        // Yield even under continuous traffic so idle/watchdog tasks run.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
