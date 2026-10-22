/* A line-oriented host test adapter, never part of the firmware. */
#include "numeric.h"
#include <fenv.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc > 1) {
        int mode = !strcmp(argv[1], "down") ? FE_DOWNWARD :
                   !strcmp(argv[1], "up") ? FE_UPWARD :
                   !strcmp(argv[1], "zero") ? FE_TOWARDZERO : FE_TONEAREST;
        if (fesetround(mode)) return 3;
    }
    char operation[32];
    uint32_t a, b;
    while (scanf("%31s %" SCNx32 " %" SCNx32, operation, &a, &b) == 3) {
        int32_t x = lvm_i32_from_bits(a), y = lvm_i32_from_bits(b);
        int32_t result = INT32_C(0x5a5aa5a5);
        uint32_t bits;
        bool ok = true;
        if (!strcmp(operation, "q16.add")) result = lvm_q16_add(x, y);
        else if (!strcmp(operation, "q16.sub")) result = lvm_q16_sub(x, y);
        else if (!strcmp(operation, "q16.neg")) result = lvm_q16_neg(x);
        else if (!strcmp(operation, "q16.mul")) result = lvm_q16_mul(x, y);
        else if (!strcmp(operation, "q16.div")) ok = lvm_q16_div(x, y, &result);
        else if (!strcmp(operation, "q16.mod")) ok = lvm_q16_mod(x, y, &result);
        else if (!strcmp(operation, "q16.min")) result = lvm_q16_min(x, y);
        else if (!strcmp(operation, "q16.max")) result = lvm_q16_max(x, y);
        else if (!strcmp(operation, "q16.lt")) result = lvm_q16_lt(x, y);
        else if (!strcmp(operation, "q16.eq")) result = lvm_q16_eq(x, y);
        else if (!strcmp(operation, "q16.sqrt")) ok = lvm_q16_sqrt(x, &result);
        else if (!strcmp(operation, "q16.from_i32")) ok = lvm_q16_from_i32(x, &result);
        else if (!strcmp(operation, "q16.to_i32")) result = lvm_q16_to_i32(x);
        else if (!strcmp(operation, "q16.from_f32")) {
            float value;
            memcpy(&value, &a, sizeof(value));
            ok = lvm_q16_from_f32(value, &result);
            int32_t raw_result = INT32_C(0x5a5aa5a5);
            if (ok != lvm_q16_from_f32_bits(a, &raw_result) || result != raw_result) return 2;
        } else if (!strcmp(operation, "q16.to_f32")) {
            float value = lvm_q16_to_f32(x);
            memcpy(&bits, &value, sizeof(bits));
            if (bits != lvm_q16_to_f32_bits(x)) return 2;
            result = lvm_i32_from_bits(bits);
        } else if (!strcmp(operation, "q16.bits") || !strcmp(operation, "q16.from_bits")) result = x;
        else if (!strcmp(operation, "i32.and")) result = lvm_i32_and(x, y);
        else if (!strcmp(operation, "i32.or")) result = lvm_i32_or(x, y);
        else if (!strcmp(operation, "i32.xor")) result = lvm_i32_xor(x, y);
        else if (!strcmp(operation, "i32.not")) result = lvm_i32_not(x);
        else if (!strcmp(operation, "i32.shl")) ok = lvm_i32_shl(x, y, &result);
        else if (!strcmp(operation, "i32.shr")) ok = lvm_i32_shr(x, y, &result);
        else return 2;
        printf("%u %08" PRIx32 "\n", ok ? 1u : 0u, lvm_i32_bits(result));
    }
    return ferror(stdin) ? 1 : 0;
}
