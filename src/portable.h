#ifndef LIGHTVM_PORTABLE_H
#define LIGHTVM_PORTABLE_H
/* LBC1/4: general typed functions/resources, bound here to animation.
 * No guest address is a native pointer. The native caller supplies render timing.
 */
#include "bytecode_core.h"
typedef union { uint32_t u; int32_t i; float f; } PbcValue;
typedef struct { uint32_t first,end,rights,result,arg_start,arg_count; } PbcFunction;
typedef struct { uint32_t kind,length,fill,seed,stream,flags; } PbcResourceSpec;
typedef struct { uint8_t op,type; uint16_t reserved; uint32_t a,b,value; } PbcExpression;
typedef struct { uint32_t binding,argc; uint8_t args[8]; } PbcCall;
typedef struct { uint32_t reg,initial,minimum,maximum; } PbcSetting;
typedef struct {
    uint8_t *data; uint32_t count,kind,generation; bool sealed;
    uint32_t random_state; /* RNG v1, explicitly seeded xorshift32. */
} PbcResource;
typedef struct Portable {
    LbcRead read; void *context;
    uint32_t size,nregs,ncode,ncolors,nfunctions,nresources,nexpressions,ncalls,nsettings;
    /* Binding 3 is animation; binding 4 is a progress-driven transition. */
    uint32_t entries[2],profile;
    uint32_t code_offset,palette_offset,manifest_offset,manifest_size;
    uint8_t *metadata, *descriptors; uint16_t *register_offsets;
    uint32_t functions_offset,specs_offset,expressions_offset,calls_offset,settings_offset;
    bool io_fault;
    uint8_t *values; PbcResource *resources; void *state;
    size_t metadata_bytes,state_bytes,payload_bytes,register_bytes;
    uint32_t generation,pc,function,used,bulk,status;
    PbcValue result;
    LbcBuffer *output;
    char setting_names[16][33];
    char argument_error[33];
    PbcValue overrides[16]; uint32_t override_mask;
    uint64_t now_us,render_count,active_wall_us,random_state;
    uint64_t native_draw_us,dispatch_count,slice_count;
    bool native_pending;
    bool deterministic_random;
    uint64_t default_seed;
    /* Native, read-only RGB views. Never exposed as guest pointers. The owner
       pins these completed scene frames until this evaluation finishes. */
    const uint8_t *scene_frames[2];
    float progress;
    bool initialized;
    _Alignas(4) uint8_t cache[256]; uint32_t cache_offset,cache_size;
} Portable;
const char *pbc_open(Portable **out,LbcRead read,void *context,size_t size);
void pbc_destroy(Portable *p);
const char *pbc_prepare(Portable *p,LbcBuffer *output);
bool pbc_invoke(Portable *p,unsigned function,const PbcValue *arguments,unsigned count);
int pbc_resume(Portable *p,unsigned quota);
unsigned pbc_type(const Portable *p,unsigned reg);
unsigned pbc_role(const Portable *p,unsigned reg);
uint32_t pbc_initial(const Portable *p,unsigned reg);
PbcFunction pbc_function(Portable *, unsigned);
PbcResourceSpec pbc_spec(Portable *, unsigned);
PbcExpression pbc_expression(Portable *, unsigned);
PbcCall pbc_call(Portable *, unsigned);
PbcSetting pbc_setting(Portable *, unsigned);
PbcValue pbc_value(const Portable *, unsigned);
void pbc_set_value(Portable *,unsigned,PbcValue);
bool pbc_read_instruction(Portable *,unsigned,uint8_t out[8]);
#endif
