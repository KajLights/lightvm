#include "service.h"
#include "vm_allocator.h"
#include "json.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static bool range(IM3Runtime r,uint32_t offset,uint32_t size,uint8_t **p) {
    uint32_t available=0; uint8_t *memory=m3_GetMemory(r,&available,0);
    if (!memory || offset>available || size>available-offset) return false;
    *p=memory+offset; return true;
}
static void close_connection(Service *s) { platform_tcp_close(&s->tcp); s->handle=0; }
static void fault(Service *s,const char *error) {
    close_connection(s); s->pending=false; s->running=false; s->faulted=true;
    snprintf(s->error,sizeof(s->error),"%s",error);
}
m3ApiRawFunction(tcp_connect_guest) {
    (void)runtime; (void)_mem; m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t,ip); m3ApiGetArg(uint32_t,port);
    Service *s=_ctx->userdata;
    if (!port || port>65535 || s->handle || s->handle_sequence==INT32_MAX) m3ApiReturn(-1);
    if (platform_tcp_connect(&s->tcp,ip,(uint16_t)port)<0) m3ApiReturn(-1);
    s->handle=++s->handle_sequence; m3ApiReturn((int32_t)s->handle);
}
m3ApiRawFunction(tcp_state_guest) {
    (void)runtime; (void)_mem; m3ApiReturnType(int32_t); m3ApiGetArg(uint32_t,handle);
    Service *s=_ctx->userdata;
    m3ApiReturn(handle && handle==s->handle?platform_tcp_state(&s->tcp):-1);
}
m3ApiRawFunction(tcp_close_guest) {
    (void)runtime; (void)_mem; m3ApiReturnType(int32_t); m3ApiGetArg(uint32_t,handle);
    Service *s=_ctx->userdata;
    if (!handle || handle!=s->handle) m3ApiReturn(-1);
    close_connection(s); m3ApiReturn(0);
}
static M3Result io(IM3Runtime r,Service *s,uint32_t handle,uint32_t offset,uint32_t size,bool write,int32_t *result) {
    uint8_t *p;
    if (size>2048 || !range(r,offset,size,&p)) return m3Err_trapOutOfBoundsMemoryAccess;
    if (!handle || handle!=s->handle) { *result=-1; return m3Err_none; }
    if (size>s->io_budget) size=s->io_budget;
    if (!size) { *result=0; return m3Err_none; }
    *result=write?platform_tcp_write(&s->tcp,p,size):platform_tcp_read(&s->tcp,p,size);
    if (*result>0) s->io_budget-=(uint32_t)*result;
    return m3Err_none;
}
m3ApiRawFunction(tcp_read_guest) {
    (void)_mem; m3ApiReturnType(int32_t); m3ApiGetArg(uint32_t,h); m3ApiGetArg(uint32_t,o); m3ApiGetArg(uint32_t,n);
    int32_t result=0; M3Result error=io(runtime,_ctx->userdata,h,o,n,false,&result);
    if (error) return error;
    m3ApiReturn(result);
}
m3ApiRawFunction(tcp_write_guest) {
    (void)_mem; m3ApiReturnType(int32_t); m3ApiGetArg(uint32_t,h); m3ApiGetArg(uint32_t,o); m3ApiGetArg(uint32_t,n);
    int32_t result=0; M3Result error=io(runtime,_ctx->userdata,h,o,n,true,&result);
    if (error) return error;
    m3ApiReturn(result);
}
m3ApiRawFunction(time_guest) {
    (void)runtime; (void)_ctx; (void)_mem; m3ApiReturnType(uint64_t); m3ApiReturn(platform_time_us()/1000u);
}
m3ApiRawFunction(identity_guest) {
    (void)_ctx; (void)_mem; m3ApiReturnType(int32_t); m3ApiGetArg(uint32_t,o); m3ApiGetArg(uint32_t,n);
    uint8_t *p; if (n>65 || !range(runtime,o,n,&p)) return m3Err_trapOutOfBoundsMemoryAccess;
    char device[65],boot[33]; platform_identity(device,sizeof(device),boot,sizeof(boot));
    size_t length=strlen(device); if (n<=length) m3ApiReturn(-1);
    memcpy(p,device,length+1); m3ApiReturn((int32_t)length);
}
m3ApiRawFunction(animation_state_guest) {
    (void)_mem; m3ApiReturnType(int32_t); m3ApiGetArg(uint32_t,o); m3ApiGetArg(uint32_t,n);
    uint8_t *p; if (n!=40 || !range(runtime,o,n,&p)) return m3Err_trapOutOfBoundsMemoryAccess;
    Service *s=_ctx->userdata; VmInstance *a=s->animation; double speed=0;
    for (uint32_t i=0;i<a->control.parameter_count;++i)
        if (!strcmp(a->control.parameters[i].name,"speed") && a->control.parameters[i].kind==PARAM_FLOAT) speed=a->control.parameters[i].value.real;
    write_u32(p,a->generation); write_u32(p+4,a->control.parameter_revision); memcpy(p+8,&speed,8);
    write_u32(p+16,a->frame_count); write_u32(p+20,(uint32_t)vm_heap_used()); write_u32(p+24,a->render_us);
    write_u32(p+28,s->command_result); write_u32(p+32,s->commands); write_u32(p+36,s->commands_rejected);
    m3ApiReturn(0);
}
m3ApiRawFunction(speed_set_guest) {
    (void)runtime; (void)_mem; m3ApiReturnType(int32_t); m3ApiGetArg(double,speed);
    m3ApiGetArg(uint32_t,generation); m3ApiGetArg(uint32_t,revision);
    Service *s=_ctx->userdata; if (s->pending) m3ApiReturn(-2);
    s->pending=true; s->pending_speed=speed; s->pending_generation=generation; s->pending_revision=revision;
    s->pending_at=platform_time_us(); m3ApiReturn(0);
}
void service_init(Service *s,VmInstance *animation) { memset(s,0,sizeof(*s)); s->animation=animation; s->tcp.fd=-1; s->tcp.state=-1; }
void service_unload(Service *s) {
    close_connection(s); s->running=false; s->pending=false;
    if (s->runtime) m3_FreeRuntime(s->runtime);
    if (s->environment) m3_FreeEnvironment(s->environment);
    #if LIGHTVM_FLASH_EXPERIMENT
    program_unmap(&s->code_mapping);
    #else
    demo_free(s->bytes);
    #endif
    s->bytes=NULL; s->size=0; s->runtime=NULL; s->environment=NULL; s->module=NULL;
    s->init=NULL; s->step=NULL; s->loaded=false; s->faulted=false; s->error[0]=0;
}
static M3Result link(Service *s,const char *name,const char *signature,M3RawCall fn) {
    M3Result e=m3_LinkRawFunctionEx(s->module,SERVICE_NAMESPACE,name,signature,fn,s);
    return e==m3Err_functionLookupFailed?m3Err_none:e;
}
M3Result service_load(Service *s,const uint8_t *bytes,uint32_t size) {
    service_unload(s); // Never reserve two service modules/linear memories.
    if (size<8 || size>MODULE_LIMIT) return "service module size invalid";
#if LIGHTVM_FLASH_EXPERIMENT
    if (!program_map(&s->code_mapping,bytes,size)) return "service flash mapping failed";
    s->bytes=(uint8_t *)s->code_mapping.bytes;
#else
    s->bytes=demo_calloc(1,size); if (!s->bytes) return "service code admission failed";
    memcpy(s->bytes,bytes,size);
#endif
    s->size=size; s->crc=crc32_bytes(bytes,size);
    M3Result e=m3Err_mallocFailed;
    s->environment=m3_NewEnvironment();
    if (s->environment) s->runtime=m3_NewRuntime(s->environment,VM_STACK_SIZE,s);
    if (s->runtime) e=m3_ParseModule(s->environment,&s->module,s->bytes,size);
    if (!e) {
        e=m3_LoadModule(s->runtime,s->module);
        if (e) { m3_FreeModule(s->module); s->module=NULL; }
    }
#define LINK(name,sig,fn) if (!e) e=link(s,name,sig,fn)
    LINK("tcp_connect","i(ii)",tcp_connect_guest); LINK("tcp_state","i(i)",tcp_state_guest);
    LINK("tcp_read","i(iii)",tcp_read_guest); LINK("tcp_write","i(iii)",tcp_write_guest); LINK("tcp_close","i(i)",tcp_close_guest);
    LINK("time_ms","I()",time_guest); LINK("identity","i(ii)",identity_guest);
    LINK("animation_state","i(ii)",animation_state_guest); LINK("speed_set","i(Fii)",speed_set_guest);
#undef LINK
    if (!e) e=m3_CompileModule(s->module);
    vm_service_execution(platform_time_us()+SERVICE_DEADLINE_US); s->io_budget=2048;
    if (!e) e=m3_FindFunction(&s->init,s->runtime,"service_init");
    if (!e) e=m3_FindFunction(&s->step,s->runtime,"service_step");
    vm_service_execution(0);
    if (!e && (m3_GetArgCount(s->init)!=2 || m3_GetArgType(s->init,0)!=c_m3Type_i32 || m3_GetArgType(s->init,1)!=c_m3Type_i32 ||
               m3_GetRetCount(s->init)!=0 || m3_GetArgCount(s->step)!=0 || m3_GetRetCount(s->step)!=1 || m3_GetRetType(s->step,0)!=c_m3Type_i32)) e="invalid service exports";
    if (e) { service_unload(s); fault(s,e); return e; }
    s->loaded=true; return m3Err_none;
}
static void observe_memory(Service *s) {
    platform_native_memory(&s->free_heap,&s->min_heap,&s->largest_block);
    s->window_heap_peak=(uint32_t)vm_heap_window_peak();
}
void service_tick(Service *s) {
    observe_memory(s);
    if (!s->running) return;
    uint64_t start=platform_time_us(); s->io_budget=2048;
    vm_service_execution(start+SERVICE_DEADLINE_US);
    M3Result e=m3_CallV(s->step); int32_t state=0;
    if (!e) e=m3_GetResultsV(s->step,&state);
    vm_service_execution(0);
    uint32_t elapsed=(uint32_t)(platform_time_us()-start);
    ++s->calls; s->execution_total_us+=elapsed; if (elapsed>s->execution_max_us) s->execution_max_us=elapsed;
    s->guest_state=(uint32_t)state;
    if (e) { fault(s,e); return; }
    if (s->pending) {
        s->pending=false;
        bool ok=control_set_float(s->animation,"speed",s->pending_generation,s->pending_revision,s->pending_speed);
        s->command_result=ok?1:2;
        if (ok) ++s->commands; else ++s->commands_rejected;
        uint32_t latency=(uint32_t)(platform_time_us()-s->pending_at);
        if (latency>s->command_max_us) s->command_max_us=latency;
        if (s->measuring) {
            if (s->latency_count<256) s->command_latencies[s->latency_count++]=latency;
            else ++s->latency_dropped;
        }
    }
    observe_memory(s);
}
void service_frame(Service *s,uint64_t now) {
    if (!s->measuring) return;
    ++s->frames;
    if (s->last_frame) {
        uint32_t gap=(uint32_t)(now-s->last_frame);
        if (s->gap_count<SERVICE_SAMPLE_LIMIT) s->frame_gaps[s->gap_count++]=gap; else ++s->gap_dropped;
        if (gap>FRAME_INTERVAL_US*3u/2u) s->missed+=gap/FRAME_INTERVAL_US>1?gap/FRAME_INTERVAL_US-1:1;
    }
    s->last_frame=now;
}
typedef struct { SendMessage send; void *context; uint16_t id; } Reply;
static void data(Reply *r,const char *p) {
    size_t n=strlen(p);
    while (n) { size_t count=n>FRAME_LIMIT?FRAME_LIMIT:n; r->send(r->context,RESP_DATA,r->id,(const uint8_t *)p,count); p+=count; n-=count; }
}
bool service_manage(Service *s,SendMessage send,void *context,uint16_t id,const uint8_t *payload,size_t size) {
    Json j; char op[40];
    if (!json_parse(&j,payload,size) || !json_string(&j,json_get(&j,0,"op"),op,sizeof(op)) || strncmp(op,"service_",8)) return false;
    static const char *const fields[]={"op","ipv4","port","offset","kind",NULL};
    Reply r={send,context,id}; enum Status code=STATUS_OK; const char *message="experimental service operation completed";
    if (!json_fields(&j,0,fields)) { code=STATUS_PROTOCOL; message="invalid service fields"; }
    else if (!strcmp(op,"service_stop")) { close_connection(s); s->running=false; s->pending=false; }
    else if (!strcmp(op,"service_unload")) service_unload(s);
    else if (!strcmp(op,"service_start")) {
        uint32_t ip,port;
        if (!s->loaded || s->faulted || !json_u32(&j,json_get(&j,0,"ipv4"),&ip) || !json_u32(&j,json_get(&j,0,"port"),&port) || !port || port>65535) {
            code=STATUS_CONFIG; message="load a service and supply numeric IPv4 and port";
        } else {
            close_connection(s); s->ipv4=ip; s->port=port; s->io_budget=2048;
            vm_service_execution(platform_time_us()+SERVICE_DEADLINE_US); M3Result e=m3_CallV(s->init,ip,port); vm_service_execution(0);
            if (e) { fault(s,e); code=STATUS_RUN; message=s->error; } else s->running=true;
        }
    } else if (!strcmp(op,"service_measure_start")) {
        s->measuring=true; s->started=platform_time_us(); s->ended=0; s->last_frame=0; s->gap_count=s->gap_dropped=s->frames=s->missed=0;
        s->calls=s->execution_max_us=s->commands=s->commands_rejected=s->command_max_us=0; s->execution_total_us=0;
        s->latency_count=s->latency_dropped=0; vm_heap_window_reset();
    } else if (!strcmp(op,"service_measure_stop")) { s->measuring=false; s->ended=platform_time_us(); }
    else if (!strcmp(op,"service_samples")) {
        uint32_t offset=0; char kind[16]="gaps"; int token=json_get(&j,0,"offset"),k=json_get(&j,0,"kind");
        if ((token>=0 && !json_u32(&j,token,&offset)) || (k>=0 && !json_string(&j,k,kind,sizeof(kind))) || (strcmp(kind,"gaps") && strcmp(kind,"latencies"))) {
            code=STATUS_PROTOCOL; message="invalid sample offset/kind";
        } else {
            const uint32_t *samples=!strcmp(kind,"gaps")?s->frame_gaps:s->command_latencies;
            uint32_t count=!strcmp(kind,"gaps")?s->gap_count:s->latency_count;
            data(&r,"[");
            for (uint32_t i=offset;i<count && i-offset<32;++i) { char b[24]; snprintf(b,sizeof(b),"%s%lu",i==offset?"":",",(unsigned long)samples[i]); data(&r,b); }
            data(&r,"]");
        }
    } else if (!strcmp(op,"service_status")) {
        observe_memory(s); char b[1500],error[sizeof(s->error)];
        for (size_t i=0;i<sizeof(error);++i) { char c=s->error[i]; error[i]=(c=='"' || c=='\\' || (c && c<32))?' ':c; if (!c) break; }
        uint64_t elapsed=s->started?(s->ended?s->ended:platform_time_us())-s->started:0;
        snprintf(b,sizeof(b),"{\"experimental\":true,\"loaded\":%s,\"running\":%s,\"faulted\":%s,\"error\":\"%s\",\"module_bytes\":%lu,\"module_crc32\":%lu,\"guest_state\":%lu,\"tcp_state\":%d,\"heap\":%lu,\"heap_peak\":%lu,\"window_heap_peak\":%lu,\"heap_limit\":%u,\"native_free_heap\":%lu,\"native_min_heap\":%lu,\"native_largest_block\":%lu,\"measurement_static_bytes\":%lu,\"calls\":%lu,\"execution_total_us\":%" PRIu64 ",\"execution_max_us\":%lu,\"commands\":%lu,\"commands_rejected\":%lu,\"command_max_us\":%lu,\"latency_count\":%lu,\"latency_dropped\":%lu,\"frames\":%lu,\"elapsed_us\":%" PRIu64 ",\"frame_interval_us\":%u,\"missed_deadlines\":%lu,\"gap_count\":%lu,\"gap_dropped\":%lu}",
            s->loaded?"true":"false",s->running?"true":"false",s->faulted?"true":"false",error,(unsigned long)s->size,(unsigned long)s->crc,(unsigned long)s->guest_state,s->handle?platform_tcp_state(&s->tcp):-1,
            (unsigned long)vm_heap_used(),(unsigned long)vm_heap_peak(),(unsigned long)s->window_heap_peak,VM_HEAP_LIMIT,(unsigned long)s->free_heap,(unsigned long)s->min_heap,(unsigned long)s->largest_block,
            (unsigned long)(sizeof(s->frame_gaps)+sizeof(s->command_latencies)),(unsigned long)s->calls,s->execution_total_us,(unsigned long)s->execution_max_us,(unsigned long)s->commands,(unsigned long)s->commands_rejected,(unsigned long)s->command_max_us,
            (unsigned long)s->latency_count,(unsigned long)s->latency_dropped,(unsigned long)s->frames,elapsed,FRAME_INTERVAL_US,(unsigned long)s->missed,(unsigned long)s->gap_count,(unsigned long)s->gap_dropped);
        data(&r,b);
    } else { code=STATUS_PROTOCOL; message="unknown experimental service operation"; }
    send_status(send,context,id,code,0,message); return true;
}
