#include "composition.h"
#include "animation.h"
#include "draw.h"
#include "runtime.h"
#include "render_executor.h"
#include "vm_allocator.h"
#include <stdio.h>
#include <string.h>

Composition *composition_create(PixelBuffer *output,unsigned layers) {
    if(!output||!output->count||layers>COMPOSITION_LAYER_LIMIT)return NULL;
    Composition *s=demo_calloc(1,sizeof(*s)+(size_t)layers*sizeof(CompositionLayer));
    if(!s)return NULL;
    s->output=output;s->count=output->count;s->layer_count=layers;s->playing=true;s->dirty=true;
    s->allocation=demo_calloc(1,(size_t)s->count*6);
    if(!s->allocation){demo_free(s);return NULL;}
    if(output->type&&!vm_heap_charge((size_t)s->count*3)){demo_free(s->allocation);demo_free(s);return NULL;}
    s->working=s->allocation;s->front=s->allocation+(size_t)s->count*3;
    for(unsigned i=0;i<layers;i++){s->layers[i].target.count=s->count;s->layers[i].opacity=255;}
    return s;
}
void composition_destroy(Composition *s) {
    if(!s)return;
    if(s->pending)s->output->type->cancel(s->output->driver);
    for(unsigned i=0;i<s->layer_count;i++)control_destroy(&s->layers[i].vm);
    if(s->output->type)vm_heap_uncharge((size_t)s->count*3);
    demo_free(s->allocation);demo_free(s);
}
void composition_offscreen(Composition *s,PixelBuffer *target) {
    if(s->pending){s->output->type->cancel(s->output->driver);s->pending=false;}
    if(s->output->type)vm_heap_uncharge((size_t)s->count*3);
    s->output=target;composition_invalidate(s);
}
void composition_publish(Composition *s) {
    uint8_t *old=s->front;s->front=s->working;s->working=old;s->prepared=false;s->frames++;
}
CompositionLayer *composition_layer(Composition *s,const char *id) {
    if(!s)return NULL;
    for(unsigned i=0;i<s->layer_count;i++)if(!strcmp(s->layers[i].id,id))return &s->layers[i];
    return NULL;
}
void composition_invalidate(Composition *s) { if(s){s->composing=false;s->prepared=false;s->dirty=true;} }
static void compose_start(Composition *s) {
    s->pixel=0;s->composing=true;s->dirty=false;
    for(unsigned i=0;i<s->layer_count;i++)s->layers[i].sampled_frame=s->layers[i].vm.control.contribution_frame;
}
static void compose_pixels(void *context,unsigned first,unsigned end){
    Composition *s=context;
    uint8_t *dst=s->working+(size_t)first*3;
    unsigned count=end-first;memset(dst,0,(size_t)count*3);
    for(unsigned n=0;n<s->layer_count;n++){
        CompositionLayer *layer=&s->layers[n];VmInstance *vm=&layer->vm;
        if(!vm->control.contribution_valid||!layer->opacity)continue;
        const uint8_t *src=control_frame(vm)+(size_t)first*4;
        draw_composite(dst,3,dst,3,src,4,count,draw_opacity(layer->opacity),(DrawBlend)layer->blend);
    }
}
static void compose_slice(Composition *s) {
    uint64_t started=platform_time_us();
    unsigned end=s->pixel+128;if(end>s->count)end=s->count;
    render_ranges(compose_pixels,s,s->pixel,end,64);
    s->pixel=end;s->composite_us+=platform_time_us()-started;
    if(end==s->count){s->composing=false;s->prepared=true;s->composite_frames++;}
}
void composition_prepare(Composition *s) {
    compose_start(s);
    while(s->composing){compose_slice(s);runtime_checkpoint();}
}
static void complete_output(Composition *s) {
    if(!s->pending)return;
    int status=s->output->type->poll(s->output->driver);
    if(!status)return;
    s->pending=false;
    if(status<0){s->output->type->cancel(s->output->driver);snprintf(s->error,sizeof(s->error),"native composition output failed; retrying");s->dirty=true;return;}
    memcpy(s->output->rgb,s->front,(size_t)s->count*3);
    s->frames++;s->output_us+=platform_time_us()-s->output_started;s->error[0]=0;
}
static void composition_tick_impl(Composition *s,unsigned budget) {
    complete_output(s);
    if(s->command_active||!s->playing)return;
    /* A composite pins every committed layer. Autonomous writers are deferred
       until the bounded composite finishes; management invalidates it first. */
    if(s->composing){compose_slice(s);return;}
    /* The completed composite no longer reads the layer contributions. Let
       suspended renders finish while it waits, without starting another frame
       on fast layers. Both composite buffers remain stable for their readers. */
    if(s->prepared){
        for(unsigned i=0;i<s->layer_count;i++){
            VmInstance *vm=&s->layers[i].vm;
            if(vm->control.in_step&&!runtime_owns(vm))vm_tick_budget(vm,budget);
        }
        if(s->pending||platform_time_us()<s->next_present)return;
        if(!s->output->type){if(!s->front_pinned)composition_publish(s);return;}
        uint8_t *next=s->front;s->front=s->working;s->working=next;s->prepared=false;
        s->output_started=platform_time_us();
        if(pixels_submit(s->output,s->front,s->count)){s->pending=true;s->next_present=s->output_started+FRAME_INTERVAL_US;complete_output(s);}
        else{snprintf(s->error,sizeof(s->error),"native composition output failed; retrying");s->dirty=true;}
    }
    unsigned first=s->layer_count?s->cursor++%s->layer_count:0;
    for(unsigned i=0;i<s->layer_count;i++){
        CompositionLayer *l=&s->layers[(first+i)%s->layer_count];
        if(!runtime_owns(&l->vm))vm_tick_budget(&l->vm,budget);
        if(l->vm.control.contribution_frame!=l->sampled_frame)s->dirty=true;
    }
    uint64_t now=platform_time_us();
    if(!s->prepared&&s->dirty&&now>=s->next_frame){s->next_frame=now+FRAME_INTERVAL_US;compose_start(s);compose_slice(s);}
}
void composition_tick(Composition *s,unsigned budget) {
    if(s->ticking)return;
    s->ticking=true;composition_tick_impl(s,budget);s->ticking=false;
}
void composition_play(Composition *s,bool playing) {
    s->playing=playing;
    if(playing){
        uint64_t now=platform_time_us();
        for(unsigned i=0;i<s->layer_count;i++){s->layers[i].vm.last_frame=now;s->layers[i].vm.next_frame=now;}
        s->dirty=true;
    }
}
const char *composition_frame(Composition *s,uint32_t delta_ms) {
    s->command_active=true;composition_invalidate(s);
    const char *error=NULL;
    for(unsigned i=0;i<s->layer_count&&!error;i++){
        VmInstance *vm=&s->layers[i].vm;
        if(vm->portable)error=animation_quiesce(vm);
        if(!error)error=control_step(vm,(uint64_t)delta_ms*1000u);
    }
    if(!error){
        composition_prepare(s);
        if(s->pending){s->output->type->cancel(s->output->driver);s->pending=false;}
        if(!s->output->type)composition_publish(s);
        else if(!pixels_show_frame(s->output,s->working,s->count))error="native composition output failed";
        else{memcpy(s->output->rgb,s->working,(size_t)s->count*3);s->frames++;s->prepared=false;}
    }
    s->command_active=false;return error;
}
void composition_remove_layer(Composition *s,unsigned index) {
    composition_invalidate(s);control_destroy(&s->layers[index].vm);
    memmove(&s->layers[index],&s->layers[index+1],(s->layer_count-index-1)*sizeof(CompositionLayer));
    memset(&s->layers[--s->layer_count],0,sizeof(CompositionLayer));
    for(unsigned i=0;i<s->layer_count;i++){s->layers[i].vm.target=&s->layers[i].target;vm_rebind(&s->layers[i].vm);}
}
