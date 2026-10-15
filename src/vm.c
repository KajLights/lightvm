#include "vm.h"
#include "vm_allocator.h"
#include "pixels.h"
#include <stdio.h>
#include <string.h>

static VmInstance *executing;
static uint64_t service_deadline;
void vm_service_execution(uint64_t deadline) { service_deadline=deadline; }
static const char timeout_error[]="execution deadline exceeded";
static const char output_error[]="console output limit exceeded";
static const char exit_trap[]="guest requested exit";
static const char background_console_error[]="use FRAME to debug animation console output";
static const char color_error[]="rgb/hsv require render(index) and valid 8-bit color values";

M3Result m3_Yield(void) {
    return ((executing && platform_time_us()>=executing->deadline) ||
            (service_deadline && platform_time_us()>=service_deadline)) ? timeout_error : m3Err_none;
}
static void reply(VmInstance *a, uint16_t id, enum Status code, int32_t value, const char *message) {
    send_status(a->send,a->transport,id,code,value,message);
}
void vm_init(VmInstance *a, PixelBuffer *target, SendMessage send, void *transport) {
    memset(a,0,sizeof(*a)); a->generation=1; a->send=send; a->transport=transport; a->target=target;
    a->output=target && target->driver ? target : NULL; memcpy(a->slot,"A",2);
}
static void stop_animation(VmInstance *a) {
    a->playing=false; a->rendering_pixel=false;
    a->render=NULL; a->before_render=NULL; a->animation_error[0]=0;
    if (a->control.enabled && a->control.allocation && a->output) {
        size_t n=(size_t)a->control.count*3;
        memset(a->control.working,0,n);
        if (a->output->count==a->control.count && a->output->type && a->output->driver && a->output->rgb &&
            a->output->type->show(a->output->driver,a->control.working,a->control.count)) {
            memset(a->output->rgb,0,n); a->control.output_valid=true;
            ++a->control.output_frame;
        } else snprintf(a->animation_error,sizeof(a->animation_error),"native pixel output failed");
    } else if (!a->control.enabled) { pixels_clear(a->target); pixels_show(a->target); }
}
void vm_destroy(VmInstance *a) {
    stop_animation(a);
    if (a->runtime) m3_FreeRuntime(a->runtime);
    if (a->environment) m3_FreeEnvironment(a->environment);
    control_destroy(a);
#if LIGHTVM_FLASH_EXPERIMENT
    if (a->code_mapping.bytes) program_unmap(&a->code_mapping); else demo_free(a->bytes);
    a->bytes=NULL;
#endif
#if LIGHTVM_MQTT_EXPERIMENT
    vm_heap_uncharge(a->code_charge); a->code_charge=0;
#endif
    if (a->generation!=UINT32_MAX) ++a->generation;
    a->runtime=NULL; a->environment=NULL; a->module=NULL; a->ready=false;
}
static bool memory_range(IM3Runtime runtime, uint32_t offset, uint32_t size, uint8_t **out) {
    uint32_t available=0;
    uint8_t *memory=m3_GetMemory(runtime,&available,0);
    if (!memory || offset>available || size>available-offset) return false;
    *out=memory+offset; return true;
}
static M3Result console_bytes(VmInstance *a, const uint8_t *p, size_t size) {
    if (a->background) return background_console_error;
    if (size>OUTPUT_LIMIT-a->output_size) return output_error;
    a->output_size+=size;
    while (size) {
        size_t chunk=size>FRAME_LIMIT ? FRAME_LIMIT : size;
        a->send(a->transport,RESP_OUTPUT,a->request_id,p,chunk);
        p+=chunk; size-=chunk;
    }
    return m3_Yield();
}
m3ApiRawFunction(lightvm_console_write) {
    (void)_mem;
    m3ApiGetArg(uint32_t,offset);
    m3ApiGetArg(uint32_t,size);
    uint8_t *bytes;
    if (!memory_range(runtime,offset,size,&bytes)) return m3Err_trapOutOfBoundsMemoryAccess;
    return console_bytes((VmInstance *)_ctx->userdata,bytes,size);
}
m3ApiRawFunction(wasi_fd_write) {
    (void)_mem;
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t,fd);
    m3ApiGetArg(uint32_t,iovs);
    m3ApiGetArg(uint32_t,count);
    m3ApiGetArg(uint32_t,written);
    VmInstance *a=(VmInstance *)_ctx->userdata;
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
    VmInstance *a=(VmInstance *)_ctx->userdata;
    a->guest_exit=true; a->exit_code=code;
    return exit_trap;
}
m3ApiRawFunction(led_pixel_count) {
    (void)runtime; (void)_ctx; (void)_mem;
    m3ApiReturnType(uint32_t);
    VmInstance *a=(VmInstance *)_ctx->userdata;
    m3ApiReturn(a->target ? a->target->count : 0);
}
m3ApiRawFunction(led_rgb) {
    (void)runtime; (void)_mem;
    m3ApiGetArg(uint32_t,red);
    m3ApiGetArg(uint32_t,green);
    m3ApiGetArg(uint32_t,blue);
    VmInstance *a=(VmInstance *)_ctx->userdata;
    if (!a->rendering_pixel || !pixels_set_rgb(a->target,a->pixel_index,red,green,blue))
        return color_error;
    return m3_Yield();
}
m3ApiRawFunction(led_hsv) {
    (void)runtime; (void)_mem;
    m3ApiGetArg(uint32_t,hue);
    m3ApiGetArg(uint32_t,saturation);
    m3ApiGetArg(uint32_t,value);
    VmInstance *a=(VmInstance *)_ctx->userdata;
    if (!a->rendering_pixel || !pixels_set_hsv(a->target,a->pixel_index,hue,saturation,value))
        return color_error;
    return m3_Yield();
}
static M3Result link_optional(VmInstance *a, const char *space, const char *name,
                              const char *signature, M3RawCall fn) {
    M3Result result=m3_LinkRawFunctionEx(a->module,space,name,signature,fn,a);
    return result==m3Err_functionLookupFailed ? m3Err_none : result;
}
bool vm_reserve_upload(VmInstance *a,size_t size) {
#if LIGHTVM_FLASH_EXPERIMENT
    a->bytes=demo_calloc(1,size); return a->bytes!=NULL;
#else
    (void)a; (void)size; return true;
#endif
}
M3Result vm_load(VmInstance *a) {
    if (a->generation==UINT32_MAX) return "program generation exhausted";
#if LIGHTVM_FLASH_EXPERIMENT
    if (!program_map(&a->code_mapping,a->bytes,a->expected)) return "program flash mapping failed";
    demo_free(a->bytes); a->bytes=(uint8_t *)a->code_mapping.bytes;
#elif LIGHTVM_MQTT_EXPERIMENT
    if (!vm_heap_charge(a->expected)) return "animation code admission failed";
    a->code_charge=a->expected;
#endif
    M3Result profile=control_probe(a->bytes,a->expected,&a->control);
    if (profile) return profile;
    a->environment=m3_NewEnvironment();
    if (!a->environment) return m3Err_mallocFailed;
    a->runtime=m3_NewRuntime(a->environment,VM_STACK_SIZE,a);
    if (!a->runtime) return m3Err_mallocFailed;
    M3Result result=m3_ParseModule(a->environment,&a->module,a->bytes,(uint32_t)a->expected);
    if (result) return result;
    result=m3_LoadModule(a->runtime,a->module);
    if (result) { m3_FreeModule(a->module); a->module=NULL; return result; }
    if (a->control.enabled) {
        result=control_link(a);
        if (!result) result=m3_CompileModule(a->module);
        return result;
    }
    result=link_optional(a,"console","write","v(ii)",lightvm_console_write);
    if (!result) result=link_optional(a,"wasi_snapshot_preview1","fd_write","i(iiii)",wasi_fd_write);
    if (!result) result=link_optional(a,"wasi_snapshot_preview1","proc_exit","v(i)",wasi_proc_exit);
    if (!result) result=link_optional(a,"led","pixel_count","i()",led_pixel_count);
    if (!result) result=link_optional(a,"led","rgb","v(iii)",led_rgb);
    if (!result) result=link_optional(a,"led","hsv","v(iii)",led_hsv);
    if (!result) result=m3_CompileModule(a->module);
    return result;
}
static void run(VmInstance *a, uint16_t id, const uint8_t *payload, size_t size) {
    if (!a->ready) { reply(a,id,STATUS_RUN,0,"no loaded module"); return; }
    if (a->control.enabled) { reply(a,id,STATUS_RUN,0,"control programs execute through PLAY/FRAME, not RUN"); return; }
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
static void begin_execution(VmInstance *a, uint16_t id, bool background) {
    a->request_id=id; a->output_size=0; a->guest_exit=false; a->exit_code=0;
    a->background=background; a->deadline=platform_time_us()+RUN_TIMEOUT_US;
    executing=a;
}
static void end_execution(VmInstance *a) {
    a->background=false; a->rendering_pixel=false; executing=NULL;
}
static enum Status execution_status(VmInstance *a, M3Result error) {
    if (a->guest_exit && error==exit_trap)
        return a->exit_code ? STATUS_EXIT : STATUS_OK;
    return (error==timeout_error || error==output_error) ? STATUS_LIMIT : STATUS_RUN;
}
static bool pixel_callback(IM3Function function) {
    return m3_GetArgCount(function)==1 &&
           m3_GetArgType(function,0)==c_m3Type_i32 &&
           m3_GetRetCount(function)==0;
}
static M3Result prepare_animation(VmInstance *a) {
    if (a->control.enabled) return control_prepare(a);
    if (!a->target || !a->target->count) return "target A has no configured output";
    if (a->render) return m3Err_none;
    if (!a->ready) return "no loaded module";
    M3Result error=m3_RunStart(a->module);
    IM3Function render=NULL, before=NULL;
    if (!error) error=m3_FindFunction(&render,a->runtime,"render");
    if (!error && !pixel_callback(render))
        error="render must take an i32 index and return void";
    if (!error) {
        error=m3_FindFunction(&before,a->runtime,"beforeRender");
        if (error==m3Err_functionLookupFailed) { error=m3Err_none; before=NULL; }
        if (!error && before && !pixel_callback(before))
            error="beforeRender must take i32 milliseconds and return void";
    }
    if (!error) { a->render=render; a->before_render=before; }
    return error;
}
static M3Result render_animation(VmInstance *a, uint32_t delta_ms) {
    if (a->control.enabled) return control_step(a,1,(uint64_t)delta_ms*1000u);
    pixels_clear(a->target);
    M3Result error=m3Err_none;
    if (a->before_render) error=m3_CallV(a->before_render,(int32_t)delta_ms);
    for (uint32_t index=0; !error && index<(a->target ? a->target->count : 0); ++index) {
        a->pixel_index=index; a->rendering_pixel=true;
        error=m3_CallV(a->render,(int32_t)index);
        a->rendering_pixel=false;
    }
    return error;
}
static void show_frame(VmInstance *a, uint64_t started, bool contribution) {
    a->render_us=(uint32_t)(platform_time_us()-started);
    uint64_t show_started=platform_time_us();
    bool success;
    if (contribution) {
        // The single-slot presenter submits the complete RGB frame.
        // Each instance keeps an independent committed contribution for a mixer.
        if (!a->output) { a->show_us=0; ++a->frame_count; return; }
        success=a->output->driver && a->output->type && a->output->rgb &&
            a->output->count==a->control.count &&
            a->output->type->show(a->output->driver,a->control.committed,a->control.count);
        if (success) memcpy(a->output->rgb,a->control.committed,(size_t)a->control.count*3);
    } else success=pixels_show(a->target);
    if (!success) {
        a->playing=false;
        if (contribution) a->control.faulted=true;
        snprintf(a->animation_error,sizeof(a->animation_error),"native pixel output failed");
    }
    a->show_us=(uint32_t)(platform_time_us()-show_started);
    if (a->control.enabled && success) {
        a->control.output_valid=true; ++a->control.output_frame;
    }
    ++a->frame_count;
}
void vm_tick(VmInstance *a) {
    if (!a->playing) return;
    uint64_t now=platform_time_us();
    if (now<a->next_frame) return;
    uint64_t elapsed_us=now-a->last_frame;
    uint32_t delta_ms=elapsed_us>1000000u ? 1000 : (uint32_t)(elapsed_us/1000u);
    // Carry fractional milliseconds and keep the scheduled frame cadence.
    a->last_frame=a->control.enabled || elapsed_us>1000000u ? now : a->last_frame+delta_ms*1000u;
    a->next_frame+=FRAME_INTERVAL_US;
    if (a->next_frame<=now) a->next_frame=now+FRAME_INTERVAL_US;
    begin_execution(a,0,true);
    M3Result error=a->control.enabled ? control_step(a,1,elapsed_us) : render_animation(a,delta_ms);
    end_execution(a);
    if (error) {
        a->playing=false;
        snprintf(a->animation_error,sizeof(a->animation_error),"%s",error);
        return; // Preserve the last complete frame on the physical strip.
    }
    show_frame(a,now,a->control.enabled);
}
static void play(VmInstance *a, uint16_t id) {
    a->playing=false; a->animation_error[0]=0;
    bool initial=a->control.enabled && !a->control.initialized;
    uint64_t started=platform_time_us();
    begin_execution(a,id,false);
    M3Result error=prepare_animation(a);
    end_execution(a);
    if (error) {
        snprintf(a->animation_error,sizeof(a->animation_error),"%s",error);
        reply(a,id,execution_status(a,error),a->guest_exit ? a->exit_code : 0,error);
        return;
    }
    if (initial) {
        show_frame(a,started,true);
        if (a->animation_error[0]) { reply(a,id,STATUS_RUN,0,a->animation_error); return; }
    }
    a->playing=true; a->frame_count=0;
    a->last_frame=platform_time_us(); a->next_frame=a->last_frame;
    reply(a,id,STATUS_OK,(a->target ? a->target->count : 0),"animation started");
}
static void frame_step(VmInstance *a, uint16_t id, uint32_t delta_ms) {
    a->playing=false;
    a->animation_error[0]=0;
    uint64_t started=platform_time_us();
    begin_execution(a,id,false);
    M3Result error=prepare_animation(a);
    if (!error) error=render_animation(a,delta_ms);
    end_execution(a);
    if (error) {
        reply(a,id,execution_status(a,error),a->guest_exit ? a->exit_code : 0,error);
        return;
    }
    show_frame(a,started,a->control.enabled);
    if (a->animation_error[0]) { reply(a,id,STATUS_RUN,0,a->animation_error); return; }
    const uint8_t *bytes=a->control.enabled ?
        (a->output ? a->output->rgb : a->control.committed) : a->target->rgb;
    for (size_t offset=0; offset<((size_t)a->target->count * 3); offset+=FRAME_LIMIT) {
        size_t count=((size_t)a->target->count * 3)-offset;
        if (count>FRAME_LIMIT) count=FRAME_LIMIT;
        a->send(a->transport,RESP_PIXELS,id,bytes+offset,count);
    }
    reply(a,id,STATUS_OK,(a->target ? a->target->count : 0),"frame rendered; snapshot is RGB before brightness scaling");
}
void vm_command(void *context, uint8_t type, uint16_t id, const uint8_t *p, size_t size) {
    VmInstance *a=context;
    switch (type) {
    case CMD_STATUS: {
        if (size) { reply(a,id,STATUS_PROTOCOL,0,"STATUS takes no payload"); break; }
        const PlatformInfo *info=platform_info();
        char message[FRAME_LIMIT-8];
        snprintf(message,sizeof(message),"api=%u protocol=%u platform=%.16s board=%.24s output=%.16s ready=%u uploading=%u received=%lu module_limit=%u heap=%lu heap_peak=%lu heap_limit=%u linear_limit=%u pin=%ld pixels=%lu brightness=%lu frame_interval_us=%u mode=%s frames=%lu render_us=%lu show_us=%lu control_abi=%u generation=%lu animation_error=%.99s",
                 LIGHTVM_ANIMATION_API_VERSION,LIGHTVM_PROTOCOL_VERSION,
                 info->platform,info->board,info->output,
                 a->ready,a->uploading,(unsigned long)a->received,MODULE_LIMIT,
                 (unsigned long)vm_heap_used(),(unsigned long)vm_heap_peak(),VM_HEAP_LIMIT,
                 LINEAR_MEMORY_LIMIT,(long)(a->target && a->target->count ? a->target->config.pin : -1),(unsigned long)(a->target ? a->target->count : 0),
                 (unsigned long)(a->target && a->target->count ? a->target->config.brightness : 0),FRAME_INTERVAL_US,
                 a->playing ? "wasm" : "idle",
                 (unsigned long)a->frame_count,(unsigned long)a->render_us,
                 (unsigned long)a->show_us,CONTROL_ABI_VERSION,(unsigned long)a->generation,a->animation_error[0] ? a->animation_error : "-");
        reply(a,id,STATUS_OK,0,message); break;
    }
    case CMD_BEGIN: {
        if (size!=8 || read_u32(p)<8 || read_u32(p)>MODULE_LIMIT) {
            char message[80];
            snprintf(message,sizeof(message),"module size must be 8..%u bytes",MODULE_LIMIT);
            reply(a,id,STATUS_UPLOAD,0,message); break;
        }
        vm_destroy(a); a->received=0; a->expected=read_u32(p);
        if (!vm_reserve_upload(a,a->expected)) { reply(a,id,STATUS_UPLOAD,0,"upload allocation failed"); break; }
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
        M3Result error=vm_load(a);
        if (error) { reply(a,id,STATUS_LOAD,0,error); vm_destroy(a); }
        else { a->ready=true; reply(a,id,STATUS_OK,(int32_t)a->received,"module loaded"); }
        break;
    }
    case CMD_RUN: run(a,id,p,size); break;
    case CMD_RESET:
        if (size) { reply(a,id,STATUS_PROTOCOL,0,"RESET takes no payload"); break; }
        vm_destroy(a); a->uploading=false; a->received=0; a->expected=0;
        reply(a,id,STATUS_OK,0,"reset"); break;
    case CMD_PLAY:
        if (size) { reply(a,id,STATUS_PROTOCOL,0,"PLAY takes no payload"); break; }
        play(a,id); break;
    case CMD_STOP:
        if (size) { reply(a,id,STATUS_PROTOCOL,0,"STOP takes no payload"); break; }
        stop_animation(a);
        reply(a,id,STATUS_OK,0,"animation stopped; LEDs cleared"); break;
    case CMD_FRAME:
        if (size!=4 || read_u32(p)>1000) {
            reply(a,id,STATUS_PROTOCOL,0,"FRAME requires 0..1000 milliseconds"); break;
        }
        frame_step(a,id,read_u32(p)); break;
    default: reply(a,id,STATUS_PROTOCOL,0,"unknown command"); break;
    }
}

M3Result vm_validate(const uint8_t *bytes, size_t size) {
    Control control;
    M3Result profile=control_probe(bytes,size,&control);
    if (profile) return profile;
    IM3Environment environment=m3_NewEnvironment();
    if (!environment) return m3Err_mallocFailed;
    IM3Module module=NULL;
    M3Result error=m3_ParseModule(environment,&module,bytes,(uint32_t)size);
    if (module) m3_FreeModule(module);
    m3_FreeEnvironment(environment);
    return error;
}
