#include "scene.h"
#include "animation.h"
#include "draw.h"
#include "runtime.h"
#include "vm_allocator.h"
#include <stdio.h>
#include <string.h>

Scene *scene_create(PixelBuffer *output,unsigned layers) {
    if(!output||!output->count||layers>SCENE_LAYER_LIMIT)return NULL;
    Scene *s=demo_calloc(1,sizeof(*s)+(size_t)layers*sizeof(SceneLayer));
    if(!s)return NULL;
    s->output=output;s->count=output->count;s->layer_count=layers;s->playing=true;s->dirty=true;
    s->allocation=demo_calloc(1,(size_t)s->count*6);
    if(!s->allocation){demo_free(s);return NULL;}
    if(!vm_heap_charge((size_t)s->count*3)){demo_free(s->allocation);demo_free(s);return NULL;}
    s->working=s->allocation;s->front=s->allocation+(size_t)s->count*3;
    for(unsigned i=0;i<layers;i++){s->layers[i].target.count=s->count;s->layers[i].opacity=255;}
    return s;
}
void scene_destroy(Scene *s) {
    if(!s)return;
    if(s->pending)s->output->type->cancel(s->output->driver);
    for(unsigned i=0;i<s->layer_count;i++)control_destroy(&s->layers[i].vm);
    vm_heap_uncharge((size_t)s->count*3);
    demo_free(s->allocation);demo_free(s);
}
SceneLayer *scene_layer(Scene *s,const char *id) {
    if(!s)return NULL;
    for(unsigned i=0;i<s->layer_count;i++)if(!strcmp(s->layers[i].id,id))return &s->layers[i];
    return NULL;
}
void scene_invalidate(Scene *s) { if(s){s->composing=false;s->prepared=false;s->dirty=true;} }
static void compose_start(Scene *s) {
    s->pixel=0;s->composing=true;s->dirty=false;
    for(unsigned i=0;i<s->layer_count;i++)s->layers[i].sampled_frame=s->layers[i].vm.control.contribution_frame;
}
static void compose_slice(Scene *s) {
    uint64_t started=platform_time_us();
    unsigned end=s->pixel+32;if(end>s->count)end=s->count;
    for(unsigned i=s->pixel;i<end;i++){
        uint8_t result[4]={0,0,0,255};
        for(unsigned n=0;n<s->layer_count;n++){
            SceneLayer *layer=&s->layers[n];VmInstance *vm=&layer->vm;
            if(!vm->control.contribution_valid||!layer->opacity)continue;
            unsigned stride=vm->portable&&vm->portable->profile>=2?4:3;
            const uint8_t *src=control_frame(vm)+(size_t)i*stride;
            uint8_t rgba[4]={src[0],src[1],src[2],stride==4?src[3]:255};
            draw_over(result,rgba,layer->opacity);
        }
        memcpy(s->working+(size_t)i*3,result,3);
    }
    s->pixel=end;s->composite_us+=platform_time_us()-started;
    if(end==s->count){s->composing=false;s->prepared=true;s->composite_frames++;}
}
void scene_prepare(Scene *s) {
    compose_start(s);
    while(s->composing){compose_slice(s);runtime_checkpoint();}
}
static void complete_output(Scene *s) {
    if(!s->pending)return;
    int status=s->output->type->poll(s->output->driver);
    if(!status)return;
    s->pending=false;
    if(status<0){s->output->type->cancel(s->output->driver);snprintf(s->error,sizeof(s->error),"native scene output failed; retrying");s->dirty=true;return;}
    memcpy(s->output->rgb,s->front,(size_t)s->count*3);
    s->frames++;s->output_us+=platform_time_us()-s->output_started;s->error[0]=0;
}
static void scene_tick_impl(Scene *s,unsigned budget) {
    complete_output(s);
    if(s->command_active||!s->playing)return;
    /* A composite pins every committed layer. Autonomous writers are deferred
       until the bounded composite finishes; management invalidates it first. */
    if(s->composing){compose_slice(s);return;}
    unsigned first=s->layer_count?s->cursor++%s->layer_count:0;
    for(unsigned i=0;i<s->layer_count;i++){
        SceneLayer *l=&s->layers[(first+i)%s->layer_count];
        if(!runtime_owns(&l->vm))vm_tick_budget(&l->vm,budget);
        if(l->vm.control.contribution_frame!=l->sampled_frame)s->dirty=true;
    }
    if(s->prepared&&!s->pending){
        uint8_t *next=s->front;s->front=s->working;s->working=next;s->prepared=false;
        s->output_started=platform_time_us();
        if(s->output->type->submit(s->output->driver,s->front,s->count)){s->pending=true;complete_output(s);}
        else{snprintf(s->error,sizeof(s->error),"native scene output failed; retrying");s->dirty=true;}
    }
    uint64_t now=platform_time_us();
    if(!s->prepared&&s->dirty&&now>=s->next_frame){s->next_frame=now+FRAME_INTERVAL_US;compose_start(s);compose_slice(s);}
}
void scene_tick(Scene *s,unsigned budget) {
    if(s->ticking)return;
    s->ticking=true;scene_tick_impl(s,budget);s->ticking=false;
}
void scene_play(Scene *s,bool playing) {
    s->playing=playing;
    if(playing){
        uint64_t now=platform_time_us();
        for(unsigned i=0;i<s->layer_count;i++){s->layers[i].vm.last_frame=now;s->layers[i].vm.next_frame=now;}
        s->dirty=true;
    }
}
const char *scene_frame(Scene *s,uint32_t delta_ms) {
    s->command_active=true;scene_invalidate(s);
    const char *error=NULL;
    for(unsigned i=0;i<s->layer_count&&!error;i++){
        VmInstance *vm=&s->layers[i].vm;
        if(vm->portable)error=animation_quiesce(vm);
        if(!error)error=control_step(vm,1,(uint64_t)delta_ms*1000u);
    }
    if(!error){
        scene_prepare(s);
        if(s->pending){s->output->type->cancel(s->output->driver);s->pending=false;}
        if(!s->output->type->show(s->output->driver,s->working,s->count))error="native scene output failed";
        else{memcpy(s->output->rgb,s->working,(size_t)s->count*3);s->frames++;s->prepared=false;}
    }
    s->command_active=false;return error;
}
void scene_remove_layer(Scene *s,unsigned index) {
    scene_invalidate(s);control_destroy(&s->layers[index].vm);
    memmove(&s->layers[index],&s->layers[index+1],(s->layer_count-index-1)*sizeof(SceneLayer));
    memset(&s->layers[--s->layer_count],0,sizeof(SceneLayer));
    for(unsigned i=0;i<s->layer_count;i++){s->layers[i].vm.target=&s->layers[i].target;vm_rebind(&s->layers[i].vm);}
}
