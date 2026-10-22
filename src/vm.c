#include "vm.h"
#include "release.h"
#include "vm_allocator.h"
#include "runtime.h"
#include "pixels.h"
#include "animation.h"
#include "render_executor.h"
#include <assert.h>
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
    a->output_pending=false;a->frame_ready=false;a->view.sealed=false;a->next_present=0;
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
            pixels_show_frame(a->output,a->control.working,a->control.count)) {
            memset(a->output->rgb,0,n); a->control.output_valid=true;
            ++a->control.output_frame;
        } else snprintf(a->animation_error,sizeof(a->animation_error),"native pixel output failed");
    } else if (!a->control.enabled&&a->output) { pixels_clear(a->output); pixels_show(a->output); }
}
void vm_destroy(VmInstance *a) {
    /* Destruction discards the instance. Finishing a suspended guest here could
       make cancellation/configuration wait forever for a loop with no end. */
    vm_output_cancel(a);a->playing=false;
    if(a->output){pixels_clear(a->output);pixels_show(a->output);}
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
}
static const char *vm_load_impl(VmInstance *a) {
    if(a->generation==UINT32_MAX)return "program generation exhausted";
    const char *error=pbc_open(&a->portable,vm_read,a,a->expected);
    if(!error)error=animation_validate(a->portable);
    if(!error)control_loaded(a);
    return error;
}
static const char *prepare_animation(VmInstance *a) { return control_prepare(a); }
static const char *render_animation(VmInstance *a,uint32_t delta_ms) {
    return control_step(a,(uint64_t)delta_ms*1000u);
}
void control_presented(VmInstance *a,bool success) {
    if(success)a->animation_error[0]=0;
    else snprintf(a->animation_error,sizeof(a->animation_error),"native pixel output failed; retrying");
}
void control_wait(VmInstance *a,unsigned kind){
    Control *c=&a->control;uint64_t now=platform_time_us();
    if(c->wait_since_us&&now>=c->wait_since_us)c->wait_us[c->wait_kind]+=now-c->wait_since_us;
    c->wait_since_us=now;c->wait_kind=(uint8_t)kind;
}
void control_observe(VmInstance *a,uint64_t time){
    if(!a->portable)return;
    Control *c=&a->control;uint32_t values[8]={0};uint8_t fallback;
    inputs_sample(a->portable,time,values,&fallback);
    if(!c->observed||memcmp(values,c->observed_inputs,sizeof(values))||fallback!=c->observed_fallback){
        if(c->dependency_revision!=UINT32_MAX)c->dependency_revision++;
        if(!c->dirty){c->dirty_since_us=platform_time_us();c->dirty=true;}
        memcpy(c->observed_inputs,values,sizeof(values));c->observed_fallback=fallback;c->observed=true;
    }
}
void control_contribution_presented(VmInstance *a,ContributionStamp stamp,uint64_t now){
    if(!stamp.frame||stamp.generation!=a->generation)return;
    if(stamp.frame!=a->control.displayed.frame||stamp.generation!=a->control.displayed.generation){
        a->control.distinct_presentations++;
        a->control.presented_us=now;
        if(stamp.dependency_revision&&
           stamp.dependency_revision!=a->control.displayed.dependency_revision){
            a->control.dirty_to_display_us=now-stamp.dirty_since_us;
            if(stamp.dependency_revision==a->control.dependency_revision)a->control.dirty=false;
        }
    }
    a->control.displayed=stamp;
}
static void show_frame(VmInstance *a,bool asynchronous) {
    uint64_t show_started=platform_time_us();
    bool success;
    // Each output presents its own complete RGB frame and retains an
    // independent committed contribution.
    if (!a->output) { control_presented(a,true);a->show_us=0; ++a->frame_count; return; }
    const uint8_t *frame=control_output_frame(a);
    a->control.submitted=a->control.contribution;
    if(asynchronous){
        if(a->output->driver&&pixels_submit(a->output,frame,a->control.count)){
            a->next_present=show_started+FRAME_INTERVAL_US;
            a->output_pending=true;a->output_started=show_started;return;
        }
        vm_output_cancel(a);
        if(a->output->driver)a->output->type->cancel(a->output->driver);
        control_presented(a,false);return;
    }
    success=a->output->driver && a->output->type && a->output->rgb &&
        a->output->count==a->control.count &&
        pixels_show_frame(a->output,frame,a->control.count);
    if (success) memcpy(a->output->rgb,frame,(size_t)a->control.count*3);
    control_presented(a,success);
    a->show_us=(uint32_t)(platform_time_us()-show_started);
    if (a->control.enabled && success) {
        a->control.output_valid=true; ++a->control.output_frame;
    }
    if(success){++a->frame_count;control_contribution_presented(a,a->control.submitted,platform_time_us());}
}
static bool complete_output(VmInstance *a) {
    if(a->output_pending){
        int status=a->output->type->poll(a->output->driver);
        if(!status)return false;
        a->view.sealed=false;a->output_pending=false;
        if(status<0){a->output->type->cancel(a->output->driver);control_presented(a,false);return false;}
        memcpy(a->output->rgb,a->control.presentation,(size_t)a->control.count*3);
        control_presented(a,true);a->control.output_valid=true;a->control.output_frame++;a->frame_count++;
        control_contribution_presented(a,a->control.submitted,platform_time_us());
        a->show_us=(uint32_t)(platform_time_us()-a->output_started);
    }
    return true;
}
static uint64_t tick_epoch;
static bool batching;
static unsigned queued;
static AnimationSlice slices[2];
static RenderJob jobs[2];
static void tick_error(VmInstance *a,const char *error){
    a->playing=false;
    snprintf(a->animation_error,sizeof(a->animation_error),"%s",error);
}
static bool tick_prepare(VmInstance *a,unsigned budget,AnimationSlice *slice){
    (void)complete_output(a);
    if(a->frame_ready){
        if(a->output_pending||platform_time_us()<a->next_present){control_wait(a,3);return false;}
        a->frame_ready=false;show_frame(a,true);(void)complete_output(a);
        /* One complete waiting frame is the entire render-ahead queue. */
        return false;
    }
    if (!a->playing){control_wait(a,0);return false;}
    uint64_t now=platform_time_us();
    bool continuing=a->control.in_step;
    if (!continuing&&now<a->next_frame) return false;
    uint64_t elapsed_us=continuing?0:now-a->last_frame;
    /* Cached pixels do not stop the program clock. Advance both the virtual
       clock and its wall anchor when sleeping, so binding a new ramp starts
       now rather than at an arbitrarily old render. This proof excludes every
       render observer of delta/time; stateful invocations never take this path. */
    if(!continuing&&a->portable->render_reusable&&a->control.contribution_valid&&a->control.contribution.frame&&
       !inputs_changed(a->portable,a->control.program_us+elapsed_us)){
        const char *error=animation_preflight(a,elapsed_us);
        if(!error&&a->portable->active_wall_us>UINT64_MAX-elapsed_us)error="wall clock overflow";
        if(error){tick_error(a,error);return false;}
        control_wait(a,4);a->control.program_us+=elapsed_us;a->portable->active_wall_us+=elapsed_us;a->last_frame=now;
        return false;
    }
    // Zero interval is demand driven; an optional cap spaces render starts.
    if(!continuing){
        a->last_frame=now;
        a->next_frame+=FRAME_INTERVAL_US;
        if (a->next_frame<=now) a->next_frame=now+FRAME_INTERVAL_US;
    }
    const char *error=animation_begin(a,elapsed_us);
    if(error){tick_error(a,error);return false;}
    control_wait(a,0);*slice=(AnimationSlice){a,budget,0};return true;
}
static void tick_commit(AnimationSlice *slice,uint64_t elapsed){
    VmInstance *a=slice->vm;bool ready=false;
    const char *error=animation_commit(slice,elapsed,&ready);
    if(error){tick_error(a,error);return;}
    control_wait(a,1);
    if(ready){
        a->frame_ready=true;
        if(!a->output_pending&&platform_time_us()>=a->next_present){a->frame_ready=false;show_frame(a,true);(void)complete_output(a);}
    }
}
void vm_tick_begin(void){assert(!batching&&!queued);runtime_defer_begin();runtime_epoch_begin();batching=true;tick_epoch++;}
void vm_tick_flush(void){
    render_execute(jobs,queued);
    for(unsigned i=0;i<queued;i++)tick_commit(&slices[i],jobs[i].elapsed_us);
    queued=0;
}
void vm_tick_collect(VmInstance *a,unsigned budget){
    assert(batching);
    if(a->scheduled_epoch==tick_epoch||runtime_owns(a))return;
    a->scheduled_epoch=tick_epoch;
    control_observe(a,a->control.program_us+(platform_time_us()>=a->last_frame?platform_time_us()-a->last_frame:0));
    /* Leave part of the epoch for composition/presentation; otherwise a full
       runnable set could consume every epoch before producing any output. */
    if(!runtime_epoch_can_collect()){control_wait(a,a->playing?1:0);return;}
    if(tick_prepare(a,budget,&slices[queued])){
        jobs[queued]=(RenderJob){animation_execute,&slices[queued],0};
        if(++queued==2)vm_tick_flush();
    }
}
void vm_tick_end(void){vm_tick_flush();batching=false;runtime_epoch_end();runtime_defer_end();}
bool vm_tick_batching(void){return batching;}
void vm_tick_budget(VmInstance *a,unsigned budget) {
    if(batching&&a->scheduled_epoch==tick_epoch)return;
    if(batching)a->scheduled_epoch=tick_epoch;
    if(!runtime_epoch_available())return;
    AnimationSlice slice;
    if(!tick_prepare(a,budget,&slice))return;
    RenderJob job={animation_execute,&slice,0};render_execute(&job,1);
    tick_commit(&slice,job.elapsed_us);
}
void vm_tick(VmInstance *a){vm_tick_budget(a,4096);}
static void play(VmInstance *a, uint16_t id) {
    vm_output_cancel(a);
    a->playing=false; a->animation_error[0]=0;
    const char * error=a->portable?animation_quiesce(a):NULL;
    if(!error)error=prepare_animation(a);
    if (error) {
        snprintf(a->animation_error,sizeof(a->animation_error),"%s",error);
        reply(a,id,STATUS_EXECUTION,0,error);
        return;
    }
    show_frame(a,false);
    a->playing=true; a->frame_count=0;
    a->last_frame=platform_time_us(); a->next_frame=a->last_frame;
    reply(a,id,STATUS_OK,(a->target ? a->target->count : 0),"animation started");
}
static void frame_step_impl(VmInstance *a, uint16_t id, uint32_t delta_ms) {
    if(a->portable){
        const char *error=animation_preflight(a,(uint64_t)delta_ms*1000u);
        if(error){reply(a,id,STATUS_EXECUTION,0,error);return;}
    }
    vm_output_cancel(a);
    a->animation_error[0]=0;
    const char * error=a->portable?animation_quiesce(a):NULL;
    if(!error)error=prepare_animation(a);
    if (!error) error=render_animation(a,delta_ms);
    if (error) {
        reply(a,id,STATUS_EXECUTION,0,error);
        return;
    }
    show_frame(a,false);
    if (a->animation_error[0]) { reply(a,id,STATUS_EXECUTION,0,a->animation_error); return; }
    const uint8_t *bytes=a->output ? a->output->rgb : a->control.committed;
    size_t frame_bytes=!a->output?control_frame_bytes(a):(size_t)a->target->count*3;
    for (size_t offset=0; offset<frame_bytes; offset+=FRAME_LIMIT) {
        size_t count=frame_bytes-offset;
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
        snprintf(message,sizeof(message),"version=%s protocol=%u platform=%.16s board=%.24s output=%.16s ready=%u uploading=%u received=%lu module_limit=%u heap=%lu heap_peak=%lu heap_limit=%u pin=%ld pixels=%lu brightness=%lu frame_interval_us=%u mode=%s code_storage=%s frames=%lu render_us=%lu show_us=%lu generation=%lu animation_error=",
                 LIGHTVM_VERSION,LIGHTVM_PROTOCOL_VERSION,
                 info->platform,info->board,info->output,
                 a->ready,a->uploading,(unsigned long)a->received,MODULE_LIMIT,
                 (unsigned long)vm_heap_used(),(unsigned long)vm_heap_peak(),VM_HEAP_LIMIT,
                 (long)(a->target && a->target->count ? a->target->config.pin : -1),(unsigned long)(a->target ? a->target->count : 0),
                 (unsigned long)(a->target && a->target->count ? a->target->config.brightness : 0),FRAME_INTERVAL_US,
                 a->playing ? "bytecode" : "idle", a->ready ? (a->from_storage ? "flash" : "ram") : "none",
                 (unsigned long)a->frame_count,(unsigned long)a->render_us,
                 (unsigned long)a->show_us,(unsigned long)a->generation);
        const char *error=a->animation_error[0]?a->animation_error:"-";
        size_t used=strlen(message),length=strlen(error);
        if(length>=sizeof(message)-used)length=sizeof(message)-used-1;
        memcpy(message+used,error,length);message[used+length]=0;
        reply(a,id,STATUS_OK,0,message); break;
    }
    case CMD_BEGIN: case CMD_CHUNK: case CMD_FINISH:
        reply(a,id,STATUS_UPLOAD,0,"uploads require the shared flash installer");break;
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

typedef struct {Store *store;const StoreEntry *entry;} StoredRead;
static bool stored_read(void *context,uint32_t offset,void *bytes,size_t size){
    StoredRead *r=context;return store_read(r->store,r->entry,offset,bytes,size);
}
const char *vm_validate_stored(Store *store,const StoreEntry *entry) {
    StoredRead read={store,entry};Portable *program=NULL;
    const char *error=pbc_open(&program,stored_read,&read,entry->size);
    if(!error)error=animation_validate(program);
    pbc_destroy(program);return error;
}

const char *vm_load(VmInstance *a) {
    const void *previous=runtime_enter(a);
    const char *error=vm_load_impl(a);
    runtime_leave(previous);return error;
}
