#ifndef LIGHTVM_ANIMATION_PROFILE_H
#define LIGHTVM_ANIMATION_PROFILE_H
#include "vm.h"
const char *animation_validate(Portable *program);
const char *animation_prepare(VmInstance *vm);
const char *animation_preflight(VmInstance *vm,uint64_t delta);
const char *animation_advance(VmInstance *vm,uint64_t delta,bool manual);
const char *animation_poll(VmInstance *vm,uint64_t delta,bool *ready);
const char *animation_poll_budget(VmInstance *vm,uint64_t delta,bool *ready,unsigned budget);
const char *animation_quiesce(VmInstance *vm);
#endif
