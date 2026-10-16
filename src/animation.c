/* animation/1: one initialization and one whole-frame render callback. */
#include "animation.h"
#include "runtime.h"
#include "draw.h"
#include "platform.h"
#include <limits.h>
#include <string.h>

const char *animation_validate(Portable *p){
    if((p->profile!=1&&p->profile!=2)||p->nregs<3||p->nfunctions!=2||!p->nresources)return "unsupported animation profile";
    const unsigned types[3]={T_INT,T_FLOAT,T_FLOAT};
    for(unsigned r=0;r<p->nregs;r++){
        if(r<3){if(pbc_role(p,r)!=R_INPUT||pbc_type(p,r)!=types[r]||pbc_initial(p,r))return "invalid runtime argument binding";}
        else if(pbc_role(p,r)==R_INPUT||pbc_role(p,r)==R_FRAME)return "unexpected animation input or frame state";
    }
    const unsigned rights[2]={9,13};
    for(unsigned n=0;n<2;n++){
        if(p->entries[n]>=p->nfunctions||p->entries[0]==p->entries[1])return "invalid animation entry binding";
        PbcFunction fn=pbc_function(p,p->entries[n]);
        if(fn.rights!=rights[n]||fn.result||fn.arg_start||fn.arg_count!=3)return "invalid animation entry signature";
    }
    PbcResourceSpec output=pbc_spec(p,0);
    if(output.kind!=(p->profile==2?DRAW_RGBA:DRAW_RGB)||output.flags!=1||pbc_expression(p,output.length).op!=1)return "invalid animation output binding";
    for(unsigned n=1;n<p->nresources;n++)if(pbc_spec(p,n).flags&1u)return "unexpected animation output resource";
    return NULL;
}
static const char *fault(VmInstance *a,const char *error){
    if(error){a->control.faulted=true;a->control.in_step=false;a->playing=false;}
    return error;
}
static bool invoke(VmInstance *a,unsigned entry,uint64_t time,uint64_t delta){
    PbcValue arguments[3]={{.i=(int32_t)a->control.count},{.f=(float)(time/1000000.0)},{.f=(float)(delta/1000000.0)}};
    a->portable->now_us=time;
    return pbc_invoke(a->portable,a->portable->entries[entry],arguments,3);
}
static const char *finish(VmInstance *a){
    Control *c=&a->control;Portable *p=a->portable;
    if(p->render_count==UINT64_MAX)return fault(a,"render counter overflow");
    c->program_us=p->now_us;p->render_count++;
    memcpy(c->committed,c->working,control_frame_bytes(a));
    a->render_us=(uint32_t)(a->render_cpu_us>UINT32_MAX?UINT32_MAX:a->render_cpu_us);
    a->render_total_us+=a->render_cpu_us;
    c->contribution_valid=true;c->contribution_frame++;c->in_step=false;
    return NULL;
}
static const char *start(VmInstance *a,uint64_t delta,bool manual){
    Portable *p=a->portable;Control *c=&a->control;
    if(c->in_step)return "render is already in progress";
    if(delta>INT64_MAX||c->program_us>(uint64_t)INT64_MAX-delta)return "program clock overflow";
    if(!manual&&p->active_wall_us>UINT64_MAX-delta)return "wall clock overflow";
    memset(c->working,0,control_frame_bytes(a));
    a->render_cpu_us=0;
    if(!invoke(a,1,c->program_us+delta,delta))return fault(a,"portable invocation rejected");
    if(!manual)p->active_wall_us+=delta;
    c->in_step=true;return NULL;
}
static const char *complete_render(VmInstance *a){
    int status;
    do{uint64_t started=platform_time_us();status=pbc_resume(a->portable,64);a->render_cpu_us+=platform_time_us()-started;runtime_checkpoint();}while(!status);
    return status<0?fault(a,"portable execution fault"):finish(a);
}
const char *animation_prepare(VmInstance *a){
    const char *error=pbc_prepare(a->portable,&a->view);
    if(!error&&!invoke(a,0,0,0))error="portable invocation rejected";
    if(!error){
        int status;
        do{status=pbc_resume(a->portable,64);runtime_checkpoint();}while(!status);
        if(status<0)error="portable initialization fault";
    }
    if(!error)error=start(a,0,true);
    if(!error)error=complete_render(a);
    return fault(a,error);
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
    *ready=false;
    if(!a->control.initialized||a->control.faulted)return "animation is not initialized or has faulted";
    if(!a->control.in_step){const char *error=start(a,delta,false);if(error)return error;}
    uint64_t started=platform_time_us();
    int status=pbc_resume(a->portable,budget);
    a->render_cpu_us+=platform_time_us()-started;
    if(status<0)return fault(a,"portable execution fault");
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
const char *control_playback_step(VmInstance *a,uint64_t delta){
    return a->portable?animation_advance(a,delta,false):control_step(a,1,delta);
}
const uint8_t *control_frame(VmInstance *a){return a->control.committed;}
size_t control_frame_bytes(const VmInstance *a){return (size_t)a->control.count*(a->portable&&a->portable->profile==2?4u:3u);}
