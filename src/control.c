// Original LightVM control profile. MIT License, copyright 2026 Kaj Wik Siebert.
#include "control.h"
#include "audio_sources.h"
#include "vm.h"
#include "vm_allocator.h"
#include "runtime.h"
#include "animation.h"
#include "release.h"
#include <inttypes.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool setting_value(const Json *j,int token,unsigned type,PbcValue *out) {
    bool b; double f; int64_t i;
    if(type==T_BOOL){if(!json_bool(j,token,&b))return false;out->u=b;return true;}
    if(type==T_FLOAT){if(!json_f64(j,token,&f)||f < -FLT_MAX||f > FLT_MAX)return false;out->f=(float)f;return true;}
    if(!json_i64(j,token,&i)||i<INT32_MIN||i>INT32_MAX||(type==T_COLOR&&(i<0||i>0xffffff)))return false;
    out->i=(int32_t)i;return true;
}
void control_loaded(VmInstance *a) {
    Control *c=&a->control;memset(c,0,sizeof(*c));
    c->manifest_offset=a->portable->manifest_offset;
    c->manifest_size=a->portable->manifest_size;c->enabled=true;
}
void control_destroy(VmInstance *a) {
    vm_output_cancel(a);
    store_unpin(a->store,a->code_pin);a->code_pin=-1;a->store=NULL;
    pbc_destroy(a->portable);a->portable=NULL;
    demo_free(a->control.allocation);
    vm_heap_uncharge(a->control.output_charge);
    memset(&a->control,0,sizeof(a->control));
}
int control_prepare_step(AnimationPreparation *s,VmInstance *a) {
    Control *c=&a->control;
    if (c->faulted) {s->error="control program faulted; reactivate to recover";return -1;}
    if (c->initialized) return 1;
    if (!a->ready || !a->target || !a->target->count || a->target->count>PIXEL_CAPACITY) {s->error="assigned pixel contribution is unavailable";return -1;}
    if (!c->allocation) {
        c->count=a->target->count;
        size_t frame_bytes=control_frame_bytes(a),rgb_bytes=(size_t)c->count*3;
        bool convert=a->output!=NULL;
        c->buffer_bytes=frame_bytes*2+(convert?rgb_bytes:0);
        if (a->output && !vm_heap_charge(rgb_bytes)) {s->error="control output exceeds aggregate heap budget";return -1;}
        c->output_charge=a->output ? rgb_bytes : 0;
        c->allocation=demo_alloc(c->buffer_bytes);
        if (!c->allocation) { vm_heap_uncharge(c->output_charge); c->output_charge=0; s->error="memory allocation failed";return -1; }
        c->committed=c->allocation; c->working=c->committed+frame_bytes;
        c->presentation=convert?c->working+frame_bytes:NULL;
    }
    if(!s->buffers_ready){
        size_t n=c->buffer_bytes-s->cleared;if(n>256)n=256;
        memset(c->allocation+s->cleared,0,n);s->cleared+=n;
        if(s->cleared==c->buffer_bytes)s->buffers_ready=true;
        return 0;
    }
    a->view=(LbcBuffer){.data=c->working,.count=c->count,.generation=a->generation};
    int status=animation_prepare_step(s,a);if(status>0)c->initialized=true;return status;
}
static const char *control_step_impl(VmInstance *a,uint64_t delta) {
    if(a->output_pending)return "output frame is still in use";
    return animation_advance(a,delta,true);
}

const char *control_settings(VmInstance *a,const Json *j,int object){
    Portable *p=a->portable;
    if(object<0)return NULL;
    if(!p||j->tokens[object].type!=JSON_OBJECT)return "settings require animation and an object";
    for(int t=object+1;t<j->tokens[object].next;t=j->tokens[t+1].next){
        char name[33];unsigned n;
        if(!json_string(j,t,name,sizeof(name)))return "invalid setting name";
        strcpy(p->argument_error,name);
        for(n=0;n<p->nsettings;n++)if(!strcmp(name,p->setting_names[n]))break;
        if(n==p->nsettings)return "unknown setting";
        unsigned type=pbc_type(p,pbc_setting(p,n).reg);
        if(!setting_value(j,t+1,type,&p->overrides[n]))return "invalid setting value";
        p->override_mask|=1u<<n;
    }
    p->argument_error[0]=0;
    return NULL;
}
const char *control_reset_program(VmInstance *a,const Json *request,int settings){
    if(!a->portable)return "program_reset requires animation";
    if(a->generation>=UINT32_MAX-1)return "program generation exhausted";
    VmInstance *candidate=demo_calloc(1,sizeof(*candidate));
    if(!candidate)return "replacement exceeds peak memory budget";
    vm_init(candidate,a->target,a->send,a->transport);candidate->output=a->output;
    candidate->generation=a->generation+1;candidate->expected=a->expected;candidate->received=a->received;
    candidate->uploading=a->uploading; /* the shared install transaction survives a live reset */
    if(a->from_storage&&!vm_bind_storage(candidate,a->store,&a->code)){demo_free(candidate);return "package reference limit exceeded";}
    memcpy(candidate->slot,a->slot,sizeof(a->slot));
    memcpy(candidate->output_id,a->output_id,sizeof(a->output_id));
    memcpy(candidate->layer,a->layer,sizeof(a->layer));
    if(!a->from_storage)candidate->memory=a->memory;
    const char *error=vm_load(candidate);
    if(!error){
        Portable *old=a->portable,*next=candidate->portable;
        next->input_registry=old->input_registry;
        if(old->ninputs){memcpy(next->inputs,old->inputs,old->ninputs*sizeof(*old->inputs));
            for(unsigned n=0;n<next->ninputs;n++){next->inputs[n].driver.started=0;next->inputs[n].driver.phase=0;next->inputs[n].has_last=false;}
        }
        for(unsigned n=0;n<old->nsettings;n++){
            if(old->initialized){next->overrides[n]=pbc_value(old,pbc_setting(old,n).reg);next->override_mask|=1u<<n;}
            else if(old->override_mask&(1u<<n)){next->overrides[n]=old->overrides[n];next->override_mask|=1u<<n;}
        }
        error=control_settings(candidate,request,settings);
        if(!error)error=inputs_bind(next,request,request?json_get(request,0,"inputs"):-1,0);

    }
    if(!error){candidate->ready=true;error=control_prepare(candidate);}
    bool was_playing=a->playing,output_attempted=false;
    if(!error&&candidate->output){
        a->playing=false;output_attempted=true;
        vm_output_cancel(a);
        PixelBuffer *output=candidate->output;const uint8_t *frame=control_output_frame(candidate);
        bool shown=output->driver&&output->type&&output->rgb&&output->count==candidate->control.count&&pixels_show_frame(output,frame,output->count);
        control_presented(candidate,shown);
        if(shown){memcpy(output->rgb,frame,(size_t)output->count*3);candidate->control.output_valid=true;candidate->control.output_frame++;
            control_contribution_presented(candidate,candidate->control.contribution,platform_time_us());}
    }
    if(error){
        if(output_attempted&&a->output&&a->control.output_valid)
            (void)pixels_show_frame(a->output,a->output->rgb,a->output->count);
        a->playing=was_playing;control_destroy(candidate);demo_free(candidate);return error;
    }
    bool playing=was_playing;
    control_destroy(a);*a=*candidate;demo_free(candidate);
    vm_rebind(a);a->playing=playing;
    a->last_frame=platform_time_us();a->next_frame=a->last_frame;
    return NULL;
}

// Management is serviced by the same serialized engine owner as guest steps.
static void emit(VmInstance *a,uint16_t id,uint8_t type,const void *bytes,size_t size) {
    const uint8_t *p=bytes;
    while (size) { size_t n=size>FRAME_LIMIT ? FRAME_LIMIT : size; a->send(a->transport,type,id,p,n); p+=n; size-=n; }
}
typedef struct { VmInstance *vm; uint16_t id; uint8_t bytes[FRAME_LIMIT]; size_t used; } ControlWriter;
static void flush(ControlWriter *w) {
    if (w->used) emit(w->vm,w->id,RESP_DATA,w->bytes,w->used);
    w->used=0;
}
static void bytes(ControlWriter *w,const void *data,size_t size) {
    const uint8_t *p=data;
    while (size) {
        size_t n=FRAME_LIMIT-w->used; if (n>size) n=size;
        memcpy(w->bytes+w->used,p,n); w->used+=n; p+=n; size-=n;
        if (w->used==FRAME_LIMIT) flush(w);
    }
}
static void text(ControlWriter *w,const char *s) { bytes(w,s,strlen(s)); }
static void input_text(void *w,const char *s){text(w,s);}
static void result(VmInstance *a,uint16_t id,enum Status code,const char *message) {
    send_status(a->send,a->transport,id,code,0,message);
}
static void describe(ControlWriter *w,bool with_manifest) {
    VmInstance *a=w->vm;
    Control *c=&a->control; char b[512];
    snprintf(b,sizeof(b),"{\"output\":\"%s\",\"slot\":\"%s\",\"generation\":%lu,\"program_us\":%" PRIu64 ",\"paused\":%s,\"faulted\":%s,\"contribution_valid\":%s,\"output_valid\":%s,\"contribution_frame\":%lu,\"output_frame\":%lu,\"buffer_bytes\":%lu",
        a->output_id,a->slot,(unsigned long)a->generation,c->program_us,a->playing ? "false" : "true",c->faulted ? "true" : "false",c->contribution_valid ? "true" : "false",c->output_valid ? "true" : "false",(unsigned long)c->contribution_frame,(unsigned long)c->output_frame,(unsigned long)(c->buffer_bytes+c->output_charge));
    text(w,b);
    if(a->layer[0]){snprintf(b,sizeof(b),",\"layer\":\"%s\"",a->layer);text(w,b);}
    if(a->portable&&a->portable->profile==4)text(w,",\"target\":\"transition\"");
    if(a->portable){
        Portable *p=a->portable;
        snprintf(b,sizeof(b),",\"program_crc32\":%" PRIu32,a->code.crc);text(w,b);
        snprintf(b,sizeof(b),",\"pixel_format\":\"%s\",\"render_total_us\":%" PRIu64 ",\"native_draw_us\":%" PRIu64 ",\"dispatch_count\":%" PRIu64 ",\"slice_count\":%" PRIu64,"rgba8-premul",a->render_total_us,p->native_draw_us,p->dispatch_count,p->slice_count);text(w,b);
        snprintf(b,sizeof(b),",\"render_count\":%" PRIu64 ",\"active_wall_us\":%" PRIu64,p->render_count,p->active_wall_us);text(w,b);
        snprintf(b,sizeof(b),",\"render_reusable\":%s,\"displayed_frame\":%lu,\"distinct_presentations\":%" PRIu64,p->render_reusable?"true":"false",(unsigned long)c->displayed.frame,c->distinct_presentations);text(w,b);
        text(w,",\"displayed_sample_age_us\":");
        if(c->displayed.frame){snprintf(b,sizeof(b),"%" PRIu64,platform_time_us()-c->displayed.captured_us);text(w,b);}else text(w,"null");
        text(w,",\"completion_to_display_us\":");
        if(c->displayed.frame){snprintf(b,sizeof(b),"%" PRIu64,c->presented_us-c->displayed.completed_us);text(w,b);}else text(w,"null");
        snprintf(b,sizeof(b),",\"dependency_revision\":%" PRIu32 ",\"dirty_to_display_us\":%" PRIu64 ",\"runnable_wait_us\":%" PRIu64 ",\"composite_wait_us\":%" PRIu64 ",\"output_wait_us\":%" PRIu64,c->dependency_revision,c->dirty_to_display_us,c->wait_us[1],c->wait_us[2],c->wait_us[3]);text(w,b);
        text(w,",\"dirty_age_us\":");if(c->dirty){snprintf(b,sizeof(b),"%" PRIu64,platform_time_us()-c->dirty_since_us);text(w,b);}else text(w,"null");
        inputs_describe(p,input_text,w);
        text(w,",\"sources\":");input_scope_describe(p->source_scope,input_text,w);
        snprintf(b,sizeof(b),",\"map_plan_bytes\":%zu",p->map_bytes);text(w,b);
        snprintf(b,sizeof(b),",\"instance_context_bytes\":%zu,\"portable_context_bytes\":%zu,\"metadata_bytes\":%zu,\"state_bytes\":%zu,\"resource_payload_bytes\":%zu,\"register_bytes\":%zu,\"call_storage_bytes\":0,\"heap_used\":%zu,\"heap_peak\":%zu",
            sizeof(*a),sizeof(*p),p->metadata_bytes,p->state_bytes,p->payload_bytes,p->register_bytes,vm_heap_used(),vm_heap_peak());text(w,b);
        if(p->initialized){
            text(w,",\"settings_values\":{");
            for(unsigned n=0;n<p->nsettings;n++){
                unsigned reg=pbc_setting(p,n).reg,type=pbc_type(p,reg);PbcValue v=pbc_value(p,reg);
                snprintf(b,sizeof(b),"%s\"%s\":",n?",":"",p->setting_names[n]);text(w,b);
                if(type==T_FLOAT)snprintf(b,sizeof(b),"%.9g",(double)v.f);
                else if(type==T_BOOL)snprintf(b,sizeof(b),"%s",v.u?"true":"false");
                else snprintf(b,sizeof(b),"%" PRId32,v.i);
                text(w,b);
            }text(w,"}");
        }
    }
    if (with_manifest) {
        text(w,",\"manifest\":");
        uint8_t chunk[256];
        for(uint32_t offset=0;offset<c->manifest_size;) {
            size_t n=c->manifest_size-offset;if(n>sizeof(chunk))n=sizeof(chunk);
            if(!vm_read(a,c->manifest_offset+offset,chunk,n))return;
            bytes(w,chunk,n);offset+=(uint32_t)n;
        }

    }
    text(w,"}");
}
bool control_manage(VmInstance *a,uint16_t id,const Json *j,const char *op) {
    bool get=!strcmp(op,"program_state"),pause=!strcmp(op,"program_pause"),resume=!strcmp(op,"program_resume"),snapshot=!strcmp(op,"program_snapshot"),reset=!strcmp(op,"program_reset"),bind=!strcmp(op,"program_inputs");
    if (!get && !pause && !resume && !snapshot && !reset && !bind) return false;
    if (!a->ready || !a->control.enabled) { result(a,id,STATUS_EXECUTION,"no control program loaded"); return true; }
    static const char *const read_fields[]={"op","slot","output","layer","target",NULL};
    static const char *const lifecycle_fields[]={"op","slot","output","layer","expected_generation",NULL};
    static const char *const reset_fields[]={"op","slot","output","layer","expected_generation","settings","inputs",NULL};
    static const char *const input_fields[]={"op","slot","output","layer","target","expected_generation","expected_inputs_revision","inputs",NULL};
    if (!json_fields(j,0,(get || snapshot) ? read_fields : reset ? reset_fields : bind ? input_fields : lifecycle_fields)) { result(a,id,STATUS_PROTOCOL,"invalid control management fields"); return true; }
    uint32_t generation;
    if ((pause || resume || reset || bind) && (!json_u32(j,json_get(j,0,"expected_generation"),&generation) || generation!=a->generation)) {
        result(a,id,STATUS_CONFIG,"stale program generation"); return true;
    }
    Control *c=&a->control;
    if(bind){
        uint32_t revision;int values=json_get(j,0,"inputs");
        if(values<0||!json_u32(j,json_get(j,0,"expected_inputs_revision"),&revision)||revision!=a->portable->input_revision){result(a,id,STATUS_CONFIG,"stale input revision or missing inputs");return true;}
        uint64_t now=platform_time_us();
        uint64_t time=a->portable->profile==4?(now>=a->portable->input_epoch?now-a->portable->input_epoch:0):c->program_us;
        const char *error=inputs_bind(a->portable,j,values,time);
        if(error){result(a,id,STATUS_CONFIG,error);return true;}
    }
    if(reset){
        const char *error=control_reset_program(a,j,json_get(j,0,"settings"));
        if(error){result(a,id,STATUS_CONFIG,error);return true;}
    }
    if (pause) {
        vm_output_cancel(a);
        const char *error=a->portable?animation_quiesce(a):NULL;
        a->playing=false;
        if(error){result(a,id,STATUS_EXECUTION,error);return true;}
    }
    if (resume) {
        if (!c->initialized || c->faulted) { result(a,id,STATUS_EXECUTION,"initialize with PLAY/FRAME; reactivate a faulted program"); return true; }
        a->playing=true; a->last_frame=platform_time_us(); a->next_frame=a->last_frame;
        /* STOP may have cleared the physical output while retaining these
           pixels. A reusable contribution must be presented again on resume. */
        if(a->output&&a->portable->render_reusable&&c->contribution_valid&&c->contribution.frame&&!inputs_changed(a->portable,c->program_us))a->frame_ready=true;
    }
    ControlWriter writer={.vm=a,.id=id}; ControlWriter *w=&writer;
    if (snapshot) {
        if (a->output)emit(a,id,RESP_PIXELS,a->output->rgb,(size_t)a->output->count*3);
        else if(c->contribution_valid)emit(a,id,RESP_PIXELS,c->committed,control_frame_bytes(a));
        text(w,"{\"state\":"); describe(w,false); text(w,"}");
    } else describe(w,true);
    flush(w);
    result(a,id,STATUS_OK,"control management completed"); return true;
}

const char *control_prepare(VmInstance *a) {
    const void *previous=runtime_enter(a);
    AnimationPreparation s={0};int status;
    do{status=control_prepare_step(&s,a);if(!status)runtime_checkpoint();}while(!status);
    const char *error=status<0?s.error:NULL;
    runtime_leave(previous);return error;
}

const char *control_step(VmInstance *a,uint64_t delta) {
    const void *previous=runtime_enter(a);
    const char *error=control_step_impl(a,delta);
    runtime_leave(previous);return error;
}
