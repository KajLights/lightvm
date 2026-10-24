#include "session.h"
#include "port.h"
#include "runtime.h"
#include "usb_connection.h"
#if LIGHTVM_MQTT
#include "home_assistant.h"
#include "mqtt_posix.h"
#endif
#if LIGHTVM_WIFI
#include "device/esp32.h"
#endif
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    bool failed;
    int64_t write_deadline;
    uint8_t frame[HEADER_SIZE + FRAME_LIMIT];
    size_t frame_size;
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
static void frame_bytes(void *context, const uint8_t *bytes, size_t size) {
    UsbTransport *transport = context;
    if (size > sizeof(transport->frame) - transport->frame_size) {
        transport->failed = true;
        return;
    }
    memcpy(transport->frame + transport->frame_size, bytes, size);
    transport->frame_size += size;
}
static void output_message(void *context, uint8_t type, uint16_t id,
                           const uint8_t *payload, size_t size) {
    UsbTransport *transport = context;
    if (transport->failed)
        return;
    // No terminal or a disconnected host must not suspend the engine forever.
    transport->write_deadline = esp_timer_get_time() + 100000;
    // A separate 16-byte header write can reach the USB FIFO before its
    // payload and end a short host transfer. Enqueue the complete frame once.
    transport->frame_size = 0;
    write_frame(frame_bytes, context, type, id, payload, size);
    output_bytes(context, transport->frame, transport->frame_size);
    // IDF copies each write into its FIFO ring. Queue DATA/PIXELS fragments
    // together: flushing every fragment adds host round trips while ingress is
    // blocked. The final status still drains the reply before reply_delivered.
    if (type == RESP_STATUS && !transport->failed &&
        usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(100)) != ESP_OK)
        transport->failed = true;
}
static void service_io(void *context) {
    (void)context;
#if LIGHTVM_WIFI
    device_esp32_poll();
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
    device_esp32_init(&app.device,false);
#endif
#if LIGHTVM_MQTT
    static HomeAssistant ha;
    static MqttSocket mqtt_socket;
    ha_init(&ha,&app,mqtt_socket_stream(&mqtt_socket));
#endif
    app.service_io=service_io;
    Esp32UsbConnection connection={.connected=usb_serial_jtag_is_connected()};
    for (;;) {
#if LIGHTVM_WIFI
        device_esp32_poll();
#endif
        app_tick(&app);
#if LIGHTVM_MQTT
        ha_tick(&ha,network_online(&app.network),platform_time_us());
#endif
        uint8_t input[256];
        runtime_ingress(platform_time_us());
        int size = usb_serial_jtag_read_bytes(input, sizeof(input), 0);
        if (esp32_usb_disconnected(&connection,usb_serial_jtag_is_connected(),size>0,platform_time_us())) {
            session_destroy(&session);
            session_init(&session,&app,1,output_message,&transport);
        }
        if (size > 0) {
            transport.failed = false;
            session_feed(&session, input, (size_t)size); app_reply_delivered(&app,1);
        }
        // Yield even under continuous traffic so idle/watchdog tasks run.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
