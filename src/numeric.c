#include "numeric.h"
#include <float.h>
#include <limits.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8 && sizeof(float) == 4 && FLT_RADIX == 2 &&
               FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 && FLT_MIN_EXP == -125,
               "numeric conversions require IEEE binary32 storage");

int32_t lvm_i32_from_bits(uint32_t bits) {
    /* Keep the signed conversion in range, including on C11 implementations
       where converting an out-of-range unsigned value is implementation-defined. */
    return bits <= INT32_MAX ? (int32_t)bits : (int32_t)((int64_t)bits - INT64_C(4294967296));
}
uint32_t lvm_i32_bits(int32_t value) { return (uint32_t)value; }
LvmQ16 lvm_q16_add(LvmQ16 a, LvmQ16 b) { return lvm_i32_from_bits((uint32_t)a + (uint32_t)b); }
LvmQ16 lvm_q16_sub(LvmQ16 a, LvmQ16 b) { return lvm_i32_from_bits((uint32_t)a - (uint32_t)b); }
LvmQ16 lvm_q16_neg(LvmQ16 value) { return lvm_i32_from_bits(0u - (uint32_t)value); }
LvmQ16 lvm_q16_mul(LvmQ16 a, LvmQ16 b) {
    /* Signed division truncates toward zero; a signed right shift would not. */
    return lvm_i32_from_bits((uint32_t)(((int64_t)a * b) / 65536));
}
bool lvm_q16_div(LvmQ16 a, LvmQ16 b, LvmQ16 *out) {
    if (!b) return false;
    int64_t quotient = ((int64_t)a * 65536) / b;
    if (quotient < INT32_MIN || quotient > INT32_MAX) return false;
    *out = (LvmQ16)quotient;
    return true;
}
bool lvm_q16_mod(LvmQ16 a, LvmQ16 b, LvmQ16 *out) {
    if (!b) return false;
    int64_t remainder = (int64_t)a % b;
    if (remainder && ((remainder < 0) != (b < 0))) remainder += b;
    *out = (LvmQ16)remainder;
    return true;
}
LvmQ16 lvm_q16_min(LvmQ16 a, LvmQ16 b) { return a < b ? a : b; }
LvmQ16 lvm_q16_max(LvmQ16 a, LvmQ16 b) { return a > b ? a : b; }
bool lvm_q16_lt(LvmQ16 a, LvmQ16 b) { return a < b; }
bool lvm_q16_eq(LvmQ16 a, LvmQ16 b) { return a == b; }
bool lvm_q16_sqrt(LvmQ16 value, LvmQ16 *out) {
    if (value < 0) return false;
    uint64_t remainder = (uint64_t)value * 65536u, root = 0;
    /* The radicand is below 2^47. Exactly 24 restoring steps suffice. */
    for (uint64_t bit = UINT64_C(1) << 46; bit; bit >>= 2) {
        if (remainder >= root + bit) {
            remainder -= root + bit;
            root = (root >> 1) + bit;
        } else root >>= 1;
    }
    *out = (LvmQ16)root;
    return true;
}
bool lvm_q16_from_i32(int32_t value, LvmQ16 *out) {
    if (value < -32768 || value > 32767) return false;
    *out = (LvmQ16)((int64_t)value * 65536);
    return true;
}
int32_t lvm_q16_to_i32(LvmQ16 value) { return value / 65536; }

/* Only called with 1 <= shift <= 24. */
static uint32_t round_even_shift(uint32_t value, unsigned shift) {
    uint32_t result = value >> shift;
    uint32_t remainder = value & ((UINT32_C(1) << shift) - 1u);
    uint32_t half = UINT32_C(1) << (shift - 1u);
    return result + (remainder > half || (remainder == half && (result & 1u)));
}
bool lvm_q16_from_f32_bits(uint32_t bits, LvmQ16 *out) {
    unsigned exponent = (bits >> 23) & 255u;
    bool negative = (bits >> 31) != 0;
    if (exponent == 255u) return false;
    /* Zero and all subnormals are less than half a Q16.16 unit. */
    if (!exponent) { *out = 0; return true; }
    uint32_t significand = (bits & 0x7fffffu) | 0x800000u;
    int shift = (int)exponent - 134; /* exponent - 127 - 23 + 16 */
    uint64_t magnitude;
    if (shift >= 0) {
        if (shift > 8) return false;
        magnitude = (uint64_t)significand << (unsigned)shift;
    } else {
        magnitude = shift < -24 ? 0 : round_even_shift(significand, (unsigned)-shift);
    }
    uint64_t limit = negative ? UINT64_C(2147483648) : INT32_MAX;
    if (magnitude > limit) return false;
    *out = (LvmQ16)(negative ? -(int64_t)magnitude : (int64_t)magnitude);
    return true;
}
uint32_t lvm_q16_to_f32_bits(LvmQ16 value) {
    uint32_t magnitude = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    if (!magnitude) return 0;
    unsigned top = 0;
    for (uint32_t rest = magnitude; rest >>= 1;) top++;
    uint32_t significand = top <= 23 ? magnitude << (23u - top) : round_even_shift(magnitude, top - 23u);
    unsigned exponent = top + 111u; /* top - 16 + 127 */
    if (significand == 0x1000000u) { significand >>= 1; exponent++; }
    return (value < 0 ? 0x80000000u : 0u) | (exponent << 23) | (significand & 0x7fffffu);
}
bool lvm_q16_from_f32(float value, LvmQ16 *out) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return lvm_q16_from_f32_bits(bits, out);
}
float lvm_q16_to_f32(LvmQ16 value) {
    uint32_t bits = lvm_q16_to_f32_bits(value);
    float result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

int32_t lvm_i32_and(int32_t a, int32_t b) { return lvm_i32_from_bits((uint32_t)a & (uint32_t)b); }
int32_t lvm_i32_or(int32_t a, int32_t b) { return lvm_i32_from_bits((uint32_t)a | (uint32_t)b); }
int32_t lvm_i32_xor(int32_t a, int32_t b) { return lvm_i32_from_bits((uint32_t)a ^ (uint32_t)b); }
int32_t lvm_i32_not(int32_t value) { return lvm_i32_from_bits(~(uint32_t)value); }
bool lvm_i32_shl(int32_t value, int32_t count, int32_t *out) {
    if (count < 0 || count > 31) return false;
    *out = lvm_i32_from_bits((uint32_t)value << (unsigned)count);
    return true;
}
bool lvm_i32_shr(int32_t value, int32_t count, int32_t *out) {
    if (count < 0 || count > 31) return false;
    uint32_t bits = (uint32_t)value >> (unsigned)count;
    if (value < 0 && count) bits |= UINT32_MAX << (32u - (unsigned)count);
    *out = lvm_i32_from_bits(bits);
    return true;
}
