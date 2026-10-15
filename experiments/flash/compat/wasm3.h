#ifndef LIGHTVM_TOYWASM_ADAPTER_H
#define LIGHTVM_TOYWASM_ADAPTER_H
// Compatibility surface for LightVM's existing bindings. All validation and
// execution is performed by Toywasm, not by an interpreter implemented here.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef const char *M3Result;
typedef struct TwEnvironment *IM3Environment;
typedef struct TwRuntime *IM3Runtime;
typedef struct TwModule *IM3Module;
typedef struct TwFunction *IM3Function;
typedef struct { void *userdata; } M3ImportContext, *IM3ImportContext;
typedef M3Result (*M3RawCall)(IM3Runtime, IM3ImportContext, uint64_t *, void *);
enum M3ValueType { c_m3Type_none, c_m3Type_i32, c_m3Type_i64, c_m3Type_f32, c_m3Type_f64 };
#define m3Err_none ((M3Result)0)
extern const char m3Err_mallocFailed[], m3Err_functionLookupFailed[], m3Err_trapOutOfBoundsMemoryAccess[];
IM3Environment m3_NewEnvironment(void);
void m3_FreeEnvironment(IM3Environment);
IM3Runtime m3_NewRuntime(IM3Environment, uint32_t, void *);
void m3_FreeRuntime(IM3Runtime);
M3Result m3_ParseModule(IM3Environment, IM3Module *, const uint8_t *, uint32_t);
void m3_FreeModule(IM3Module);
M3Result m3_LoadModule(IM3Runtime, IM3Module);
M3Result m3_CompileModule(IM3Module);
M3Result m3_LinkRawFunctionEx(IM3Module, const char *, const char *, const char *, M3RawCall, void *);
M3Result m3_RunStart(IM3Module);
M3Result m3_FindFunction(IM3Function *, IM3Runtime, const char *);
uint32_t m3_GetArgCount(IM3Function);
uint32_t m3_GetRetCount(IM3Function);
enum M3ValueType m3_GetArgType(IM3Function, uint32_t);
enum M3ValueType m3_GetRetType(IM3Function, uint32_t);
M3Result m3_CallV(IM3Function, ...);
M3Result m3_GetResultsV(IM3Function, ...);
uint8_t *m3_GetMemory(IM3Runtime, uint32_t *, uint32_t);
M3Result m3_Yield(void);
#define m3ApiRawFunction(name) M3Result name(IM3Runtime runtime, IM3ImportContext _ctx, uint64_t *_sp, void *_mem)
#define m3ApiGetArg(type, name) type name; memcpy(&name, _sp++, sizeof(type))
#define m3ApiReturnType(type) type *_ret = (type *)_sp++
#define m3ApiReturn(value) do { *_ret = (value); return m3Err_none; } while (0)
#endif
