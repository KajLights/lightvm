#ifndef LIGHTVM_NUMERIC_H
#define LIGHTVM_NUMERIC_H

/* Standalone numeric foundation; not yet linked into the interpreter or firmware.
 * Q16.16 values are signed raw integers, with one unit equal to 1/65536.
 * Checked operations require a non-null out pointer and leave it unchanged on
 * failure. All operations are pure, bounded and allocation-free.
 */
#include <stdbool.h>
#include <stdint.h>

typedef int32_t LvmQ16;

int32_t lvm_i32_from_bits(uint32_t bits);
uint32_t lvm_i32_bits(int32_t value);
LvmQ16 lvm_q16_add(LvmQ16 a, LvmQ16 b);
LvmQ16 lvm_q16_sub(LvmQ16 a, LvmQ16 b);
LvmQ16 lvm_q16_neg(LvmQ16 value);
LvmQ16 lvm_q16_mul(LvmQ16 a, LvmQ16 b);
bool lvm_q16_div(LvmQ16 a, LvmQ16 b, LvmQ16 *out);
bool lvm_q16_mod(LvmQ16 a, LvmQ16 b, LvmQ16 *out);
LvmQ16 lvm_q16_min(LvmQ16 a, LvmQ16 b);
LvmQ16 lvm_q16_max(LvmQ16 a, LvmQ16 b);
bool lvm_q16_lt(LvmQ16 a, LvmQ16 b);
bool lvm_q16_eq(LvmQ16 a, LvmQ16 b);
bool lvm_q16_sqrt(LvmQ16 value, LvmQ16 *out);
bool lvm_q16_from_i32(int32_t value, LvmQ16 *out);
int32_t lvm_q16_to_i32(LvmQ16 value);

/* IEEE binary32 bit conversions use integer arithmetic and ties-to-even,
 * independent of the host floating-point rounding mode. Signed zeros map to
 * raw zero; converting raw zero to f32 produces positive zero.
 */
bool lvm_q16_from_f32_bits(uint32_t bits, LvmQ16 *out);
uint32_t lvm_q16_to_f32_bits(LvmQ16 value);
bool lvm_q16_from_f32(float value, LvmQ16 *out);
float lvm_q16_to_f32(LvmQ16 value);

int32_t lvm_i32_and(int32_t a, int32_t b);
int32_t lvm_i32_or(int32_t a, int32_t b);
int32_t lvm_i32_xor(int32_t a, int32_t b);
int32_t lvm_i32_not(int32_t value);
bool lvm_i32_shl(int32_t value, int32_t count, int32_t *out);
bool lvm_i32_shr(int32_t value, int32_t count, int32_t *out);

#endif
