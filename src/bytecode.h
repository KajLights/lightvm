#ifndef LBC_VM_H
#define LBC_VM_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
enum { LBC_REGS=128, LBC_CODE=2048, LBC_MAX_BYTES=32768 };
enum { END, RETURN, MOV, IADD, ISUB, IMUL, IDIV, IMOD, FADD, FSUB, FMUL, FDIV, FMOD, ITOF, FTOI, ILT, FLT, EQ, JZ, JMP, RGB, HSV, PALETTE, IMIN, IMAX, FMIN, FMAX, HASH, HSV_RETURN, STORE, FILL, HUE_RETURN, IFMADD };
enum { T_INT=1,T_FLOAT,T_COLOR,T_BOOL };
enum { R_LOCAL,R_INPUT,R_CONST,R_STATE,R_FRAME,R_PARAM };
typedef union { uint32_t u; int32_t i; float f; } LbcValue;
typedef bool (*LbcRead)(void *context, uint32_t offset, void *bytes, size_t size);
typedef struct {
    const uint8_t *raw, *registers, *code, *palette;
    uint32_t nregs, ncode, ncolors, entry[3], validated, mode;
    uint32_t code_offset, palette_offset, manifest_offset, manifest_size, size;
    LbcRead read; void *context;
    uint8_t descriptors[LBC_REGS * 8];
    _Alignas(4) uint8_t cache[256];
    uint32_t cache_offset, cache_size;
} LbcProgram;
/* Native owner issues this view. Guest numeric registers never contain pointers. */
typedef struct { uint8_t *data; uint32_t count,generation; bool sealed; } LbcBuffer;
typedef struct {
    LbcProgram *program; LbcBuffer *buffer; uint32_t generation;
    LbcValue r[LBC_REGS]; uint32_t pc,stage,used,bulk; int status; bool tick_only;
} LbcInstance;
uint32_t lbc_u32(const uint8_t *p);
bool lbc_open_reader(LbcProgram *p,LbcRead read,void *context,size_t size);
bool lbc_open(LbcProgram *p,const void *bytes,size_t size);
int lbc_validate(LbcProgram *p,unsigned quota);
bool lbc_init(LbcInstance *i,LbcProgram *p,LbcBuffer *buffer);
bool lbc_begin(LbcInstance *i,float time,float delta);
bool lbc_tick(LbcInstance *i,float time,float delta);
bool lbc_render(LbcInstance *i,float time);
int lbc_resume(LbcInstance *i,unsigned quota); /* -1 fault, 0 suspended, 1 complete */
bool lbc_parameter(LbcInstance *i,unsigned reg,uint32_t bits);
uint32_t lbc_hsv(float h,float s,float v);
float lbc_hash01(int32_t index,int32_t seed);
void lbc_native_rainbow(uint8_t *rgb,unsigned start,unsigned end,float phase,float stride);
#endif
