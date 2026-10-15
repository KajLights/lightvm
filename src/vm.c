#include "vm.h"
#include "vm_allocator.h"
#include "runtime.h"
#include "pixels.h"
#include "animation.h"
#include <stdio.h>
#include <string.h>

static void reply(VmInstance *a, uint16_t id, enum Status code, int32_t value, const char *message) {
    send_status(a->send,a->transport,id,code,value,message);
}
void vm_init(VmInstance *a, PixelBuffer *target, SendMessage send, void *transport) {
    memset(a,0,sizeof(*a)); a->generation=1; a->code_pin=-1; a->send=send; a->transport=transport; a->target=target;
    a->output=target && target->driver ? target : NULL; memcpy(a->slot,"A",2);
}
void vm_output_cancel(VmInstance *a) {
    if(a->output_pending&&a->output&&a->output->driver)a->output->type->cancel(a->output->driver);
    a->output_pending=false;a->view.sealed=false;
}
static void stop_animation(VmInstance *a) {
    vm_output_cancel(a);
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
    a->memory=NULL;
}
bool vm_read(void *context,uint32_t offset,void *bytes,size_t size) {
    VmInstance *a=context;
    if(offset>a->expected || size>a->expected-offset)return false;
    if(a->from_storage)return store_read(a->store,&a->code,offset,bytes,size);
    if(!a->memory)return false;
    memcpy(bytes,a->memory+offset,size);return true;
}
bool vm_bind_storage(VmInstance *a,Store *store,const StoreEntry *entry) {
    int pin=store_pin(store,entry);if(pin<0)return false;
    a->store=store;a->code=*entry;a->code_pin=pin;a->from_storage=true;
    a->expected=a->received=entry->size;return true;
}
void vm_rebind(VmInstance *a) {
    if(a->portable){a->portable->context=a;a->portable->output=&a->view;}
    if(a->legacy){a->legacy->program.context=a;a->legacy->instance.program=&a->legacy->program;a->legacy->instance.buffer=&a->view;}
}
static const char *vm_load_impl(VmInstance *a) {
    if(a->generation==UINT32_MAX)return "program generation exhausted";
    uint8_t header[8];
    if(vm_read(a,0,header,sizeof(header))&&!memcmp(header,"LBC1",4)&&lbc_u32(header+4)==4){
        const char *error=pbc_open(&a->portable,vm_read,a,a->expected);
        if(!error)error=animation_validate(a->portable);
        return error?error:control_probe(a);
    }
    a->legacy=demo_calloc(1,sizeof(*a->legacy));
    if(!a->legacy)return "legacy context exceeds memory budget";
    if(!lbc_open_reader(&a->legacy->program,vm_read,a,a->expected))return "invalid LBC1 bytecode package";
    int status;do{status=lbc_validate(&a->legacy->program,32);runtime_checkpoint();}while(!status);
    if(status!=1)return "invalid bytecode instruction";
    return control_probe(a);
}
static void run(VmInstance *a,uint16_t id,const uint8_t *payload,size_t size) {
    (void)payload;(void)size;
    reply(a,id,STATUS_RUN,0,a->ready ? "control programs execute through PLAY/FRAME, not RUN" : "no loaded module");
}
static enum Status execution_status(VmInstance *a,const char *error) {
    (void)a;(void)error;
    return STATUS_RUN;
}
static const char *prepare_animation(VmInstance *a) { return control_prepare(a); }
static const char *render_animation(VmInstance *a,uint32_t delta_ms) {
    return control_step(a,1,(uint64_t)delta_ms*1000u);
}
void control_presented(VmInstance *a,bool success) {
    if(success)a->animation_error[0]=0;
    else snprintf(a->animation_error,sizeof(a->animation_error),"native pixel output failed; retrying");
}
static void show_frame(VmInstance *a, uint64_t started, bool contribution, bool asynchronous) {
    a->render_us=(uint32_t)(platform_time_us()-started);
    uint64_t show_started=platform_time_us();
    bool success;
    if (contribution) {
        // Each device presents its own complete RGB frame and retains an
        // independent committed contribution.
        if (!a->output) { control_presented(a,true);a->show_us=0; ++a->frame_count; return; }
        const uint8_t *frame=control_frame(a);
        if(asynchronous){
            if(a->output->driver&&a->output->type->submit(a->output->driver,frame,a->control.count)){
                a->output_pending=true;a->view.sealed=true;a->output_started=show_started;return;
            }
            vm_output_cancel(a);
            if(a->output->driver)a->output->type->cancel(a->output->driver);
            control_presented(a,false);return;
        }
        success=a->output->driver && a->output->type && a->output->rgb &&
            a->output->count==a->control.count &&
            a->output->type->show(a->output->driver,frame,a->control.count);
        if (success) memcpy(a->output->rgb,frame,(size_t)a->control.count*3);
    } else success=pixels_show(a->target);
    control_presented(a,success);
    a->show_us=(uint32_t)(platform_time_us()-show_started);
    if (a->control.enabled && success) {
        a->control.output_valid=true; ++a->control.output_frame;
    }
    if(success)++a->frame_count;
}
static bool complete_output(VmInstance *a) {
    if(a->output_pending){
        int status=a->output->type->poll(a->output->driver);
        if(!status)return false;
        a->view.sealed=false;a->output_pending=false;
        if(status<0){a->output->type->cancel(a->output->driver);control_presented(a,false);return false;}
        memcpy(a->output->rgb,control_frame(a),(size_t)a->control.count*3);
        control_presented(a,true);a->control.output_valid=true;a->control.output_frame++;a->frame_count++;
        a->show_us=(uint32_t)(platform_time_us()-a->output_started);
    }
    return true;
}
void vm_tick_budget(VmInstance *a,unsigned budget) {
    if(!complete_output(a))return;
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
    const char * error=a->portable?animation_poll_budget(a,elapsed_us,&ready,budget):a->control.enabled ? control_playback_step(a,elapsed_us) : render_animation(a,delta_ms);
    if (error) {
        a->playing=false;
        snprintf(a->animation_error,sizeof(a->animation_error),"%s",error);
        return; // Preserve the last complete frame on the physical strip.
    }
    if(ready){show_frame(a,now,a->control.enabled,true);(void)complete_output(a);}
}
void vm_tick(VmInstance *a){vm_tick_budget(a,4096);}
static void play(VmInstance *a, uint16_t id) {
    vm_output_cancel(a);
    a->playing=false; a->animation_error[0]=0;
    bool initial=a->control.enabled && !a->control.initialized;
    uint64_t started=platform_time_us();
    const char * error=a->portable?animation_quiesce(a):NULL;
    if(!error)error=prepare_animation(a);
    if (error) {
        snprintf(a->animation_error,sizeof(a->animation_error),"%s",error);
        reply(a,id,execution_status(a,error),0,error);
        return;
    }
    if (initial || a->portable) {
        show_frame(a,started,true,false);
    }
    a->playing=true; a->frame_count=0;
    a->last_frame=platform_time_us(); a->next_frame=a->last_frame;
    reply(a,id,STATUS_OK,(a->target ? a->target->count : 0),"animation started");
}
static void frame_step_impl(VmInstance *a, uint16_t id, uint32_t delta_ms) {
    if(a->portable){
        const char *error=animation_preflight(a,(uint64_t)delta_ms*1000u);
        if(error){reply(a,id,execution_status(a,error),0,error);return;}
    }
    vm_output_cancel(a);
    a->animation_error[0]=0;
    uint64_t started=platform_time_us();
    const char * error=a->portable?animation_quiesce(a):NULL;
    if(!error)error=prepare_animation(a);
    if (!error) error=render_animation(a,delta_ms);
    if (error) {
        reply(a,id,execution_status(a,error),0,error);
        return;
    }
    show_frame(a,started,a->control.enabled,false);
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
static void frame_step(VmInstance *a,uint16_t id,uint32_t delta_ms) {
    const void *previous=runtime_enter(a);
    frame_step_impl(a,id,delta_ms);
    runtime_leave(previous);
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
    case CMD_BEGIN: case CMD_CHUNK: case CMD_FINISH:
        reply(a,id,STATUS_UPLOAD,0,"uploads require the shared flash installer");break;
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
        candidate->code_pin=-1;candidate->memory=bytes;candidate->expected=size;
        error=vm_load(candidate);
    }
    control_destroy(candidate);demo_free(candidate);return error;
}

const char *vm_validate_stored(Store *store,const StoreEntry *entry) {
    VmInstance candidate;vm_init(&candidate,NULL,NULL,NULL);
    if(!vm_bind_storage(&candidate,store,entry))return "package reference limit exceeded";
    const char *error=vm_load(&candidate);control_destroy(&candidate);return error;
}

const char *vm_load(VmInstance *a) {
    const void *previous=runtime_enter(a);
    const char *error=vm_load_impl(a);
    runtime_leave(previous);return error;
}
