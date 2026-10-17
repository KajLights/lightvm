#ifndef LIGHTVM_BYTECODE_CORE_H
#define LIGHTVM_BYTECODE_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
enum { END=0, RETURN, MOV, IADD, ISUB, IMUL, IDIV, IMOD, FADD, FSUB, FMUL, FDIV, FMOD, ITOF, FTOI, ILT, FLT, EQ, JZ, JMP, RGB, HSV,
       IMIN=23, IMAX, FMIN, FMAX, HASH };
enum { T_INT=1,T_FLOAT,T_COLOR,T_BOOL };
enum { R_LOCAL,R_INPUT,R_CONST,R_STATE };
typedef bool (*LbcRead)(void *context, uint32_t offset, void *bytes, size_t size);
/* Native owner issues this view. Guest numeric registers never contain pointers. */
typedef struct { uint8_t *data; uint32_t count,generation; bool sealed; } LbcBuffer;
uint32_t lbc_u32(const uint8_t *p);
uint32_t lbc_hsv(float h,float s,float v);
float lbc_hash01(int32_t index,int32_t seed);
#endif
