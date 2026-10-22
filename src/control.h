#ifndef LIGHTVM_CONTROL_H
#define LIGHTVM_CONTROL_H

#include "json.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint64_t captured_us,completed_us,dirty_since_us;
    uint32_t generation,frame,dependency_revision;
} ContributionStamp;
typedef struct {
    bool enabled, initialized, in_step, faulted, contribution_valid, output_valid;
    uint32_t contribution_frame, output_frame, count;
    uint32_t manifest_offset, manifest_size;
    uint64_t program_us;
    uint64_t capture_started_us,presented_us,distinct_presentations;
    ContributionStamp contribution,submitted,displayed;
    uint64_t captured_dirty_since_us,dirty_since_us,dirty_to_display_us,wait_since_us,wait_us[5];
    uint32_t observed_inputs[8],dependency_revision,captured_revision;
    uint8_t observed_fallback,wait_kind;
    bool observed,dirty;

    uint8_t *allocation, *committed, *working, *presentation;
    size_t buffer_bytes, output_charge;
} Control;
struct VmInstance;
void control_loaded(struct VmInstance *vm);
struct AnimationPreparation;
int control_prepare_step(struct AnimationPreparation *,struct VmInstance *);
const char * control_prepare(struct VmInstance *vm);
const char * control_step(struct VmInstance *vm,uint64_t delta);
const uint8_t *control_frame(struct VmInstance *vm);
const uint8_t *control_output_frame(struct VmInstance *vm);
size_t control_frame_bytes(const struct VmInstance *vm);
void control_presented(struct VmInstance *vm,bool success);
void control_observe(struct VmInstance *,uint64_t);
void control_wait(struct VmInstance *,unsigned); /* idle, runnable, composite, output, cached */
void control_contribution_presented(struct VmInstance *,ContributionStamp,uint64_t);
const char *control_reset_program(struct VmInstance *vm,const Json *request,int settings);
const char *control_settings(struct VmInstance *vm,const Json *request,int settings);
void control_destroy(struct VmInstance *vm);
bool control_manage(struct VmInstance *vm,uint16_t id,const Json *request,const char *operation);
#endif
