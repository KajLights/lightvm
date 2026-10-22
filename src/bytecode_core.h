#ifndef LIGHTVM_BYTECODE_CORE_H
#define LIGHTVM_BYTECODE_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <float.h>
_Static_assert(sizeof(float)==4 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128,"LBC1 requires IEEE binary32");
/* Avoid soft-float comparison calls in per-pixel validity checks. */
static inline bool lbc_finite(float value){
    uint32_t bits;memcpy(&bits,&value,sizeof(bits));
    return (bits&0x7f800000u)!=0x7f800000u;
}
enum { END=0, RETURN, MOV, IADD, ISUB, IMUL, IDIV, IMOD, FADD, FSUB, FMUL, FDIV, FMOD, ITOF, FTOI, ILT, FLT, EQ, JZ, JMP, RGB, HSV,
       IMIN=23, IMAX, FMIN, FMAX, HASH,
       QADD=40, QSUB, QMUL, QDIV, QMOD, QNEG, QMIN, QMAX, QLT,
       ITOQ, QTOI, FTOQ, QTOF, QFROMBITS, QBITS, QSQRT,
       IAND, IOR, IXOR, INOT, ISHL, ISHR };
enum { T_INT=1,T_FLOAT,T_COLOR,T_BOOL,T_FIXED=7 };
enum { LBC_FIXED_ARRAY=20 };
#define LBC_NUMERIC_RELEASE "1.2.0"
typedef struct { uint8_t result,left,right; } LbcNumericSignature;
/* New scalar instructions share types and exact evaluation with declaration
 * expressions. A zero right type denotes a unary operation. Unknown IDs have
 * a zero result and cannot be emitted or admitted. */
LbcNumericSignature lbc_numeric_signature(unsigned op);
bool lbc_numeric_eval(unsigned op,uint32_t left,uint32_t right,uint32_t *out);
enum { R_LOCAL,R_INPUT,R_CONST,R_STATE };
typedef bool (*LbcRead)(void *context, uint32_t offset, void *bytes, size_t size);
/* Native owner issues this view. Guest numeric registers never contain pointers. */
typedef struct { uint8_t *data; uint32_t count,generation; bool sealed; } LbcBuffer;
static inline uint32_t lbc_u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
uint32_t lbc_hsv(float h,float s,float v);
float lbc_hash01(int32_t index,int32_t seed);
#endif
