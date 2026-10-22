#ifndef LIGHTVM_VM_H
#define LIGHTVM_VM_H
#include "protocol.h"
#include "bytecode_core.h"
#include "config.h"
#include "platform.h"
#include <stdbool.h>
#include "pixels.h"
#include "control.h"
#include "portable.h"
#include "store.h"

typedef struct VmInstance {
    const uint8_t *memory; /* Borrowed immutable fixtures/firmware only. */
    Store *store; StoreEntry code; int code_pin; bool from_storage;
    LbcBuffer view;
    Portable *portable;
    size_t received, expected;
    uint32_t expected_crc;
    bool uploading, ready;
    bool playing, output_pending, frame_ready;
    uint64_t scheduled_epoch;
    uint64_t output_started;
    uint32_t frame_count, render_us, show_us;
    uint64_t render_cpu_us, render_total_us;
    uint64_t next_frame, last_frame, next_present;
    char animation_error[100];
    SendMessage send;
    void *transport;
    PixelBuffer *target; // Assigned contribution dimensions; no hardware access in control_step.
    PixelBuffer *output; // Optional native presentation destination.
    char slot[33], output_id[33];
    char layer[OBJECT_NAME_LIMIT+1];
    uint32_t generation;
    Control control;
} VmInstance;
void vm_init(VmInstance *app, PixelBuffer *target, SendMessage send, void *transport);
const char * vm_load(VmInstance *app);
const char * vm_validate(const uint8_t *bytes, size_t size);
bool vm_read(void *context,uint32_t offset,void *bytes,size_t size);
bool vm_bind_storage(VmInstance *, Store *, const StoreEntry *);
const char *vm_validate_stored(Store *, const StoreEntry *);
void vm_rebind(VmInstance *);
void vm_destroy(VmInstance *app);
void vm_output_cancel(VmInstance *);
void vm_tick(VmInstance *app);
void vm_tick_budget(VmInstance *app,unsigned budget);
/* Collect independent VM slices across outputs, then join before presentation. */
void vm_tick_begin(void);
void vm_tick_collect(VmInstance *,unsigned budget);
void vm_tick_flush(void);
void vm_tick_end(void);
bool vm_tick_batching(void);
void vm_command(void *context, uint8_t type, uint16_t id, const uint8_t *payload, size_t size);
#endif
