#ifndef LIGHTVM_PORTABLE_H
#define LIGHTVM_PORTABLE_H
/* LBC1/3: general typed functions/resources, bound here to animation/1.
 * No guest address is a native pointer. No animation completion ends a component.
 */
#include "bytecode.h"
typedef union { uint64_t stamp; uint32_t u; int32_t i; float f; } PbcValue;
typedef struct { uint32_t first,end,rights,result,arg_start,arg_count; } PbcFunction;
typedef struct { uint32_t kind,length,fill,seed,stream,flags; } PbcResourceSpec;
typedef struct { uint8_t op,type; uint16_t reserved; uint32_t a,b,value; } PbcExpression;
typedef struct { uint32_t binding,argc; uint8_t args[4]; uint32_t reserved; } PbcCall;
typedef struct { uint32_t reg,initial,minimum,maximum; } PbcSetting;
typedef struct {
    uint8_t *data; uint32_t count,kind,generation; bool sealed;
    uint32_t random_state; /* RNG v1, explicitly seeded xorshift32. */
} PbcResource;
typedef struct Portable {
    LbcRead read; void *context;
    uint32_t size,nregs,ncode,ncolors,nfunctions,nresources,nexpressions,ncalls,nsettings;
    uint32_t entries[4],mode,period_expression,max_catch_up,first,profile;
    uint32_t code_offset,palette_offset,manifest_offset,manifest_size;
    uint8_t *metadata, *descriptors;
    PbcFunction *functions; PbcResourceSpec *specs; PbcExpression *expressions;
    PbcCall *calls; PbcSetting *settings;
    PbcValue *values; PbcResource *resources; void *state;
    size_t metadata_bytes,state_bytes,payload_bytes;
    uint32_t generation,pc,function,used,bulk,status;
    PbcValue result;
    LbcBuffer *output;
    char setting_names[16][33];
    PbcValue overrides[16]; uint32_t override_mask;
    uint64_t now_us,period_us,credit_us,ticks,cycles,pending_cycles,completion_time_us;
    uint64_t active_wall_us,dropped_ticks,cancelled_ticks,random_exhaustions;
    bool completion_pending,completed,initialized;
    _Alignas(4) uint8_t cache[256]; uint32_t cache_offset,cache_size;
} Portable;
const char *pbc_open(Portable **out,LbcRead read,void *context,size_t size);
void pbc_destroy(Portable *p);
const char *pbc_period(Portable *p,unsigned count,uint64_t *period_us);
const char *pbc_prepare(Portable *p,LbcBuffer *output);
bool pbc_invoke(Portable *p,unsigned function,const PbcValue *arguments,unsigned count);
int pbc_resume(Portable *p,unsigned quota);
bool pbc_parameter(Portable *p,unsigned reg,uint32_t bits);
unsigned pbc_type(const Portable *p,unsigned reg);
unsigned pbc_role(const Portable *p,unsigned reg);
uint32_t pbc_initial(const Portable *p,unsigned reg);
bool pbc_random_index(uint32_t bound,uint32_t (*word)(void *),void *context,int32_t *result);
#endif
