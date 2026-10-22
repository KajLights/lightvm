#ifndef LIGHTVM_ANIMATION_PROFILE_H
#define LIGHTVM_ANIMATION_PROFILE_H
#include "vm.h"
const char *animation_validate(Portable *program);
typedef struct AnimationPreparation {
    unsigned stage;
    size_t cleared;
    bool buffers_ready;
    PbcPreparation resources;
    const char *error;
} AnimationPreparation;
int animation_prepare_step(AnimationPreparation *,VmInstance *);
const char *animation_prepare(VmInstance *vm);
const char *animation_preflight(VmInstance *vm,uint64_t delta);
const char *animation_advance(VmInstance *vm,uint64_t delta,bool manual);
const char *animation_poll(VmInstance *vm,uint64_t delta,bool *ready);
const char *animation_poll_budget(VmInstance *vm,uint64_t delta,bool *ready,unsigned budget);
/* Capture/start and commit run on the coordinator; execution is instance-local. */
typedef struct { VmInstance *vm;unsigned budget;int status; } AnimationSlice;
const char *animation_begin(VmInstance *,uint64_t delta);
void animation_execute(void *slice);
const char *animation_commit(AnimationSlice *,uint64_t elapsed_us,bool *ready);
const char *animation_quiesce(VmInstance *vm);
#endif
