#include "wasm3.h"
#include "vm_allocator.h"
#include "cell.h"
#include "exec_context.h"
#include "instance.h"
#include "load_context.h"
#include "mem.h"
#include "module.h"
#include "type.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

const char m3Err_mallocFailed[] = "memory allocation failed";
const char m3Err_functionLookupFailed[] = "function lookup failed";
const char m3Err_trapOutOfBoundsMemoryAccess[] = "out of bounds memory access";
static char diagnostic[256];
static M3Result host_error;
struct TwEnvironment { unsigned unused; };
struct TwFunction { IM3Module module; uint32_t index; struct val result; };
struct Binding { struct funcinst func; IM3Module module; M3RawCall callback; M3ImportContext context; };
struct TwModule {
    struct mem_context memory;
    struct module *module;
    struct instance *instance;
    struct exec_context exec;
    struct import_object *imports;
    struct Binding *bindings;
    struct TwFunction *exports;
    IM3Runtime runtime;
    bool context_ready, started;
};
struct TwRuntime { IM3Module module; uint32_t stack_size; };

void *lightvm_toy_malloc(size_t size) { return demo_calloc(1,size); }
int lightvm_toy_vasprintf(char **out,const char *format,va_list ap) {
    va_list copy; va_copy(copy,ap); int size=vsnprintf(NULL,0,format,copy); va_end(copy);
    if(size<0 || size>4096) { *out=NULL; return -1; }
    *out=demo_calloc(1,(size_t)size+1); if(!*out) return -1;
    return vsnprintf(*out,(size_t)size+1,format,ap);
}
// Diagnostic output is not used on the wire-protocol owner.
int nbio_vfprintf(FILE *f,const char *fmt,va_list ap) { (void)f; (void)fmt; (void)ap; return 0; }
int nbio_fprintf(FILE *f,const char *fmt,...) { (void)f; (void)fmt; return 0; }
int nbio_printf(const char *fmt,...) { (void)fmt; return 0; }
int lightvm_toy_interrupt(void) { host_error=m3_Yield(); return host_error!=NULL; }
static M3Result failure(int error, const struct report *report) {
    if (!error) return NULL;
    if (host_error) return host_error;
    if (error==ENOMEM) return m3Err_mallocFailed;
    snprintf(diagnostic,sizeof(diagnostic),"Toywasm: %s (%d)",report?report_getmessage(report):"runtime error",error);
    return diagnostic;
}
IM3Environment m3_NewEnvironment(void) { return demo_calloc(1,sizeof(struct TwEnvironment)); }
void m3_FreeEnvironment(IM3Environment env) { demo_free(env); }
IM3Runtime m3_NewRuntime(IM3Environment env,uint32_t stack,void *userdata) {
    (void)env; (void)userdata;
    IM3Runtime r=demo_calloc(1,sizeof(*r)); if (r) r->stack_size=stack; return r;
}
void m3_FreeModule(IM3Module m) {
    if (!m) return;
    if (m->context_ready) exec_context_clear(&m->exec);
    if (m->instance) instance_destroy(m->instance);
    if (m->imports) import_object_destroy(&m->memory,m->imports);
    if (m->module) module_destroy(&m->memory,m->module);
    demo_free(m->bindings); demo_free(m->exports); demo_free(m);
}
void m3_FreeRuntime(IM3Runtime r) { if (r) { m3_FreeModule(r->module); demo_free(r); } }
M3Result m3_ParseModule(IM3Environment env,IM3Module *out,const uint8_t *bytes,uint32_t size) {
    (void)env; *out=NULL; host_error=NULL;
    IM3Module m=demo_calloc(1,sizeof(*m)); if (!m) return m3Err_mallocFailed;
    mem_context_init(&m->memory);
    struct load_context ctx; load_context_init(&ctx,&m->memory);
    int ret=module_create(&m->module,bytes,bytes+size,&ctx);
    M3Result error=failure(ret,&ctx.report); load_context_clear(&ctx);
    if (!error) {
        struct module *m0=m->module;
        if (m0->nimportedmems || m0->nmems>1) error="unsupported memory count/import";
        if (m0->nmems) {
            if (m0->mems[0].lim.min>1) error="linear memory limit exceeded";
            if (m0->mems[0].lim.max>1) m0->mems[0].lim.max=1;
        }
        if (m0->nimports!=m0->nimportedfuncs) error="only function imports are supported";
        for (uint32_t i=0;!error && i<m0->ntables;i++)
            if (m0->tables[i].lim.min>4096) error="table limit exceeded";
        if (!error && m0->nimports) {
            m->bindings=demo_calloc(m0->nimports,sizeof(*m->bindings));
            ret=import_object_alloc(&m->memory,m0->nimports,&m->imports);
            if (!m->bindings || ret) error=m3Err_mallocFailed;
        }
        if (!error && m0->nexports) {
            m->exports=demo_calloc(m0->nexports,sizeof(*m->exports));
            if (!m->exports) error=m3Err_mallocFailed;
        }
    }
    if (error) m3_FreeModule(m); else *out=m;
    return error;
}
M3Result m3_LoadModule(IM3Runtime r,IM3Module m) { r->module=m; m->runtime=r; return NULL; }
static void reset_context(IM3Module m) {
    if (m->context_ready) exec_context_clear(&m->exec);
    exec_context_init(&m->exec,m->instance,&m->memory); m->context_ready=true;
    m->exec.options.max_frames=128;
    m->exec.options.max_stackcells=m->runtime->stack_size/sizeof(struct cell);
    m->exec.check_interval=32;
}
static int host_call(struct exec_context *ctx,struct host_instance *opaque,const struct functype *ft,const struct cell *params,struct cell *results) {
    (void)ctx;
    struct Binding *b=(struct Binding *)opaque;
    if (ft->parameter.ntypes>8 || ft->result.ntypes>1) return EINVAL;
    struct val values[8]; uint64_t stack[9]={0};
    vals_from_cells(values,params,&ft->parameter);
    uint32_t base=ft->result.ntypes;
    for (uint32_t i=0;i<ft->parameter.ntypes;i++) memcpy(&stack[base+i],&values[i].u,sizeof(uint64_t));
    host_error=b->callback(b->module->runtime,&b->context,stack,NULL);
    if (host_error) return ETOYWASMTRAP;
    if (base) { struct val value={0}; memcpy(&value.u,stack,sizeof(uint64_t)); vals_to_cells(&value,results,&ft->result); }
    return 0;
}
static char type_code(enum valtype t) {
    switch(t) { case TYPE_i32:return 'i'; case TYPE_i64:return 'I'; case TYPE_f32:return 'f'; case TYPE_f64:return 'F'; default:return '?'; }
}
M3Result m3_LinkRawFunctionEx(IM3Module m,const char *space,const char *name,const char *sig,M3RawCall fn,void *userdata) {
    bool found=false;
    for (uint32_t i=0;i<m->module->nimports;i++) {
        struct import *im=&m->module->imports[i];
        if (strlen(space)!=im->module_name.nbytes || memcmp(space,im->module_name.data,im->module_name.nbytes) ||
            strlen(name)!=im->name.nbytes || memcmp(name,im->name.data,im->name.nbytes)) continue;
        found=true;
        const struct functype *ft=&m->module->types[im->desc.u.typeidx];
        if (ft->parameter.ntypes>8 || ft->result.ntypes>1) return "unsupported import signature";
        char expected[16]; uint32_t n=0;
        expected[n++]=ft->result.ntypes?type_code(ft->result.types[0]):'v'; expected[n++]='(';
        for(uint32_t a=0;a<ft->parameter.ntypes;a++) expected[n++]=type_code(ft->parameter.types[a]);
        expected[n++]=')'; expected[n]=0;
        if (strcmp(sig,expected)) return "import signature mismatch";
        struct Binding *b=&m->bindings[i]; b->module=m; b->callback=fn; b->context.userdata=userdata;
        b->func.is_host=true; b->func.u.host.instance=(struct host_instance *)b; b->func.u.host.type=ft; b->func.u.host.func=host_call;
        m->imports->entries[i]=(struct import_object_entry){.module_name=&im->module_name,.name=&im->name,.type=EXTERNTYPE_FUNC,.u.func=&b->func};
    }
    return found?NULL:m3Err_functionLookupFailed;
}
M3Result m3_CompileModule(IM3Module m) {
    // The compatibility name means finish linking; Toywasm never translates code.
    host_error=NULL;
    for(uint32_t i=0;i<m->module->nimports;i++) if (!m->bindings[i].callback) return "unresolved import";
    struct report report; report_init(&report);
    int ret=instance_create_no_init(&m->memory,m->module,&m->instance,m->imports,&report);
    M3Result error=failure(ret,&report); report_clear(&report);
    if (error) return error;
    reset_context(m);
    // Initialize data/elements now; defer the start function until RUN/PLAY.
    bool start=m->module->has_start; m->module->has_start=false;
    ret=instance_execute_init(&m->exec);
    while (ret==ETOYWASMRESTART) ret=instance_execute_continue(&m->exec);
    m->module->has_start=start;
    return failure(ret,m->exec.report);
}
static M3Result invoke_function(IM3Module m,uint32_t idx,const struct val *args,struct val *result) {
    host_error=NULL;
    const struct functype *ft=module_functype(m->module,idx);
    int ret=exec_push_vals(&m->exec,&ft->parameter,args);
    if (!ret) ret=instance_execute_func(&m->exec,idx,&ft->parameter,&ft->result);
    while (ret==ETOYWASMRESTART) ret=instance_execute_continue(&m->exec);
    M3Result error=failure(ret,m->exec.report);
    if (!error) exec_pop_vals(&m->exec,&ft->result,result);
    else reset_context(m);
    return error;
}
M3Result m3_RunStart(IM3Module m) {
    if (m->started) return NULL;
    m->started=true;
    return m->module->has_start?invoke_function(m,m->module->start,NULL,NULL):NULL;
}
M3Result m3_FindFunction(IM3Function *out,IM3Runtime r,const char *name) {
    *out=NULL;
    M3Result e=m3_RunStart(r->module); if (e) return e;
    IM3Module m=r->module;
    for(uint32_t i=0;i<m->module->nexports;i++) {
        struct wasm_export *ex=&m->module->exports[i];
        if (ex->desc.type==EXTERNTYPE_FUNC && strlen(name)==ex->name.nbytes && !memcmp(name,ex->name.data,ex->name.nbytes)) {
            m->exports[i].module=m; m->exports[i].index=ex->desc.idx; *out=&m->exports[i]; return NULL;
        }
    }
    return m3Err_functionLookupFailed;
}
static const struct functype *function_type(IM3Function f) { return module_functype(f->module->module,f->index); }
uint32_t m3_GetArgCount(IM3Function f) { return function_type(f)->parameter.ntypes; }
uint32_t m3_GetRetCount(IM3Function f) { return function_type(f)->result.ntypes; }
static enum M3ValueType convert_type(enum valtype t) {
    switch(t) { case TYPE_i32:return c_m3Type_i32; case TYPE_i64:return c_m3Type_i64; case TYPE_f32:return c_m3Type_f32; case TYPE_f64:return c_m3Type_f64; default:return c_m3Type_none; }
}
enum M3ValueType m3_GetArgType(IM3Function f,uint32_t n) { return convert_type(function_type(f)->parameter.types[n]); }
enum M3ValueType m3_GetRetType(IM3Function f,uint32_t n) { return convert_type(function_type(f)->result.types[n]); }
M3Result m3_CallV(IM3Function f,...) {
    const struct functype *ft=function_type(f);
    if(ft->parameter.ntypes>8 || ft->result.ntypes>1) return "unsupported export signature";
    struct val args[8]={0}; va_list ap; va_start(ap,f);
    for(uint32_t i=0;i<ft->parameter.ntypes;i++) switch(ft->parameter.types[i]) {
        case TYPE_i32:args[i].u.i32=va_arg(ap,uint32_t);break;
        case TYPE_i64:args[i].u.i64=va_arg(ap,uint64_t);break;
        case TYPE_f32:args[i].u.f32=(float)va_arg(ap,double);break;
        case TYPE_f64:args[i].u.f64=va_arg(ap,double);break;
        default:va_end(ap);return "unsupported argument type";
    }
    va_end(ap); return invoke_function(f->module,f->index,args,&f->result);
}
M3Result m3_GetResultsV(IM3Function f,...) {
    if (!m3_GetRetCount(f)) return NULL;
    va_list ap; va_start(ap,f); void *p=va_arg(ap,void *); va_end(ap);
    enum M3ValueType t=m3_GetRetType(f,0);
    memcpy(p,&f->result.u,t==c_m3Type_i64 || t==c_m3Type_f64?8:4); return NULL;
}
uint8_t *m3_GetMemory(IM3Runtime r,uint32_t *size,uint32_t index) {
    *size=0;
    if (!r->module->instance || index>=r->module->instance->mems.lsize) return NULL;
    struct meminst *mem=VEC_ELEM(r->module->instance->mems,index);
    uint32_t length=mem->size_in_pages*65536u;
    void *p=NULL; bool moved=false;
    if (memory_instance_getptr2(mem,0,0,length,&p,&moved)) return NULL;
    *size=length; return p;
}
