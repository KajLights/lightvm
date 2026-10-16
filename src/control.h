#ifndef LIGHTVM_CONTROL_H
#define LIGHTVM_CONTROL_H

#include "json.h"
#include <stdbool.h>
#include <stdint.h>
#define CONTROL_ABI_VERSION 2u
#define CONTROL_PARAMETER_LIMIT 8u
#define CONTROL_MANIFEST_LIMIT 8192u
#define CONTROL_NAMESPACE "lightvm.control.v2"
typedef enum { PARAM_BOOL, PARAM_INT, PARAM_FLOAT, PARAM_RGB } ParameterKind;
typedef union { int64_t integer; double real; } ParameterValue;
typedef struct {
    char name[33];
    ParameterKind kind;
    ParameterValue value, minimum, maximum;
    bool has_minimum, has_maximum;
} ControlParameter;
typedef struct {
    bool enabled, initialized, in_step, faulted, contribution_valid, output_valid;
    uint32_t parameter_count, parameter_revision, contribution_frame, output_frame, count;
    ControlParameter parameters[CONTROL_PARAMETER_LIMIT];
    uint32_t manifest_offset, manifest_size;
    uint64_t program_us;
    uint8_t parameter_registers[CONTROL_PARAMETER_LIMIT];
    uint8_t *allocation, *committed, *working;
    size_t buffer_bytes, output_charge;
    void *continuation; size_t continuation_bytes;
} Control;
struct VmInstance;
const char *control_probe(struct VmInstance *vm);
const char * control_prepare(struct VmInstance *vm);
const char * control_step(struct VmInstance *vm,uint32_t trigger,uint64_t delta);
const char *control_playback_step(struct VmInstance *vm,uint64_t delta);
const uint8_t *control_frame(struct VmInstance *vm);
size_t control_frame_bytes(const struct VmInstance *vm);
void control_presented(struct VmInstance *vm,bool success);
const char *control_reset_program(struct VmInstance *vm,const Json *request,int settings);
const char *control_settings(struct VmInstance *vm,const Json *request,int settings);
void control_destroy(struct VmInstance *vm);
bool control_manage(struct VmInstance *vm,uint16_t id,const uint8_t *payload,size_t size);
#endif
