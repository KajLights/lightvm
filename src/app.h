#ifndef DEMO_APP_H
#define DEMO_APP_H
#include "protocol.h"
#include "wasm3.h"
#include "config.h"
#include "platform.h"
#include <stdbool.h>

typedef struct {
    uint8_t bytes[MODULE_LIMIT];
    size_t received, expected, output_size;
    uint32_t expected_crc;
    bool uploading, ready, guest_exit;
    int32_t exit_code;
    uint16_t request_id;
    uint64_t deadline;
    IM3Environment environment;
    IM3Runtime runtime;
    IM3Module module;
    IM3Function before_render, render;
    bool playing, native_demo, rendering_pixel, background;
    uint32_t pixel_index, native_phase, frame_count, render_us, show_us;
    uint64_t next_frame, last_frame;
    char animation_error[100];
    SendMessage send;
    void *transport;
} App;
void app_init(App *app, SendMessage send, void *transport);
void app_destroy(App *app);
void app_tick(App *app);
void app_start_native_demo(App *app);
void app_command(void *context, uint8_t type, uint16_t id, const uint8_t *payload, size_t size);
#endif
