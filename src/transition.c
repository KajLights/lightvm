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
bool transition_program_capture(TransitionProgram *program){
    Portable *p=program->vm.portable;
    program->baseline=demo_calloc(1,p->state_bytes);if(!program->baseline)return false;
    for(size_t offset=0;offset<p->state_bytes;offset+=256){size_t n=p->state_bytes-offset;if(n>256)n=256;memcpy(program->baseline+offset,(uint8_t *)p->state+offset,n);runtime_checkpoint();}
    program->random_state=p->random_state;return true;
}
void transition_program_destroy(TransitionProgram *p){if(p){control_destroy(&p->vm);demo_free(p->baseline);demo_free(p);}}
void transition_destroy(Transition *t){
    if(!t)return;
    if(t->pending)t->output->type->cancel(t->output->driver);
    for(unsigned n=0;n<2;n++)transition_program_destroy(t->programs[n]);
    vm_heap_uncharge((size_t)t->output->count*3);demo_free(t->allocation);demo_free(t);
}
/* Progress updates affect the next evaluation. A pinned, unfinished evaluation
 * remains coherent and can finish even under a continuous stream of updates. */
static void unpin_scenes(Transition *t){for(unsigned n=0;n<2;n++)if(t->scenes[n])t->scenes[n]->front_pinned=false;}
void transition_invalidate(Transition *t){if(t){unpin_scenes(t);t->composing=false;t->prepared=false;t->dirty=true;}}
void transition_update(Transition *t,uint64_t now){
    if(t->driver!=TRANSITION_TIMED)return;
    double elapsed=now>=t->started?(double)(now-t->started):0;
    double from=t->start,to=t->target_progress,phase=elapsed;
    if(t->interval){
        double leg=floor(elapsed/t->interval);phase=fmod(elapsed,t->interval);
        if(fmod(leg,2)>=1){from=t->target_progress;to=t->start;}
    }
    double u=t->duration?phase/t->duration:1;
    double next=u>=1?to:from+(to-from)*u;
    if(next!=t->progress){t->progress=next;t->dirty=true;}
    if(u>=1&&!t->interval)t->driver=TRANSITION_MANUAL;
}
static bool unassign(Transition *t){
    if(t->assignment==UINT32_MAX)return false;
    t->assignment++;t->driver=TRANSITION_MANUAL;t->source[0]=0;return true;
}
bool transition_set(Transition *t,double value){
    if(!transition_percentage(value)||t->assignment==UINT32_MAX)return false;
    unassign(t);t->progress=value;t->dirty=true;return true;
}
void transition_hold(Transition *t){transition_update(t,platform_time_us());t->driver=TRANSITION_MANUAL;t->source[0]=0;}
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
static void start(Transition *t){
    t->sampled_progress=t->progress;t->pixel=0;t->phase=0;t->restore_offset=0;t->dirty=false;t->composing=true;
    for(unsigned n=0;n<2;n++){
        t->sampled_frames[n]=t->scenes[n]?t->scenes[n]->frames:0;
        if(t->scenes[n])t->scenes[n]->front_pinned=true;
    }
    if(t->effect==TRANSITION_PROGRAM&&t->programs[0]){
        Portable *p=t->programs[0]->vm.portable;p->progress=(float)(t->sampled_progress/100);
        for(unsigned n=0;n<2;n++)p->scene_frames[n]=t->scenes[n]?t->scenes[n]->front:NULL;
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
            if(p==0||p==1){Scene *s=t->scenes[p==1];if(s)src=s->front+(size_t)pixel*3;}
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
        const uint8_t *a=t->scenes[0]?t->scenes[0]->front+(size_t)pixel*3:NULL;
        const uint8_t *b=t->scenes[1]?t->scenes[1]->front+(size_t)pixel*3:NULL;
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
    if(end==t->target.count){unpin_scenes(t);t->composing=false;t->prepared=true;t->composite_frames++;}
}
static void slice(Transition *t,unsigned budget){
    uint64_t begun=platform_time_us();
    /* Reset all custom state to its admitted baseline, in bounded chunks. This
       makes seeking independent of previous percentages, including guest RNG. */
    while(t->phase<4){
        unsigned n=t->phase/2;TransitionProgram *program=t->programs[n];
        if((t->effect!=TRANSITION_CUSTOM&&t->effect!=TRANSITION_PROGRAM)||t->sampled_progress==0||t->sampled_progress==100||!program){t->phase=(n+1)*2;continue;}
        VmInstance *vm=&program->vm;Portable *p=vm->portable;
        if(!(t->phase%2)){
            size_t left=p->state_bytes-t->restore_offset;if(left>256)left=256;
            memcpy((uint8_t *)p->state+t->restore_offset,program->baseline+t->restore_offset,left);t->restore_offset+=(unsigned)left;
            if(t->restore_offset==p->state_bytes){t->restore_offset=0;t->phase++;p->random_state=program->random_state;p->status=1;p->bulk=0;p->native_pending=false;vm->control.program_us=0;vm->control.in_step=false;}
        }else{
            bool ready=false;const char *error=animation_poll_budget(vm,p->profile==4?0:(uint64_t)(t->sampled_progress*10000),&ready,budget);
            if(error){snprintf(t->error,sizeof(t->error),"transition program: %.70s",error);unpin_scenes(t);t->composing=false;return;}
            if(ready)t->phase++;
        }
        t->composite_us+=platform_time_us()-begun;return;
    }
    if(!t->pixel)for(unsigned n=0;n<2;n++)t->sampled_frames[n]=t->scenes[n]?t->scenes[n]->frames:0;
    compose(t);t->composite_us+=platform_time_us()-begun;
}
static void complete(Transition *t){
    if(!t->pending)return;
    int status=t->output->type->poll(t->output->driver);if(!status)return;t->pending=false;
    if(status<0){t->output->type->cancel(t->output->driver);strcpy(t->error,"native transition output failed; retrying");t->dirty=true;return;}
    memcpy(t->output->rgb,t->front,(size_t)t->target.count*3);t->frames++;t->output_us+=platform_time_us()-t->output_started;t->error[0]=0;
}
void transition_tick(Transition *t,unsigned budget){
    if(t->ticking)return;
    t->ticking=true;complete(t);transition_update(t,platform_time_us());
    if(t->command_active||!t->playing){t->ticking=false;return;}
    if(t->composing){
        for(unsigned n=0;n<2;n++)if(t->scenes[n])scene_tick(t->scenes[n],budget);
        slice(t,budget);t->ticking=false;return;
    }
    for(unsigned n=0;n<2;n++)if(t->scenes[n]){scene_tick(t->scenes[n],budget);if(t->scenes[n]->frames!=t->sampled_frames[n])t->dirty=true;}
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
    if(playing)for(unsigned n=0;n<2;n++)if(t->scenes[n])scene_play(t->scenes[n],t->scenes[n]->playing);
    if(playing)t->dirty=true;
}
const char *transition_frame(Transition *t,uint32_t delta_ms){
    t->command_active=true;transition_invalidate(t);const char *error=NULL;
    for(unsigned n=0;n<2&&!error;n++)if(t->scenes[n])error=scene_frame(t->scenes[n],delta_ms);
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
