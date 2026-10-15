/* animation/1 scheduling and publication policy, outside the portable VM. */
#include "animation.h"
#include "vm_allocator.h"
#include <limits.h>
#include <string.h>
typedef struct {
    uint64_t started,target_time,ticks_left;
    uint32_t phase,pixel,work;
} AnimationContinuation;
const char *animation_validate(Portable *p){
    if(p->profile!=1||p->nregs<4||p->nfunctions<4||!p->nresources||(p->mode!=1&&p->mode!=2)||!p->max_catch_up||p->max_catch_up>64||p->first>1)return "unsupported animation profile";
    if(p->period_expression>=p->nexpressions||p->expressions[p->period_expression].type!=T_INT)return "invalid animation period expression";
    for(unsigned r=0;r<p->nregs;r++){
        if(r<4){if(pbc_role(p,r)!=R_INPUT||pbc_type(p,r)!=(r<2?T_INT:T_FLOAT)||pbc_initial(p,r))return "invalid animation input binding";}
        else if(pbc_role(p,r)==R_INPUT)return "unexpected animation input";
    }
    const unsigned rights[4]={25,2,p->mode==2?4:0,9};
    for(unsigned n=0;n<4;n++){
        if(p->entries[n]>=p->nfunctions)return "invalid animation entry binding";
        for(unsigned k=0;k<n;k++)if(p->entries[k]==p->entries[n])return "aliased animation entries";
        PbcFunction *fn=&p->functions[p->entries[n]];
        if(fn->rights!=rights[n]||fn->result!=(n==2&&p->mode==1?T_COLOR:0)||fn->arg_count)return "invalid animation entry permissions/signature";
    }
    PbcResourceSpec *output=&p->specs[0];
    if(output->kind!=3||output->flags!=1||p->expressions[output->length].op!=1)return "invalid animation output binding";
    for(unsigned n=1;n<p->nresources;n++)if(p->specs[n].flags)return "unexpected animation output resource";
    return NULL;
}
static const char *invoke(VmInstance *a,unsigned function,float delta,unsigned index,uint64_t started,uint32_t *work){
    Portable *p=a->portable;
    p->now_us=a->control.program_us;
    p->values[0].i=(int32_t)a->control.count;
    p->values[1].u=index;p->values[2].f=(float)(p->now_us/1000000.0);p->values[3].f=delta;
    if(function==p->entries[1])for(unsigned r=0;r<p->nregs;r++)if(pbc_role(p,r)==R_FRAME){p->values[r].stamp=0;p->values[r].u=pbc_initial(p,r);}
    if(!pbc_invoke(p,function,NULL,0))return "portable invocation rejected";
    int status;
    do{
        if(platform_time_us()-started>=RUN_TIMEOUT_US)return "execution deadline exceeded";
        if(p->used>=2000000-*work)return "animation aggregate instruction limit exceeded";
        unsigned remaining=2000000-*work-p->used;
        status=pbc_resume(p,remaining<64?remaining:64);
    }while(!status);
    if(status<0)return p->used>=2000000?"bytecode instruction limit exceeded":"portable execution fault";
    if(p->used>2000000-*work)return "animation aggregate instruction limit exceeded";
    *work+=p->used;return NULL;
}
static const char *parameters(VmInstance *a){
    for(unsigned n=0;n<a->control.parameter_count;n++){
        ControlParameter *parameter=&a->control.parameters[n];LbcValue value;
        if(parameter->kind==PARAM_FLOAT)value.f=(float)parameter->value.real;else value.i=(int32_t)parameter->value.integer;
        if(!pbc_parameter(a->portable,a->control.parameter_registers[n],value.u))return "invalid parameter binding";
    }return NULL;
}
static const char *tick(VmInstance *a,float delta,uint64_t started,uint32_t *work){
    Portable *p=a->portable;const char *error=invoke(a,p->entries[0],delta,0,started,work);
    if(error)return error;
    if(p->ticks==UINT64_MAX||UINT64_MAX-p->cycles<p->pending_cycles)return "animation counter overflow";
    p->ticks++;
    if(!p->completion_pending){p->cycles+=p->pending_cycles;p->pending_cycles=0;}
    return NULL;
}
static const char *render(VmInstance *a,uint64_t started,uint32_t *work){
    Portable *p=a->portable;Control *c=&a->control;
    memset(c->working,0,(size_t)c->count*3);
    const char *error=invoke(a,p->entries[1],0,0,started,work);
    if(error)return error;
    unsigned iterations=p->mode==1?c->count:1;
    for(unsigned n=0;n<iterations;n++){
        error=invoke(a,p->entries[2],0,n,started,work);if(error)return error;
        if(p->mode==1){uint32_t color=p->result.u;uint8_t *at=c->working+3*n;at[0]=(uint8_t)(color>>16);at[1]=(uint8_t)(color>>8);at[2]=(uint8_t)color;}
    }
    /* A completing frame remains a candidate until native presentation succeeds. */
    if(!p->completion_pending){memcpy(c->committed,c->working,(size_t)c->count*3);c->contribution_valid=true;c->contribution_frame++;}
    return NULL;
}
static const char *fault(VmInstance *a,const char *error){
    if(error){a->control.faulted=true;a->playing=false;a->portable->pending_cycles=0;a->portable->completion_pending=false;}
    return error;
}
const char *animation_prepare(VmInstance *a){
    Portable *p=a->portable;uint32_t work=0;uint64_t started=platform_time_us();
    a->control.continuation_bytes=sizeof(AnimationContinuation);
    a->control.continuation=demo_calloc(1,sizeof(AnimationContinuation));
    if(!a->control.continuation)return "animation continuation exceeds memory budget";
    const char *error=pbc_period(p,a->control.count,&p->period_us);
    if(!error)error=pbc_prepare(p,&a->view);
    if(!error)error=parameters(a);
    if(!error)error=invoke(a,p->entries[3],0,0,started,&work);
    if(!error&&p->period_us&&p->first)error=tick(a,0,started,&work);
    if(!error)error=render(a,started,&work);
    return fault(a,error);
}
const char *animation_preflight(VmInstance *a,uint64_t delta){
    Portable *p=a->portable;uint64_t period;
    if(p->completed)return NULL;
    const char *error=pbc_period(p,a->target?a->target->count:0,&period);
    if(error)return error;
    if(delta>INT64_MAX||a->control.program_us>(uint64_t)INT64_MAX-delta||p->credit_us>UINT64_MAX-delta)return "program clock overflow";
    if(period&&(p->credit_us+delta)/period>64)return "manual advance exceeds max_manual_ticks (64)";
    return NULL;
}
const char *animation_advance(VmInstance *a,uint64_t delta,bool manual){
    Portable *p=a->portable;Control *c=&a->control;
    if(!c->initialized||c->faulted)return "animation is not initialized or has faulted";
    if(p->completed)return NULL;
    if(p->completion_pending){uint32_t work=0;return fault(a,render(a,platform_time_us(),&work));}
    if(delta>INT64_MAX||c->program_us>(uint64_t)INT64_MAX-delta||p->credit_us>UINT64_MAX-delta)return "program clock overflow";
    uint64_t credit=p->credit_us+delta,due=p->period_us?credit/p->period_us:0;
    if(manual&&due>64)return "manual advance exceeds max_manual_ticks (64)";
    uint64_t run=due;
    if(p->period_us&&due>((uint64_t)INT64_MAX-c->program_us)/p->period_us)return "program clock overflow";
    if(!manual){
        if(UINT64_MAX-p->active_wall_us<delta)return "wall clock overflow";
        p->active_wall_us+=delta;
        if(run>p->max_catch_up){
            uint64_t dropped=run-p->max_catch_up;
            if(UINT64_MAX-p->dropped_ticks<dropped)return "overload counter overflow";
            p->dropped_ticks+=dropped;run=p->max_catch_up;
        }
    }
    p->credit_us=p->period_us?credit%p->period_us:0;
    uint32_t work=0;uint64_t started=platform_time_us();const char *error=parameters(a);
    for(uint64_t n=0;!error&&n<run;n++){
        c->program_us+=p->period_us;
        error=tick(a,(float)(p->period_us/1000000.0),started,&work);
        if(error)c->program_us-=p->period_us;
        if(p->completion_pending){p->credit_us=0;break;}
    }
    if(!p->period_us)c->program_us+=delta;
    if(!error)error=render(a,started,&work);
    return fault(a,error);
}

/* Autonomous work returns to the native event loop after at most 4096 VM
 * dispatches. Each bulk dispatch handles at most 32 elements. The same snapshot
 * and continuation survive service/management calls between these polls. */
static bool begin_async(VmInstance *a,unsigned phase){
    Portable *p=a->portable;AnimationContinuation *run=a->control.continuation;
    run->phase=phase;
    p->now_us=phase==1?run->target_time:a->control.program_us;
    p->values[1].u=run->pixel;p->values[2].f=(float)(p->now_us/1000000.0);
    p->values[3].f=phase==1?(float)(p->period_us/1000000.0):0;
    if(phase==2){
        memset(a->control.working,0,(size_t)a->control.count*3);
        for(unsigned r=0;r<p->nregs;r++)if(pbc_role(p,r)==R_FRAME){p->values[r].stamp=0;p->values[r].u=pbc_initial(p,r);}
    }
    return pbc_invoke(p,p->entries[phase-1],NULL,0);
}
static const char *end_async_tick(VmInstance *a){
    Portable *p=a->portable;AnimationContinuation *run=a->control.continuation;
    if(p->ticks==UINT64_MAX||UINT64_MAX-p->cycles<p->pending_cycles)return "animation counter overflow";
    a->control.program_us=run->target_time;p->ticks++;run->ticks_left--;
    if(!p->completion_pending){p->cycles+=p->pending_cycles;p->pending_cycles=0;}
    else p->credit_us=0;
    return NULL;
}
static const char *async_fault(VmInstance *a,const char *error){
    AnimationContinuation *run=a->control.continuation;run->phase=0;a->control.in_step=false;
    return fault(a,error);
}
const char *animation_poll(VmInstance *a,uint64_t delta,bool *ready){
    Portable *p=a->portable;Control *c=&a->control;AnimationContinuation *run=c->continuation;
    *ready=false;
    if(!c->initialized||c->faulted||!run)return "animation is not initialized or has faulted";
    if(p->completed)return NULL;
    if(!run->phase){
        if(delta>INT64_MAX||c->program_us>(uint64_t)INT64_MAX-delta||p->credit_us>UINT64_MAX-delta||p->active_wall_us>UINT64_MAX-delta)return "program clock overflow";
        memset(run,0,sizeof(*run));run->started=platform_time_us();
        uint64_t credit=p->credit_us+delta;
        run->ticks_left=p->period_us&&!p->completion_pending?credit/p->period_us:0;
        if(p->period_us&&run->ticks_left>((uint64_t)INT64_MAX-c->program_us)/p->period_us)return "program clock overflow";
        p->credit_us=p->period_us&&!p->completion_pending?credit%p->period_us:0;p->active_wall_us+=delta;
        if(run->ticks_left>p->max_catch_up){
            uint64_t dropped=run->ticks_left-p->max_catch_up;
            if(UINT64_MAX-p->dropped_ticks<dropped)return "overload counter overflow";
            p->dropped_ticks+=dropped;run->ticks_left=p->max_catch_up;
        }
        const char *error=parameters(a);if(error)return fault(a,error);
        if(!p->period_us)c->program_us+=delta;
        run->target_time=c->program_us+p->period_us;c->in_step=true;
        if(!begin_async(a,run->ticks_left?1:2))return async_fault(a,"portable invocation rejected");
    }
    unsigned remaining=4096;
    while(remaining){
        if(platform_time_us()-run->started>=RUN_TIMEOUT_US)return async_fault(a,"execution deadline exceeded");
        if(p->used>=2000000-run->work)return async_fault(a,"animation aggregate instruction limit exceeded");
        unsigned allowed=2000000-run->work-p->used;
        unsigned quantum=remaining<64?remaining:64;if(quantum>allowed)quantum=allowed;
        unsigned before=p->used;int status=pbc_resume(p,quantum);
        unsigned used=p->used-before;remaining-=used;
        if(status<0)return async_fault(a,p->used>=2000000?"bytecode instruction limit exceeded":"portable execution fault");
        if(!status)continue;
        if(p->used>2000000-run->work)return async_fault(a,"animation aggregate instruction limit exceeded");
        run->work+=p->used;
        if(run->phase==1){
            const char *error=end_async_tick(a);if(error)return async_fault(a,error);
            unsigned next=run->ticks_left&&!p->completion_pending?1:2;
            run->target_time=c->program_us+p->period_us;
            if(!begin_async(a,next))return async_fault(a,"portable invocation rejected");
        }else if(run->phase==2){
            run->pixel=0;if(!begin_async(a,3))return async_fault(a,"portable invocation rejected");
        }else{
            if(p->mode==1){
                uint32_t color=p->result.u;uint8_t *at=c->working+3*run->pixel;
                at[0]=(uint8_t)(color>>16);at[1]=(uint8_t)(color>>8);at[2]=(uint8_t)color;
                if(++run->pixel<c->count){if(!begin_async(a,3))return async_fault(a,"portable invocation rejected");continue;}
            }
            if(!p->completion_pending){memcpy(c->committed,c->working,(size_t)c->count*3);c->contribution_valid=true;c->contribution_frame++;}
            run->phase=0;c->in_step=false;*ready=true;return NULL;
        }
    }
    return NULL;
}
const char *animation_quiesce(VmInstance *a){
    AnimationContinuation *run=a->control.continuation;Portable *p=a->portable;
    if(!run||!run->phase)return NULL;
    if(run->phase==1&&p->used){
        int status;
        do{
            if(platform_time_us()-run->started>=RUN_TIMEOUT_US)return async_fault(a,"execution deadline exceeded");
            if(p->used>=2000000-run->work)return async_fault(a,"animation aggregate instruction limit exceeded");
            unsigned remaining=2000000-run->work-p->used;
            status=pbc_resume(p,remaining<64?remaining:64);
        }while(!status);
        if(status<0)return async_fault(a,"portable execution fault");
        if(p->used>2000000-run->work)return async_fault(a,"animation aggregate instruction limit exceeded");
        const char *error=end_async_tick(a);if(error)return async_fault(a,error);
    }
    if(!p->completion_pending){
        if(UINT64_MAX-p->cancelled_ticks<run->ticks_left)return async_fault(a,"cancellation counter overflow");
        p->cancelled_ticks+=run->ticks_left;
    }
    /* A render has no simulation effects, so cancellation needs no rollback. */
    p->status=1;p->bulk=0;run->phase=0;a->control.in_step=false;return NULL;
}
const char *control_playback_step(VmInstance *a,uint64_t delta){
    return a->portable?animation_advance(a,delta,false):control_step(a,1,delta);
}
const uint8_t *control_frame(VmInstance *a){
    return a->portable&&a->portable->completion_pending?a->control.working:a->control.committed;
}
void control_presented(VmInstance *a,bool success){
    Portable *p=a->portable;if(!p)return;
    if(!success){fault(a,"native pixel output failed");return;}
    if(p->completion_pending){
        memcpy(a->control.committed,a->control.working,(size_t)a->control.count*3);
        a->control.contribution_valid=true;a->control.contribution_frame++;
        p->cycles+=p->pending_cycles;p->pending_cycles=0;p->completion_pending=false;
        p->completion_time_us=a->control.program_us;p->completed=true;
        /* Only the animation is complete. The component remains available. */
        a->playing=false;
    }
}
