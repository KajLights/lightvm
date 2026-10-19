#include "transition.h"
#include "animation.h"
#include "draw.h"
#include "runtime.h"
#include "vm_allocator.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const char *transition_effect_name(TransitionEffect effect) {
    static const char *const names[]={"crossfade","wipe","soft_wipe","dissolve","dip_black","white_flash","custom","program"};
    return effect<TRANSITION_EFFECT_COUNT?names[effect]:"";
}
bool transition_percentage(double value){return isfinite(value)&&value>=0&&value<=100;}
Transition *transition_create(PixelBuffer *output){
    if(!output||!output->count)return NULL;
    Transition *t=demo_calloc(1,sizeof(*t));if(!t)return NULL;
    t->allocation=demo_calloc(output->count,6);
    if(!t->allocation){demo_free(t);return NULL;}
    if(!vm_heap_charge((size_t)output->count*3)){demo_free(t->allocation);demo_free(t);return NULL;}
    t->output=output;t->target.count=output->count;t->working=t->allocation;t->front=t->working+(size_t)output->count*3;
    t->playing=true;t->dirty=true;return t;
}
/* Resource descriptors are fixed at admission. Only register values and owned
 * payload can change during evaluation; don't checkpoint immutable pointers and
 * layout descriptors between them. A chunk crosses at most these two spans. */
static size_t saved_bytes(const Portable *p){return p->register_bytes+p->payload_bytes;}
static void state_copy(TransitionProgram *program,size_t offset,size_t count,bool restore){
    Portable *p=program->vm.portable;
    while(count){
        size_t n=count;uint8_t *state;
        if(offset<p->register_bytes){
            if(n>p->register_bytes-offset)n=p->register_bytes-offset;
            state=p->values+offset;
        }else state=(uint8_t *)p->state+p->state_bytes-p->payload_bytes+offset-p->register_bytes;
        if(restore)memcpy(state,program->baseline+offset,n);
        else memcpy(program->baseline+offset,state,n);
        offset+=n;count-=n;
    }
}
bool transition_program_capture(TransitionProgram *program){
    Portable *p=program->vm.portable;size_t bytes=saved_bytes(p);
    program->baseline=demo_calloc(1,bytes);if(!program->baseline)return false;
    for(size_t offset=0;offset<bytes;offset+=256){size_t n=bytes-offset;if(n>256)n=256;state_copy(program,offset,n,false);runtime_checkpoint();}
    program->random_state=p->random_state;return true;
}
void transition_program_destroy(TransitionProgram *p){if(p){control_destroy(&p->vm);demo_free(p->baseline);demo_free(p);}}
void transition_destroy(Transition *t){
    if(!t)return;
    if(t->pending)t->output->type->cancel(t->output->driver);
    for(unsigned n=0;n<2;n++)transition_program_destroy(t->programs[n]);
    demo_free(t->input);
    vm_heap_uncharge((size_t)t->output->count*3);demo_free(t->allocation);demo_free(t);
}
/* Progress updates affect the next evaluation. A pinned, unfinished evaluation
 * remains coherent and can finish even under a continuous stream of updates. */
static void unpin_compositions(Transition *t){for(unsigned n=0;n<2;n++)if(t->compositions[n])t->compositions[n]->front_pinned=false;}
void transition_invalidate(Transition *t){if(t){unpin_compositions(t);t->composing=false;t->prepared=false;t->dirty=true;}}
void transition_update(Transition *t,uint64_t now){
    if(t->start_pending)return;
    if(t->driver==TRANSITION_INPUT){
        if(input_port_capture(t->input_registry,t->input,now)){t->progress=(double)t->input->snapshot.f*100;t->dirty=true;}
        return;
    }
    if(t->driver!=TRANSITION_TIMED)return;
    double elapsed=now>=t->started?(double)(now-t->started):0;
    double next=input_ramp(t->start,t->target_progress,elapsed,t->duration,t->interval,t->interval?3:1);
    if(next!=t->progress){t->progress=next;t->dirty=true;}
    if(elapsed>=t->duration&&!t->interval)t->driver=TRANSITION_MANUAL;
}
static bool unassign(Transition *t){
    if(t->assignment==UINT32_MAX)return false;
    demo_free(t->input);t->input=NULL;
    t->assignment++;t->driver=TRANSITION_MANUAL;t->source[0]=0;t->start_pending=false;return true;
}
bool transition_set(Transition *t,double value){
    if(!transition_percentage(value)||t->assignment==UINT32_MAX)return false;
    unassign(t);t->progress=value;t->dirty=true;return true;
}
void transition_hold(Transition *t){transition_update(t,platform_time_us());(void)unassign(t);}
bool transition_run(Transition *t,double target,double duration_ms){return transition_run_interval(t,target,duration_ms,0);}
bool transition_run_interval(Transition *t,double target,double duration_ms,double interval_ms){
    if(!transition_percentage(target)||!isfinite(duration_ms)||duration_ms<0||!isfinite(duration_ms*1000)||
       !isfinite(interval_ms)||interval_ms<0||!isfinite(interval_ms*1000)||(interval_ms>0&&interval_ms<duration_ms)||t->assignment==UINT32_MAX)return false;
    uint64_t now=platform_time_us();transition_update(t,now);unassign(t);
    t->interval=interval_ms*1000;
    if(!duration_ms&&!interval_ms){t->progress=target;t->dirty=true;return true;}
    t->start=t->progress;t->target_progress=target;t->started=now;t->duration=duration_ms*1000;t->driver=TRANSITION_TIMED;return true;
}
bool transition_assign(Transition *t,const char *source){
    if(!object_name_valid(source)||t->assignment==UINT32_MAX)return false;
    transition_update(t,platform_time_us());unassign(t);strcpy(t->source,source);t->driver=TRANSITION_SOURCE;return true;
}
bool transition_source(Transition *t,const char *source,uint32_t assignment,double value){
    if(t->driver!=TRANSITION_SOURCE||assignment!=t->assignment||strcmp(t->source,source)||!transition_percentage(value))return false;
    t->progress=value;t->dirty=true;return true;
}
const char *transition_bind(Transition *t,InputRegistry *registry,const Json *j,int object){
    if(t->assignment==UINT32_MAX)return "driver assignment exhausted";
    InputPort *port=demo_calloc(1,sizeof(*port));if(!port)return "transition input exceeds peak memory budget";
    strcpy(port->name,"progress");port->type=T_FLOAT;port->bounds=3;port->maximum.f=1;
    port->initial.f=port->snapshot.f=port->driver.value.f=(float)(t->progress/100);
    const char *error=input_port_bind(registry,port,j,object,platform_time_us());
    if(error){demo_free(port);return error;}
    if(port->driver.kind==INPUT_SOURCE&&json_get(j,object,"fallback")<0)port->driver.hold=true;
    unassign(t);t->input=port;t->input_registry=registry;t->driver=TRANSITION_INPUT;
    transition_update(t,platform_time_us());return NULL;
}
static void start(Transition *t){
    t->sampled_progress=t->progress;t->pixel=0;t->phase=0;t->restore_offset=0;t->dirty=false;t->composing=true;
    for(unsigned n=0;n<2;n++){
        t->sampled_frames[n]=t->compositions[n]?t->compositions[n]->frames:0;
        if(t->compositions[n])t->compositions[n]->front_pinned=true;
    }
    if(t->effect==TRANSITION_PROGRAM&&t->programs[0]){
        Portable *p=t->programs[0]->vm.portable;p->progress=(float)(t->sampled_progress/100);
        uint64_t now=platform_time_us();inputs_capture(p,now>=p->input_epoch?now-p->input_epoch:0,false);
        for(unsigned n=0;n<2;n++)p->composition_frames[n]=t->compositions[n]?t->compositions[n]->front:NULL;
    }
}
static uint32_t threshold(uint32_t pixel,uint32_t seed){
    uint32_t x=pixel^seed^0x9e3779b9u;x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;
}
static void compose(Transition *t){
    unsigned end=t->pixel+32;if(end>t->target.count)end=t->target.count;
    double p=t->sampled_progress/100;
    for(unsigned pixel=t->pixel;pixel<end;pixel++){
        if(p==0||p==1||t->effect==TRANSITION_PROGRAM){
            const uint8_t *src=NULL;
            if(p==0||p==1){Composition *s=t->compositions[p==1];if(s)src=s->front+(size_t)pixel*3;}
            else if(t->programs[0])src=control_frame(&t->programs[0]->vm)+(size_t)pixel*4;
            if(src)memcpy(t->working+(size_t)pixel*3,src,3);else memset(t->working+(size_t)pixel*3,0,3);
            continue;
        }
        double reveal=p,black=1,white=0;
        const uint8_t *mask=t->programs[0]?control_frame(&t->programs[0]->vm)+(size_t)pixel*4:NULL;
        if(p>0&&p<1)switch(t->effect){
            case TRANSITION_WIPE:reveal=(double)pixel/t->target.count<p?1:0;break;
            case TRANSITION_SOFT_WIPE:reveal=fmin(1,fmax(0,(p*1.2-(pixel+0.5)/t->target.count)/0.2));break;
            case TRANSITION_DISSOLVE:reveal=(threshold(pixel,t->seed)+0.5)/4294967296.0<p?1:0;break;
            case TRANSITION_DIP:reveal=p<0.5?0:1;black=fabs(2*p-1);break;
            case TRANSITION_FLASH:reveal=p<0.5?0:1;white=1-fabs(2*p-1);break;
            case TRANSITION_CUSTOM:if(mask)reveal=mask[0]/255.0;break;
            default:break;
        }
        uint8_t result[4];
        const uint8_t *a=t->compositions[0]?t->compositions[0]->front+(size_t)pixel*3:NULL;
        const uint8_t *b=t->compositions[1]?t->compositions[1]->front+(size_t)pixel*3:NULL;
        draw_composite_rgb(result,4,a,b,1,draw_opacity_float((float)reveal));
        if(black<1){
            const uint8_t dark[3]={0};
            draw_composite(result,4,dark,3,result,4,1,draw_opacity_float((float)black),DRAW_BLEND_OVER);
        }
        if(white>0){
            const uint8_t light[3]={255,255,255};
            draw_composite(result,4,result,4,light,3,1,draw_opacity_float((float)white),DRAW_BLEND_OVER);
        }
        if(p>0&&p<1&&t->effect==TRANSITION_CUSTOM&&t->programs[1])draw_blend(result,control_frame(&t->programs[1]->vm)+(size_t)pixel*4,255,DRAW_BLEND_OVER);
        memcpy(t->working+(size_t)pixel*3,result,3);
    }
    t->pixel=end;
    if(end==t->target.count){unpin_compositions(t);t->composing=false;t->prepared=true;t->composite_frames++;}
}
static void slice(Transition *t,unsigned budget){
    uint64_t begun=platform_time_us();
    bool restored=false,ran=false;
    /* Reset all custom state to its admitted baseline, in bounded chunks. This
       makes seeking independent of previous percentages, including guest RNG. */
    while(t->phase<4){
        unsigned n=t->phase/2;TransitionProgram *program=t->programs[n];
        if((t->effect!=TRANSITION_CUSTOM&&t->effect!=TRANSITION_PROGRAM)||t->sampled_progress==0||t->sampled_progress==100||!program){t->phase=(n+1)*2;continue;}
        VmInstance *vm=&program->vm;Portable *p=vm->portable;
        if(!(t->phase%2)){
            if(restored)break;
            restored=true;
            size_t bytes=saved_bytes(p),left=bytes-t->restore_offset;if(left>256)left=256;
            state_copy(program,t->restore_offset,left,true);t->restore_offset+=(unsigned)left;
            if(t->restore_offset==bytes){t->restore_offset=0;t->phase++;p->random_state=program->random_state;p->status=1;p->bulk=0;p->native_pending=false;vm->control.program_us=0;vm->control.in_step=false;}
        }else{
            if(ran)break;
            ran=true;
            bool ready=false;const char *error=animation_poll_budget(vm,p->profile==4?0:(uint64_t)(t->sampled_progress*10000),&ready,budget);
            if(error){snprintf(t->error,sizeof(t->error),"transition program: %.70s",error);unpin_compositions(t);t->composing=false;return;}
            if(ready){
                t->phase++;
                /* A Python program has finished reading both compositions. Its
                   committed RGBA now owns the result during RGB conversion. */
                if(t->effect==TRANSITION_PROGRAM)unpin_compositions(t);
            }
        }
    }
    /* At most one 256-byte restore, one guest budget and one 32-pixel copy.
       Finished phases fall through without another full scheduler round. */
    if(t->phase==4)compose(t);
    t->composite_us+=platform_time_us()-begun;
}
static void complete(Transition *t){
    if(!t->pending)return;
    int status=t->output->type->poll(t->output->driver);if(!status)return;t->pending=false;
    if(status<0){t->output->type->cancel(t->output->driver);strcpy(t->error,"native transition output failed; retrying");t->dirty=true;return;}
    memcpy(t->output->rgb,t->front,(size_t)t->target.count*3);t->frames++;t->output_us+=platform_time_us()-t->output_started;t->error[0]=0;
}
void transition_tick(Transition *t,unsigned budget){
    if(t->ticking)return;
    if(t->start_pending){
        if(t->command_active||!t->playing)return;
        t->started=platform_time_us();t->start_pending=false;
    }
    t->ticking=true;complete(t);transition_update(t,platform_time_us());
    if(t->command_active||!t->playing){t->ticking=false;return;}
    if(t->composing){
        for(unsigned n=0;n<2;n++)if(t->compositions[n])composition_tick(t->compositions[n],budget);
        slice(t,budget);t->ticking=false;return;
    }
    for(unsigned n=0;n<2;n++)if(t->compositions[n]){composition_tick(t->compositions[n],budget);if(t->compositions[n]->frames!=t->sampled_frames[n])t->dirty=true;}
    if(t->effect==TRANSITION_PROGRAM&&t->programs[0]){
        Portable *p=t->programs[0]->vm.portable;uint64_t now=platform_time_us();
        if(inputs_changed(p,now>=p->input_epoch?now-p->input_epoch:0))t->dirty=true;
    }
    if(t->prepared&&!t->pending){
        uint8_t *previous=t->front;t->front=t->working;t->working=previous;t->prepared=false;t->output_started=platform_time_us();
        if(t->output->type->submit(t->output->driver,t->front,t->target.count)){t->pending=true;complete(t);}
        else{strcpy(t->error,"native transition output failed; retrying");t->dirty=true;}
    }
    uint64_t now=platform_time_us();
    if(!t->prepared&&t->dirty&&(t->effect!=TRANSITION_PROGRAM||now>=t->next_frame)){
        t->next_frame=now+FRAME_INTERVAL_US;start(t);slice(t,budget);
    }
    t->ticking=false;
}
void transition_play(Transition *t,bool playing){
    t->playing=playing;
    if(playing)for(unsigned n=0;n<2;n++)if(t->compositions[n])composition_play(t->compositions[n],t->compositions[n]->playing);
    if(playing)t->dirty=true;
}
const char *transition_frame(Transition *t,uint32_t delta_ms){
    t->command_active=true;transition_invalidate(t);const char *error=NULL;
    for(unsigned n=0;n<2&&!error;n++)if(t->compositions[n])error=composition_frame(t->compositions[n],delta_ms);
    if(!error){
        transition_update(t,platform_time_us());start(t);
        while(t->composing){slice(t,64);runtime_checkpoint();}
        if(!t->prepared)error=t->error;
        else{
            if(t->pending){t->output->type->cancel(t->output->driver);t->pending=false;}
            if(!t->output->type->show(t->output->driver,t->working,t->target.count))error="native transition output failed";
            else{memcpy(t->output->rgb,t->working,(size_t)t->target.count*3);t->frames++;t->prepared=false;}
        }
    }
    t->command_active=false;return error;
}
