#include "app.h"
#include "vm_allocator.h"
#include <stdio.h>
#include <string.h>

static App *executing;
static const char timeout_error[]="execution deadline exceeded";
static const char output_error[]="console output limit exceeded";
static const char exit_trap[]="guest requested exit";

M3Result m3_Yield(void) {
    return executing && platform_time_us()>=executing->deadline ? timeout_error : m3Err_none;
}
static void reply(App *a, uint16_t id, enum Status code, int32_t value, const char *message) {
    send_status(a->send,a->transport,id,code,value,message);
}
void app_init(App *a, SendMessage send, void *transport) {
    memset(a,0,sizeof(*a)); a->send=send; a->transport=transport;
}
void app_destroy(App *a) {
    if (a->runtime) m3_FreeRuntime(a->runtime);
    if (a->environment) m3_FreeEnvironment(a->environment);
    a->runtime=NULL; a->environment=NULL; a->module=NULL; a->ready=false;
}
static bool memory_range(IM3Runtime runtime, uint32_t offset, uint32_t size, uint8_t **out) {
    uint32_t available=0;
    uint8_t *memory=m3_GetMemory(runtime,&available,0);
    if (!memory || offset>available || size>available-offset) return false;
    *out=memory+offset; return true;
}
static M3Result console_bytes(App *a, const uint8_t *p, size_t size) {
    if (size>OUTPUT_LIMIT-a->output_size) return output_error;
    a->output_size+=size;
    while (size) {
        size_t chunk=size>FRAME_LIMIT ? FRAME_LIMIT : size;
        a->send(a->transport,RESP_OUTPUT,a->request_id,p,chunk);
        p+=chunk; size-=chunk;
    }
    return m3_Yield();
}
m3ApiRawFunction(console_write) {
    (void)_mem;
    m3ApiGetArg(uint32_t,offset);
    m3ApiGetArg(uint32_t,size);
    uint8_t *bytes;
    if (!memory_range(runtime,offset,size,&bytes)) return m3Err_trapOutOfBoundsMemoryAccess;
    return console_bytes((App *)_ctx->userdata,bytes,size);
}
m3ApiRawFunction(wasi_fd_write) {
    (void)_mem;
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t,fd);
    m3ApiGetArg(uint32_t,iovs);
    m3ApiGetArg(uint32_t,count);
    m3ApiGetArg(uint32_t,written);
    App *a=(App *)_ctx->userdata;
    uint8_t *vectors,*result;
    if (fd!=1 && fd!=2) m3ApiReturn(8); /* WASI EBADF */
    if (count>64 || !memory_range(runtime,iovs,count*8,&vectors) ||
        !memory_range(runtime,written,4,&result)) m3ApiReturn(21); /* EFAULT */
    size_t total=0;
    for (uint32_t i=0; i<count; ++i) {
        uint8_t *bytes;
        uint32_t size=read_u32(vectors+i*8+4);
        if (!memory_range(runtime,read_u32(vectors+i*8),size,&bytes)) m3ApiReturn(21);
        if (size>OUTPUT_LIMIT-a->output_size-total) return output_error;
        total+=size;
    }
    for (uint32_t i=0; i<count; ++i) {
        uint8_t *bytes;
        uint32_t size=read_u32(vectors+i*8+4);
        if (!memory_range(runtime,read_u32(vectors+i*8),size,&bytes)) m3ApiReturn(21);
        M3Result error=console_bytes(a,bytes,size);
        if (error) return error;
    }
    write_u32(result,(uint32_t)total);
    m3ApiReturn(0);
}
m3ApiRawFunction(wasi_proc_exit) {
    (void)runtime; (void)_mem;
    m3ApiGetArg(int32_t,code);
    App *a=(App *)_ctx->userdata;
    a->guest_exit=true; a->exit_code=code;
    return exit_trap;
}
static M3Result link_optional(App *a, const char *space, const char *name,
                              const char *signature, M3RawCall fn) {
    M3Result result=m3_LinkRawFunctionEx(a->module,space,name,signature,fn,a);
    return result==m3Err_functionLookupFailed ? m3Err_none : result;
}
static M3Result load(App *a) {
    a->environment=m3_NewEnvironment();
    if (!a->environment) return m3Err_mallocFailed;
    a->runtime=m3_NewRuntime(a->environment,8192,a);
    if (!a->runtime) return m3Err_mallocFailed;
    M3Result result=m3_ParseModule(a->environment,&a->module,a->bytes,(uint32_t)a->expected);
    if (result) return result;
    result=m3_LoadModule(a->runtime,a->module);
    if (result) { m3_FreeModule(a->module); a->module=NULL; return result; }
    result=link_optional(a,"console","write","v(ii)",console_write);
    if (!result) result=link_optional(a,"wasi_snapshot_preview1","fd_write","i(iiii)",wasi_fd_write);
    if (!result) result=link_optional(a,"wasi_snapshot_preview1","proc_exit","v(i)",wasi_proc_exit);
    if (!result) result=m3_CompileModule(a->module);
    return result;
}
static void run(App *a, uint16_t id, const uint8_t *payload, size_t size) {
    if (!a->ready) { reply(a,id,STATUS_RUN,0,"no loaded module"); return; }
    char name[64]="run";
    if (size) {
        if (size>=sizeof(name) || memchr(payload,0,size)) {
            reply(a,id,STATUS_PROTOCOL,0,"invalid export name"); return;
        }
        memcpy(name,payload,size); name[size]=0;
    }
    a->request_id=id; a->output_size=0; a->guest_exit=false; a->exit_code=0;
    a->deadline=platform_time_us()+RUN_TIMEOUT_US; executing=a;
    // wasm3 may also invoke the start function during export lookup. Run it
    // explicitly here so all guest initialization shares this request's limits.
    M3Result error=m3_RunStart(a->module);
    IM3Function fn=NULL;
    if (!error) error=m3_FindFunction(&fn,a->runtime,name);
    uint32_t returns=0;
    if (!error) {
        returns=m3_GetRetCount(fn);
        if (m3_GetArgCount(fn)!=0 || returns>1 || (returns && m3_GetRetType(fn,0)!=c_m3Type_i32))
            error="export must take no arguments and return void or i32";
    }
    if (!error) error=m3_CallV(fn);
    int32_t value=0;
    if (!error && returns) error=m3_GetResultsV(fn,&value);
    executing=NULL;
    if (a->guest_exit && error==exit_trap)
        reply(a,id,a->exit_code ? STATUS_EXIT : STATUS_OK,a->exit_code,"guest exited");
    else if (error)
        reply(a,id,(error==timeout_error || error==output_error) ? STATUS_LIMIT : STATUS_RUN,0,error);
    else reply(a,id,STATUS_OK,value,"executed");
}
void app_command(void *context, uint8_t type, uint16_t id, const uint8_t *p, size_t size) {
    App *a=context;
    switch (type) {
    case CMD_STATUS: {
        if (size) { reply(a,id,STATUS_PROTOCOL,0,"STATUS takes no payload"); break; }
        char message[220];
        snprintf(message,sizeof(message),"ready=%u uploading=%u received=%lu module_limit=%u heap=%lu heap_peak=%lu heap_limit=%u linear_limit=65536",
                 a->ready,a->uploading,(unsigned long)a->received,MODULE_LIMIT,
                 (unsigned long)vm_heap_used(),(unsigned long)vm_heap_peak(),VM_HEAP_LIMIT);
        reply(a,id,STATUS_OK,0,message); break;
    }
    case CMD_BEGIN: {
        if (size!=8 || read_u32(p)<8 || read_u32(p)>MODULE_LIMIT) {
            reply(a,id,STATUS_UPLOAD,0,"module size must be 8..32768 bytes"); break;
        }
        app_destroy(a); a->received=0; a->expected=read_u32(p);
        a->expected_crc=read_u32(p+4); a->uploading=true;
        reply(a,id,STATUS_OK,0,"upload started"); break;
    }
    case CMD_CHUNK: {
        if (!a->uploading || size<=4) { reply(a,id,STATUS_UPLOAD,0,"no upload or empty chunk"); break; }
        uint32_t offset=read_u32(p);
        size_t count=size-4;
        if (offset<=a->received && count<=a->received-offset && !memcmp(a->bytes+offset,p+4,count)) {
            reply(a,id,STATUS_OK,(int32_t)a->received,"duplicate chunk"); break;
        }
        if (offset!=a->received || count>a->expected-a->received) {
            reply(a,id,STATUS_UPLOAD,(int32_t)a->received,"unexpected chunk offset or size"); break;
        }
        memcpy(a->bytes+a->received,p+4,count); a->received+=count;
        reply(a,id,STATUS_OK,(int32_t)a->received,"chunk accepted"); break;
    }
    case CMD_FINISH: {
        if (size || !a->uploading || a->received!=a->expected) {
            reply(a,id,STATUS_UPLOAD,(int32_t)a->received,"upload incomplete"); break;
        }
        a->uploading=false;
        if (crc32_bytes(a->bytes,a->expected)!=a->expected_crc) {
            reply(a,id,STATUS_UPLOAD,0,"module checksum mismatch"); break;
        }
        M3Result error=load(a);
        if (error) { reply(a,id,STATUS_LOAD,0,error); app_destroy(a); }
        else { a->ready=true; reply(a,id,STATUS_OK,(int32_t)a->received,"module loaded"); }
        break;
    }
    case CMD_RUN: run(a,id,p,size); break;
    case CMD_RESET:
        if (size) { reply(a,id,STATUS_PROTOCOL,0,"RESET takes no payload"); break; }
        app_destroy(a); a->uploading=false; a->received=0; a->expected=0;
        reply(a,id,STATUS_OK,0,"reset"); break;
    default: reply(a,id,STATUS_PROTOCOL,0,"unknown command"); break;
    }
}
