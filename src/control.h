#ifndef LIGHTVM_CONTROL_H
#define LIGHTVM_CONTROL_H

#include "json.h"
#include <stdbool.h>
#include <stdint.h>
#define CONTROL_MANIFEST_LIMIT 8192u
typedef struct {
    bool enabled, initialized, in_step, faulted, contribution_valid, output_valid;
    uint32_t contribution_frame, output_frame, count;
    uint32_t manifest_offset, manifest_size;
    uint64_t program_us;
    uint8_t *allocation, *committed, *working, *presentation;
    size_t buffer_bytes, output_charge;
} Control;
struct VmInstance;
const char *control_probe(struct VmInstance *vm);
const char * control_prepare(struct VmInstance *vm);
const char * control_step(struct VmInstance *vm,uint64_t delta);
const uint8_t *control_frame(struct VmInstance *vm);
const uint8_t *control_output_frame(struct VmInstance *vm);
size_t control_frame_bytes(const struct VmInstance *vm);
void control_presented(struct VmInstance *vm,bool success);
const char *control_reset_program(struct VmInstance *vm,const Json *request,int settings);
const char *control_settings(struct VmInstance *vm,const Json *request,int settings);
void control_destroy(struct VmInstance *vm);
bool control_manage(struct VmInstance *vm,uint16_t id,const uint8_t *payload,size_t size);
#endif
