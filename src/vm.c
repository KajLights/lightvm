#include "vm.h"
#include "vm_allocator.h"
#include "pixels.h"
#include "animation.h"
#include <stdio.h>
#include <string.h>

static const char timeout_error[]="execution deadline exceeded";
static void reply(VmInstance *a, uint16_t id, enum Status code, int32_t value, const char *message) {
    send_status(a->send,a->transport,id,code,value,message);
}
void vm_init(VmInstance *a, PixelBuffer *target, SendMessage send, void *transport) {
    memset(a,0,sizeof(*a)); a->generation=1; a->send=send; a->transport=transport; a->target=target;
    a->output=target && target->driver ? target : NULL; memcpy(a->slot,"A",2);
}
static void stop_animation(VmInstance *a) {
    a->playing=false;
    a->animation_error[0]=0;
    if(a->portable){const char *error=animation_quiesce(a);if(error)snprintf(a->animation_error,sizeof(a->animation_error),"%s",error);}
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
    control_destroy(a);
    if (a->generation!=UINT32_MAX) ++a->generation;
    a->ready=false; a->from_storage=false;
    memset(&a->program,0,sizeof(a->program)); memset(&a->instance,0,sizeof(a->instance));
}
bool vm_read(void *context,uint32_t offset,void *bytes,size_t size) {
    VmInstance *a=context;
    if(offset>a->expected || size>a->expected-offset)return false;
    if(a->from_storage)return platform_storage_read(a->storage_offset+offset,bytes,size);
    memcpy(bytes,a->bytes+offset,size);return true;
}
bool vm_materialize(VmInstance *a) {
    if(!a->from_storage)return true;
    if(!vm_read(a,0,a->bytes,a->expected))return false;
    a->from_storage=false;a->program.cache_size=0;if(a->portable)a->portable->cache_size=0;return true;
}
const char *vm_load(VmInstance *a) {
    if(a->generation==UINT32_MAX)return "program generation exhausted";
    uint8_t header[8];
    if(vm_read(a,0,header,sizeof(header))&&!memcmp(header,"LBC1",4)&&lbc_u32(header+4)==3){
        const char *error=pbc_open(&a->portable,vm_read,a,a->expected);
        if(!error)error=animation_validate(a->portable);
        return error?error:control_probe(a);
    }
    if(!lbc_open_reader(&a->program,vm_read,a,a->expected))return "invalid LBC1 bytecode package";
    if(lbc_validate(&a->program,LBC_CODE)!=1)return "invalid bytecode instruction";
    return control_probe(a);
}
static void run(VmInstance *a,uint16_t id,const uint8_t *payload,size_t size) {
    (void)payload;(void)size;
    reply(a,id,STATUS_RUN,0,a->ready ? "control programs execute through PLAY/FRAME, not RUN" : "no loaded module");
}
static enum Status execution_status(VmInstance *a,const char *error) {
    (void)a;
    return !strcmp(error,timeout_error) || strstr(error,"instruction limit") ? STATUS_LIMIT : STATUS_RUN;
}
static const char *prepare_animation(VmInstance *a) { return control_prepare(a); }
static const char *render_animation(VmInstance *a,uint32_t delta_ms) {
    return control_step(a,1,(uint64_t)delta_ms*1000u);
}
static void show_frame(VmInstance *a, uint64_t started, bool contribution) {
    a->render_us=(uint32_t)(platform_time_us()-started);
    uint64_t show_started=platform_time_us();
    bool success;
    if (contribution) {
        // The single-slot presenter submits the complete RGB frame.
        // Each instance keeps an independent committed contribution for a mixer.
        if (!a->output) { control_presented(a,true);a->show_us=0; ++a->frame_count; return; }
        const uint8_t *frame=control_frame(a);
        success=a->output->driver && a->output->type && a->output->rgb &&
            a->output->count==a->control.count &&
            a->output->type->show(a->output->driver,frame,a->control.count);
        if (success) memcpy(a->output->rgb,frame,(size_t)a->control.count*3);
        control_presented(a,success);
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
    bool continuing=a->portable&&a->control.in_step;
    if (!continuing&&now<a->next_frame) return;
    uint64_t elapsed_us=continuing?0:now-a->last_frame;
    uint32_t delta_ms=elapsed_us>1000000u ? 1000 : (uint32_t)(elapsed_us/1000u);
    // Carry fractional milliseconds and keep the scheduled frame cadence.
    if(!continuing){
        a->last_frame=a->control.enabled || elapsed_us>1000000u ? now : a->last_frame+delta_ms*1000u;
        a->next_frame+=FRAME_INTERVAL_US;
        if (a->next_frame<=now) a->next_frame=now+FRAME_INTERVAL_US;
    }
    bool ready=true;
    const char * error=a->portable?animation_poll(a,elapsed_us,&ready):a->control.enabled ? control_playback_step(a,elapsed_us) : render_animation(a,delta_ms);
    if (error) {
        a->playing=false;
        snprintf(a->animation_error,sizeof(a->animation_error),"%s",error);
        return; // Preserve the last complete frame on the physical strip.
    }
    if(ready)show_frame(a,now,a->control.enabled);
}
static void play(VmInstance *a, uint16_t id) {
    a->playing=false; a->animation_error[0]=0;
    bool initial=a->control.enabled && !a->control.initialized;
    uint64_t started=platform_time_us();
    const char * error=a->portable?animation_quiesce(a):NULL;
    if(!error)error=prepare_animation(a);
    if(!error&&a->portable&&!initial)error=animation_advance(a,0,true);
    if (error) {
        snprintf(a->animation_error,sizeof(a->animation_error),"%s",error);
        reply(a,id,execution_status(a,error),0,error);
        return;
    }
    if (initial || a->portable) {
        show_frame(a,started,true);
        if (a->animation_error[0]) { reply(a,id,STATUS_RUN,0,a->animation_error); return; }
    }
    a->playing=!(a->portable&&a->portable->completed); a->frame_count=0;
    a->last_frame=platform_time_us(); a->next_frame=a->last_frame;
    reply(a,id,STATUS_OK,(a->target ? a->target->count : 0),"animation started");
}
static void frame_step(VmInstance *a, uint16_t id, uint32_t delta_ms) {
    if(a->portable){
        const char *error=animation_preflight(a,(uint64_t)delta_ms*1000u);
        if(error){reply(a,id,execution_status(a,error),0,error);return;}
    }
    a->playing=false;
    a->animation_error[0]=0;
    uint64_t started=platform_time_us();
    const char * error=a->portable?animation_quiesce(a):NULL;
    if(!error)error=prepare_animation(a);
    if (!error) error=render_animation(a,delta_ms);
    if (error) {
        reply(a,id,execution_status(a,error),0,error);
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
        snprintf(message,sizeof(message),"api=%u protocol=%u platform=%.16s board=%.24s output=%.16s ready=%u uploading=%u received=%lu module_limit=%u heap=%lu heap_peak=%lu heap_limit=%u linear_limit=%u pin=%ld pixels=%lu brightness=%lu frame_interval_us=%u mode=%s code_storage=%s frames=%lu render_us=%lu show_us=%lu control_abi=%u generation=%lu animation_error=%.99s",
                 LIGHTVM_ANIMATION_API_VERSION,LIGHTVM_PROTOCOL_VERSION,
                 info->platform,info->board,info->output,
                 a->ready,a->uploading,(unsigned long)a->received,MODULE_LIMIT,
                 (unsigned long)vm_heap_used(),(unsigned long)vm_heap_peak(),VM_HEAP_LIMIT,
                 LINEAR_MEMORY_LIMIT,(long)(a->target && a->target->count ? a->target->config.pin : -1),(unsigned long)(a->target ? a->target->count : 0),
                 (unsigned long)(a->target && a->target->count ? a->target->config.brightness : 0),FRAME_INTERVAL_US,
                 a->playing ? "bytecode" : "idle", a->ready ? (a->from_storage ? "flash" : "ram") : "none",
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
        const char * error=vm_load(a);
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

const char *vm_validate(const uint8_t *bytes,size_t size) {
    // A temporary instance uses the same admission path without executing code.
    VmInstance *candidate=demo_calloc(1,sizeof(*candidate));
    if(!candidate)return "memory allocation failed";
    const char *error="bytecode package exceeds module limit";
    if(size<=MODULE_LIMIT) {
        memcpy(candidate->bytes,bytes,size);candidate->expected=size;
        error=vm_load(candidate);
    }
    control_destroy(candidate);demo_free(candidate);return error;
}
