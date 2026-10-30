#ifndef LIGHTVM_PORTABLE_H
#define LIGHTVM_PORTABLE_H
/* LBC1/4: general typed functions/resources, bound here to animation.
 * No guest address is a native pointer. The native caller supplies render timing.
 */
#include "bytecode_core.h"
#include "inputs.h"
#include "config.h"
#define PBC_MANIFEST_LIMIT 8192u
typedef InputValue PbcValue;
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
    uint8_t *metadata, *descriptors, *initials; uint16_t *register_offsets;
    uint32_t functions_offset,specs_offset,expressions_offset,calls_offset,settings_offset;
    bool io_fault;
    uint8_t *values; PbcResource *resources; void *state;
    size_t metadata_bytes,state_bytes,payload_bytes,register_bytes;
    struct PbcMapPlan *maps; size_t map_bytes;
    uint32_t generation,pc,function,used,bulk,status;
    uint32_t fault_pc,fault_code;
    PbcValue result;
    LbcBuffer *output;
    char setting_names[16][33];
    char argument_error[33];
    PbcValue overrides[16]; uint32_t override_mask;
    InputPort *inputs; unsigned ninputs; uint32_t input_revision;
    InputRegistry *input_registry;
    struct InputSourceScope *source_scope;
    bool source_demand;
    uint64_t input_epoch;
    uint64_t now_us,render_count,active_wall_us,random_state;
    uint64_t native_draw_us,dispatch_count,slice_count;
    bool native_pending;
    bool deterministic_random;
    uint64_t default_seed;
    uint64_t seed_sequence;
    /* Native, read-only RGB views. Never exposed as guest pointers. The owner
       pins these completed composition frames until this evaluation finishes. */
    const uint8_t *composition_frames[2];
    float progress;
    bool initialized;
    bool render_reusable; /* Derived from verified operations, never a manifest claim. */
    _Alignas(4) uint8_t cache[LIGHTVM_BYTECODE_CACHE_BYTES]; uint32_t cache_offset,cache_size;
} Portable;
typedef struct {
    unsigned stage,index,function,pc,previous,loaded;
    PbcFunction current;
    Json *manifest;
    JsonParser parser;
    const char *error;
} PbcLoader;
/* Returns 0 while pending, 1 on completion, -1 on failure. The caller owns
 * *out from its first allocation, including on failure/cancellation. */
int pbc_open_step(PbcLoader *,Portable **out,LbcRead,void *,size_t);
void pbc_open_cancel(PbcLoader *);
const char *pbc_open(Portable **out,LbcRead read,void *context,size_t size);
const char *pbc_manifest_decode(Portable *,const Json *);
void pbc_destroy(Portable *p);
typedef struct {
    unsigned stage,index,pixel;
    unsigned map_pc,map_index;
    size_t resource_offset,payload,zeroed,cursor;
    PbcValue fill;
    bool image;
    const char *error;
} PbcPreparation;
int pbc_prepare_step(PbcPreparation *,Portable *,LbcBuffer *);
const char *pbc_prepare(Portable *p,LbcBuffer *output);
bool pbc_invoke(Portable *p,unsigned function,const PbcValue *arguments,unsigned count);
int pbc_resume(Portable *p,unsigned quota);
enum { PBC_FAULT_OPERATION=1, PBC_FAULT_BUDGET, PBC_FAULT_RESOURCE };
/* Source maps live beside the program in the editor, not in controller RAM. */
const char *pbc_fault_reason(Portable *);
const char *pbc_fault_message(Portable *,char *,size_t);
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
bool pbc_render_reusable(Portable *);
#endif
