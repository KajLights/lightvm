/* animation: one initialization and one whole-frame render callback. */
#include "animation.h"
#include "runtime.h"
#include "draw.h"
#include "platform.h"
#include <limits.h>
#include <string.h>

const char *animation_validate(Portable *p){
    unsigned inputs=p->profile==4?2:3;
    if((p->profile!=3&&p->profile!=4)||p->nregs<inputs||p->nfunctions!=2||!p->nresources)return "unsupported program profile";
    const unsigned types[3]={T_INT,T_FLOAT,T_FLOAT};
    for(unsigned r=0;r<p->nregs;r++){
        if(r<inputs){if(pbc_role(p,r)!=R_INPUT||pbc_type(p,r)!=types[r]||pbc_initial(p,r))return "invalid runtime argument binding";}
        else if(pbc_role(p,r)==R_INPUT)return "unexpected animation input or frame state";
    }
    const unsigned rights[2]={9,13};
    for(unsigned n=0;n<2;n++){
        if(p->entries[n]>=p->nfunctions||p->entries[0]==p->entries[1])return "invalid animation entry binding";
        PbcFunction fn=pbc_function(p,p->entries[n]);
        if(fn.rights!=rights[n]||fn.result||fn.arg_start||fn.arg_count!=inputs)return "invalid program entry signature";
    }
    PbcResourceSpec output=pbc_spec(p,0);
    if(output.kind!=DRAW_RGBA||output.flags!=1||pbc_expression(p,output.length).op!=1)return "invalid animation output binding";
    for(unsigned n=1;n<p->nresources;n++)if(pbc_spec(p,n).flags&1u)return "unexpected animation output resource";
    return NULL;
}
static const char *fault(VmInstance *a,const char *error){
    if(error){a->control.faulted=true;a->control.in_step=false;a->playing=false;}
    return error;
}
static bool invoke(VmInstance *a,unsigned entry,uint64_t time,uint64_t delta){
    PbcValue arguments[3]={{.i=(int32_t)a->control.count},{.f=(float)(time/1000000.0)},{.f=(float)(delta/1000000.0)}};
    if(a->portable->profile==4){arguments[1].f=a->portable->progress;time=0;}
    a->portable->now_us=time;
    return pbc_invoke(a->portable,a->portable->entries[entry],arguments,a->portable->profile==4?2:3);
}
static const char *finish(VmInstance *a){
    Control *c=&a->control;Portable *p=a->portable;
    if(p->render_count==UINT64_MAX||c->contribution_frame==UINT32_MAX)return fault(a,"render counter overflow");
    c->program_us=p->now_us;p->render_count++;
    memcpy(c->committed,c->working,control_frame_bytes(a));
    a->render_us=(uint32_t)(a->render_cpu_us>UINT32_MAX?UINT32_MAX:a->render_cpu_us);
    a->render_total_us+=a->render_cpu_us;
    c->contribution_valid=true;c->contribution_frame++;c->in_step=false;
    c->contribution=(ContributionStamp){.captured_us=c->capture_started_us,.completed_us=platform_time_us(),.dirty_since_us=c->captured_dirty_since_us,.generation=a->generation,.frame=c->contribution_frame,.dependency_revision=c->captured_revision};
    return NULL;
}
static const char *start(VmInstance *a,uint64_t delta,bool manual){
    Portable *p=a->portable;Control *c=&a->control;
    if(c->in_step)return "render is already in progress";
    if(delta>INT64_MAX||c->program_us>(uint64_t)INT64_MAX-delta)return "program clock overflow";
    if(!manual&&p->active_wall_us>UINT64_MAX-delta)return "wall clock overflow";
    memset(c->working,0,control_frame_bytes(a));
    control_observe(a,c->program_us+delta);c->captured_revision=c->dependency_revision;c->captured_dirty_since_us=c->dirty_since_us;
    c->capture_started_us=platform_time_us();
    a->render_cpu_us=0;
    /* A transition owner captures progress, fronts and live controls together.
       Its preparation uses defaults; subsequent evaluations retain that capture. */
    if(p->profile!=4||!c->initialized)inputs_capture(p,c->program_us+delta,p->profile==4);
    if(!invoke(a,1,c->program_us+delta,delta))return fault(a,"portable invocation rejected");
    if(!manual)p->active_wall_us+=delta;
    c->in_step=true;return NULL;
}
static const char *complete_render(VmInstance *a){
    int status;
    /* Resume already enforces the board's wall-time quantum. A tiny secondary
       instruction quota repeatedly ran whole service epochs during preparation.
       Finish the invocation before returning to its caller when it is done. */
    do{uint64_t started=platform_time_us();status=pbc_resume(a->portable,4096);a->render_cpu_us+=platform_time_us()-started;if(!status)runtime_checkpoint();}while(!status);
    return status<0?fault(a,pbc_fault_reason(a->portable)):finish(a);
}
int animation_prepare_step(AnimationPreparation *s,VmInstance *a){
    const char *error=NULL;int status;
    switch(s->stage){
    case 0:
        if(a->portable->profile==4&&!s->resources.stage)a->portable->input_epoch=platform_time_us();
        status=pbc_prepare_step(&s->resources,a->portable,&a->view);
        if(status<0){error=s->resources.error;break;}
        if(status>0)s->stage=1;
        return 0;
    case 1:
        if(!invoke(a,0,0,0)){error="portable invocation rejected";break;}
        s->stage=2;return 0;
    case 2:
        /* Let the interpreter time slice bound a heavy initializer; a tiny
           instruction quota adds avoidable foreground/transport round trips. */
        status=pbc_resume(a->portable,4096);
        if(status<0){error=pbc_fault_reason(a->portable);break;}
        if(status>0)s->stage=3;
        return 0;
    case 3:
        error=start(a,0,true);if(error)break;
        s->stage=4;return 0;
    case 4:{
        uint64_t began=platform_time_us();status=pbc_resume(a->portable,4096);
        a->render_cpu_us+=platform_time_us()-began;
        if(status<0){error=pbc_fault_reason(a->portable);break;}
        if(status>0){error=finish(a);if(error)break;s->stage=5;}
        return 0;
    }
    case 5:
        a->portable->render_reusable=pbc_render_reusable(a->portable);s->stage=6;return 1;
    case 6:return 1;
    default:error="invalid animation preparation stage";
    }
    s->error=fault(a,error);return -1;
}
const char *animation_prepare(VmInstance *a){
    AnimationPreparation s={0};int status;
    do{status=animation_prepare_step(&s,a);if(!status)runtime_checkpoint();}while(!status);
    return status<0?s.error:NULL;
}
const char *animation_preflight(VmInstance *a,uint64_t delta){
    if(delta>INT64_MAX||a->control.program_us>(uint64_t)INT64_MAX-delta)return "program clock overflow";
    return NULL;
}
const char *animation_advance(VmInstance *a,uint64_t delta,bool manual){
    if(!a->control.initialized||a->control.faulted)return "animation is not initialized or has faulted";
    const char *error=start(a,delta,manual);
    return error?error:complete_render(a);
}
/* A suspended render retains its arguments, context and RNG state. The next
 * slice continues that same call; elapsed time is supplied only at call start. */
const char *animation_poll_budget(VmInstance *a,uint64_t delta,bool *ready,unsigned budget){
    const char *error=animation_begin(a,delta);
    *ready=false;if(error)return error;
    AnimationSlice slice={a,budget,0};
    uint64_t started=platform_time_us();animation_execute(&slice);
    return animation_commit(&slice,platform_time_us()-started,ready);
}
const char *animation_begin(VmInstance *a,uint64_t delta){
    if(!a->control.initialized||a->control.faulted)return "animation is not initialized or has faulted";
    return a->control.in_step?NULL:start(a,delta,false);
}
void animation_execute(void *context){
    AnimationSlice *slice=context;
    slice->status=pbc_resume(slice->vm->portable,slice->budget);
}
const char *animation_commit(AnimationSlice *slice,uint64_t elapsed_us,bool *ready){
    VmInstance *a=slice->vm;int status=slice->status;
    *ready=false;
    a->render_cpu_us+=elapsed_us;
    if(status<0)return fault(a,pbc_fault_reason(a->portable));
    if(!status)return NULL;
    const char *error=finish(a);*ready=!error;return error;
}
const char *animation_poll(VmInstance *a,uint64_t delta,bool *ready){return animation_poll_budget(a,delta,ready,4096);}
const char *animation_quiesce(VmInstance *a){
    if(!a->control.in_step)return NULL;
    /* Render may mutate context: finish an already-started call before pause,
     * stop or reset; never discard half of a state update. */
    const void *previous=runtime_enter(a);
    const char *error=complete_render(a);
    runtime_leave(previous);return error;
}
const uint8_t *control_frame(VmInstance *a){return a->control.committed;}
const uint8_t *control_output_frame(VmInstance *a){
    const uint8_t *frame=control_frame(a);
    if(!a->control.presentation)return frame;
    /* Premultiplied RGB gives the animation over black. Keep this separate
       frame stable until the physical driver releases it. */
    for(unsigned i=0;i<a->control.count;i++)memcpy(a->control.presentation+(size_t)i*3,frame+(size_t)i*4,3);
    return a->control.presentation;
}
size_t control_frame_bytes(const VmInstance *a){return (size_t)a->control.count*4u;}
