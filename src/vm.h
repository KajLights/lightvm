#ifndef LIGHTVM_VM_H
#define LIGHTVM_VM_H
#include "protocol.h"
#include "wasm3.h"
#include "config.h"
#include "platform.h"
#include <stdbool.h>
#include "pixels.h"
#include "control.h"

#if LIGHTVM_FLASH_EXPERIMENT
#include "program.h"
#endif
typedef struct VmInstance {
#if LIGHTVM_FLASH_EXPERIMENT
    uint8_t *bytes;
    ProgramMapping code_mapping;
#else
    uint8_t bytes[MODULE_LIMIT];
#endif
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
    bool playing, rendering_pixel, background;
    uint32_t pixel_index, frame_count, render_us, show_us;
    uint64_t next_frame, last_frame;
    char animation_error[100];
    SendMessage send;
    void *transport;
    PixelBuffer *target; // Assigned contribution dimensions; no hardware access in control_step.
    PixelBuffer *output; // Optional native presentation destination.
    char slot[33];
    uint32_t generation;
    Control control;
#if LIGHTVM_MQTT_EXPERIMENT
    size_t code_charge;
#endif
} VmInstance;
void vm_init(VmInstance *app, PixelBuffer *target, SendMessage send, void *transport);
bool vm_reserve_upload(VmInstance *,size_t);
M3Result vm_load(VmInstance *app);
M3Result vm_validate(const uint8_t *bytes, size_t size);
void vm_destroy(VmInstance *app);
void vm_tick(VmInstance *app);
// The same engine owner brackets experimental service execution.
void vm_service_execution(uint64_t deadline);
void vm_command(void *context, uint8_t type, uint16_t id, const uint8_t *payload, size_t size);
#endif
