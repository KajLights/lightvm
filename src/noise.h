#ifndef LIGHTVM_NOISE_H
#define LIGHTVM_NOISE_H
#include <stdbool.h>
#include <stdint.h>

enum { LVM_PERLIN2=59, LVM_PERLIN3, LVM_SIMPLEX2, LVM_SIMPLEX3 };
typedef struct { uint8_t argc,args[7],work; } LvmNoiseSignature;
LvmNoiseSignature lvm_noise_signature(unsigned binding);
/* Arguments/results are typed register words. Coordinates are finite f32 in
 * [-65536,65536], seeds are arbitrary i32 bits, Perlin periods are i32 1..65536.
 * Pure, bounded, allocation-free; failure leaves *out unchanged. See noise.md. */
bool lvm_noise_eval(unsigned binding,const uint32_t args[7],uint32_t *out);
#endif
