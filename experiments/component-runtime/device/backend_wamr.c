#include "backend.h"
#include "wasm_export.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

/* The core retains the entire instance while RMT reads this instance's one RGB
 * allocation. Guest entry is blocked when sealed. This is a trusted generated
 * AOT contract, not revocation of raw stores by arbitrary guest machine code. */
struct CrModule { wasm_module_t module; unsigned instances; };
struct CrInstance {
    CrModule *module;
    wasm_module_inst_t instance;
    wasm_exec_env_t environment;
    wasm_function_inst_t configure, prepare, pixels, begin, step;
    uint8_t *rgb;
    bool sealed, prepared, begun;
};
const char *cr_backend(void) { return "wamr-aot"; }
bool cr_runtime_init(void) {
    RuntimeInitArgs args={0}; args.mem_alloc_type=Alloc_With_System_Allocator;
    return wasm_runtime_full_init(&args);
}
CrModule *cr_load(uint8_t *bytes,size_t size,char *error,size_t capacity) {
    if(size>UINT32_MAX || !capacity || !bytes ||
       wasm_runtime_get_file_package_type(bytes,(uint32_t)size)!=Wasm_Module_AoT) {
        if(capacity) snprintf(error,capacity,"Expected WAMR AOT package");
        return NULL;
    }
    CrModule *m=calloc(1,sizeof(*m));
    if(!m) { snprintf(error,capacity,"Module wrapper allocation failed"); return NULL; }
    m->module=wasm_runtime_load(bytes,(uint32_t)size,error,(uint32_t)capacity);
    if(!m->module) { free(m); return NULL; }
    return m;
}
void cr_unload(CrModule *m) {
    if(!m || m->instances) return;
    wasm_runtime_unload(m->module); free(m);
}
static int invoke(CrInstance *i,wasm_function_inst_t function,unsigned argc,uint32_t input) {
    uint32_t args[1]={input};
    if(!function || !wasm_runtime_call_wasm(i->environment,function,argc,args)) return -1;
    return (int)args[0];
}
CrInstance *cr_create(CrModule *m,uint32_t seed,uint32_t pixels) {
    if(!m || pixels!=200) return NULL;
    CrInstance *i=calloc(1,sizeof(*i)); if(!i) return NULL;
    char error[128]; i->module=m;
    i->instance=wasm_runtime_instantiate(m->module,2048,0,error,sizeof(error));
    if(!i->instance) { printf("WAMR instantiate: %s\n",error); free(i); return NULL; }
    i->environment=wasm_runtime_create_exec_env(i->instance,2048);
    if(!i->environment) goto fail;
    /* Upstream ESP port returns NULL when FreeRTOS tracing is disabled.
     * Use IDF's always-available stack base so AOT stack checks remain real. */
    uint8_t *stack_base=pxTaskGetStackStart(NULL);
    if(!stack_base) goto fail;
    wasm_runtime_set_native_stack_boundary(i->environment,stack_base+2048);
    i->configure=wasm_runtime_lookup_function(i->instance,"configure");
    i->prepare=wasm_runtime_lookup_function(i->instance,"prepare");
    i->pixels=wasm_runtime_lookup_function(i->instance,"pixels");
    i->begin=wasm_runtime_lookup_function(i->instance,"begin");
    i->step=wasm_runtime_lookup_function(i->instance,"step");
    if(!i->configure || !i->prepare || !i->pixels || !i->begin || !i->step || invoke(i,i->configure,1,seed)!=1) goto fail;
    int offset=invoke(i,i->pixels,0,0);
    if(offset<0 || !wasm_runtime_validate_app_addr(i->instance,(uint32_t)offset,600)) goto fail;
    i->rgb=wasm_runtime_addr_app_to_native(i->instance,(uint32_t)offset);
    if(!i->rgb) goto fail;
    m->instances++; return i;
fail:
    if(i->environment) wasm_runtime_destroy_exec_env(i->environment);
    wasm_runtime_deinstantiate(i->instance); free(i); return NULL;
}
int cr_prepare(CrInstance *i) {
    if(!i || i->sealed) return -1;
    if(i->prepared) return 1;
    int r=invoke(i,i->prepare,0,0); if(r==1) i->prepared=true; return r;
}
int cr_begin(CrInstance *i,uint32_t frame) {
    if(!i || i->sealed || !i->prepared) return -1;
    if(invoke(i,i->begin,1,frame)!=1) return -1;
    i->begun=true; return 0;
}
int cr_step(CrInstance *i) {
    if(!i || i->sealed || !i->begun) return -1;
    return invoke(i,i->step,0,0);
}
uint8_t *cr_pixels(CrInstance *i) { return i ? i->rgb : NULL; }
void cr_seal(CrInstance *i,bool sealed) { if(i) i->sealed=sealed; }
void cr_destroy(CrInstance *i) {
    if(!i || i->sealed) return;
    wasm_runtime_destroy_exec_env(i->environment); wasm_runtime_deinstantiate(i->instance);
    i->module->instances--; free(i);
}
