#ifndef LIGHTVM_NATIVE_MATH_H
#define LIGHTVM_NATIVE_MATH_H
#include <stdbool.h>
#include <stdint.h>
enum { LVM_ABS,LVM_FLOOR,LVM_CEIL,LVM_TRUNC,LVM_SIN,LVM_COS,
       LVM_SQRT,LVM_LOG,LVM_EXP,LVM_POW,LVM_MIN,LVM_MAX };
/* Finite IEEE binary32 words, exact domains and explicit signed zeros. A fault
 * leaves *out unchanged. No allocation, shared mutable state or fenv changes. */
bool lvm_f32_eval(unsigned op,uint32_t left,uint32_t right,uint32_t *out);
#endif
