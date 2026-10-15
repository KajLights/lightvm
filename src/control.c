// Original LightVM control profile. MIT License, copyright 2026 Kaj Wik Siebert.
#include "control.h"
#include "vm.h"
#include "vm_allocator.h"
#include <inttypes.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool value(const Json *j,int token,ParameterKind kind,ParameterValue *out) {
    bool b;
    if (kind==PARAM_BOOL) { if (!json_bool(j,token,&b)) return false; out->integer=b; return true; }
    if (kind==PARAM_FLOAT) return json_f64(j,token,&out->real) && out->real>=-FLT_MAX && out->real<=FLT_MAX;
    if (!json_i64(j,token,&out->integer)) return false;
    return kind==PARAM_RGB ? (out->integer>=0 && out->integer<=0xffffff) : (out->integer>=INT32_MIN && out->integer<=INT32_MAX);
}
static bool in_bounds(const ControlParameter *p,ParameterValue v) {
    if (p->kind==PARAM_FLOAT)
        return (!p->has_minimum || v.real>=p->minimum.real) && (!p->has_maximum || v.real<=p->maximum.real);
    return (!p->has_minimum || v.integer>=p->minimum.integer) && (!p->has_maximum || v.integer<=p->maximum.integer);
}
static bool manifest(const uint8_t *bytes,size_t size,Control *c) {
    Json j; uint32_t abi;
    static const char *const fields[]={"schema","abi","pixels","parameters",NULL};
    static const char *const pixel_fields[]={"format",NULL};
    static const char *const param_fields[]={"name","type","default","minimum","maximum",NULL};
    if (size>CONTROL_MANIFEST_LIMIT || !json_parse(&j,bytes,size) || !json_fields(&j,0,fields) ||
        !json_equal(&j,json_get(&j,0,"schema"),"lightvm.program/3") ||
        !json_u32(&j,json_get(&j,0,"abi"),&abi) || abi!=CONTROL_ABI_VERSION) return false;
    int pixels=json_get(&j,0,"pixels"),params=json_get(&j,0,"parameters");
    if (!json_fields(&j,pixels,pixel_fields) || !json_equal(&j,json_get(&j,pixels,"format"),"rgb8")) return false;
    if (params<0 || j.tokens[params].type!=JSON_ARRAY || j.tokens[params].children>(int)CONTROL_PARAMETER_LIMIT) return false;
    for (int t=params+1;t<j.tokens[params].next;t=j.tokens[t].next) {
        ControlParameter *p=&c->parameters[c->parameter_count++];
        if (!json_fields(&j,t,param_fields) || !json_string(&j,json_get(&j,t,"name"),p->name,sizeof(p->name)) || !object_name_valid(p->name)) return false;
        for (uint32_t i=0;i+1<c->parameter_count;++i) if (!strcmp(c->parameters[i].name,p->name)) return false;
        int type=json_get(&j,t,"type");
        if (json_equal(&j,type,"bool")) p->kind=PARAM_BOOL;
        else if (json_equal(&j,type,"int")) p->kind=PARAM_INT;
        else if (json_equal(&j,type,"float")) p->kind=PARAM_FLOAT;
        else if (json_equal(&j,type,"rgb8")) p->kind=PARAM_RGB;
        else return false;
        if (!value(&j,json_get(&j,t,"default"),p->kind,&p->value)) return false;
        int lo=json_get(&j,t,"minimum"),hi=json_get(&j,t,"maximum");
        p->has_minimum=lo>=0; p->has_maximum=hi>=0;
        if ((p->has_minimum || p->has_maximum) && (p->kind==PARAM_RGB || p->kind==PARAM_BOOL)) return false;
        if ((p->has_minimum && !value(&j,lo,p->kind,&p->minimum)) ||
            (p->has_maximum && !value(&j,hi,p->kind,&p->maximum)) || !in_bounds(p,p->value)) return false;
    }
    c->enabled=true; return true;
}
const char *control_probe(VmInstance *a) {
    Control *c=&a->control;memset(c,0,sizeof(*c));
    LbcProgram *p=&a->program;
    if(!p->manifest_size || p->manifest_size>CONTROL_MANIFEST_LIMIT)return "bytecode requires a LightVM manifest";
    uint8_t *json=demo_calloc(1,p->manifest_size);
    if(!json)return "memory allocation failed";
    bool ok=vm_read(a,p->manifest_offset,json,p->manifest_size) && manifest(json,p->manifest_size,c);
    demo_free(json);
    if(!ok)return "invalid LightVM manifest";
    c->manifest_offset=p->manifest_offset;c->manifest_size=p->manifest_size;
    uint32_t n=0;
    for(unsigned r=0;r<p->nregs;r++)if(p->registers[r*8+1]==R_PARAM) {
        if(n>=c->parameter_count)return "parameter manifest does not match bytecode";
        ControlParameter *param=&c->parameters[n];
        unsigned type=p->registers[r*8];
        unsigned expected=param->kind==PARAM_BOOL?T_BOOL:param->kind==PARAM_FLOAT?T_FLOAT:param->kind==PARAM_RGB?T_COLOR:T_INT;
        LbcValue v={.u=lbc_u32(p->registers+r*8+4)};
        if(type!=expected || (type==T_FLOAT ? v.f!=(float)param->value.real : v.i!=(int32_t)param->value.integer))return "parameter type/default mismatch";
        c->parameter_registers[n++]=(uint8_t)r;
    }
    return n==c->parameter_count ? NULL : "parameter manifest does not match bytecode";
}
void control_destroy(VmInstance *a) {
    demo_free(a->control.allocation);
    vm_heap_uncharge(a->control.output_charge);
    memset(&a->control,0,sizeof(a->control));
}
const char * control_prepare(VmInstance *a) {
    Control *c=&a->control;
    if (c->faulted) return "control program faulted; reactivate to recover";
    if (c->initialized) return NULL;
    if (!a->ready || !a->target || !a->target->count || a->target->count>PIXEL_CAPACITY) return "assigned pixel contribution is unavailable";
    if (!c->allocation) {
        c->count=a->target->count;
        size_t rgb_bytes=(size_t)c->count*3;
        c->buffer_bytes=rgb_bytes*2;
        if (a->output && !vm_heap_charge(rgb_bytes)) return "control output exceeds aggregate heap budget";
        c->output_charge=a->output ? rgb_bytes : 0;
        c->allocation=demo_calloc(1,c->buffer_bytes);
        if (!c->allocation) { vm_heap_uncharge(c->output_charge); c->output_charge=0; return "memory allocation failed"; }
        c->committed=c->allocation; c->working=c->committed+rgb_bytes;
    }
    a->view=(LbcBuffer){.data=c->working,.count=c->count,.generation=a->generation};
    if(!lbc_init(&a->instance,&a->program,&a->view))return "bytecode instance initialization failed";
    const char *error=control_step(a,0,0);
    if (!error) c->initialized=true;
    return error;
}
const char * control_step(VmInstance *a,uint32_t trigger,uint64_t delta) {
    Control *c=&a->control;
    if (!c->allocation || c->faulted || trigger>1) return "control contribution is not prepared or faulted";
    size_t n=(size_t)c->count*3;
    if (delta>INT64_MAX || c->program_us>(uint64_t)INT64_MAX-delta) return "program clock overflow";
    if (trigger) c->program_us+=delta;
    memset(c->working,0,n);
    c->in_step=true;
    uint64_t started=platform_time_us();
    const char *error=NULL;
    for(uint32_t n=0;n<c->parameter_count;n++) {
        ControlParameter *p=&c->parameters[n];LbcValue v;
        if(p->kind==PARAM_FLOAT)v.f=(float)p->value.real;else v.i=(int32_t)p->value.integer;
        if(!lbc_parameter(&a->instance,c->parameter_registers[n],v.u))error="invalid bytecode parameter";
    }
    // Initial preparation renders declared state without advancing simulation.
    if(!error && !(trigger ? lbc_begin(&a->instance,(float)(c->program_us/1000000.0),(float)(delta/1000000.0)) :
                            lbc_render(&a->instance,0)))error="bytecode invocation failed";
    int status=0;
    while(!error && !(status=lbc_resume(&a->instance,64))) {
        if(platform_time_us()-started>=RUN_TIMEOUT_US)error="execution deadline exceeded";
    }
    if(!error && status<0)error=a->instance.used>=2000000 ? "bytecode instruction limit exceeded" : "bytecode execution fault";
    c->in_step=false;
    if (error) { c->faulted=true; a->playing=false; return error; }
    memcpy(c->committed,c->working,n);
    c->contribution_valid=true; ++c->contribution_frame;
    a->render_us=(uint32_t)(platform_time_us()-started);
    // Rendering commits only this instance's contribution. The native scheduler
    // owns output publication; an offscreen slot never calls a physical driver.
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
static void result(VmInstance *a,uint16_t id,enum Status code,const char *message) {
    send_status(a->send,a->transport,id,code,0,message);
}
static void describe(ControlWriter *w,bool with_parameters) {
    VmInstance *a=w->vm;
    Control *c=&a->control; char b[512];
    snprintf(b,sizeof(b),"{\"slot\":\"%s\",\"generation\":%lu,\"parameter_revision\":%lu,\"program_us\":%" PRIu64 ",\"paused\":%s,\"faulted\":%s,\"contribution_valid\":%s,\"output_valid\":%s,\"contribution_frame\":%lu,\"output_frame\":%lu,\"buffer_bytes\":%lu",
        a->slot,(unsigned long)a->generation,(unsigned long)c->parameter_revision,c->program_us,a->playing ? "false" : "true",c->faulted ? "true" : "false",c->contribution_valid ? "true" : "false",c->output_valid ? "true" : "false",(unsigned long)c->contribution_frame,(unsigned long)c->output_frame,(unsigned long)(c->buffer_bytes+c->output_charge));
    text(w,b);
    if (with_parameters) {
        text(w,",\"manifest\":");
        uint8_t chunk[256];
        for(uint32_t offset=0;offset<c->manifest_size;) {
            size_t n=c->manifest_size-offset;if(n>sizeof(chunk))n=sizeof(chunk);
            if(!vm_read(a,c->manifest_offset+offset,chunk,n))return;
            bytes(w,chunk,n);offset+=(uint32_t)n;
        }
        text(w,",\"values\":{");
        for (uint32_t i=0;i<c->parameter_count;++i) {
            ControlParameter *p=&c->parameters[i];
            snprintf(b,sizeof(b),"%s\"%s\":",i ? "," : "",p->name); text(w,b);
            if (p->kind==PARAM_BOOL) snprintf(b,sizeof(b),"%s",p->value.integer ? "true" : "false");
            else if (p->kind==PARAM_FLOAT) snprintf(b,sizeof(b),"%.17g",p->value.real);
            else snprintf(b,sizeof(b),"%" PRId64,p->value.integer);
            text(w,b);
        }
        text(w,"}");
    }
    text(w,"}");
}
bool control_manage(VmInstance *a,uint16_t id,const uint8_t *payload,size_t size) {
    Json j; char op[40];
    if (!json_parse(&j,payload,size) || !json_string(&j,json_get(&j,0,"op"),op,sizeof(op)) || strncmp(op,"program_",8)) return false;
    bool get=!strcmp(op,"program_parameters_get"),set=!strcmp(op,"program_parameters_set"),pause=!strcmp(op,"program_pause"),resume=!strcmp(op,"program_resume"),snapshot=!strcmp(op,"program_snapshot");
    if (!get && !set && !pause && !resume && !snapshot) return false;
    if (!a->ready || !a->control.enabled) { result(a,id,STATUS_RUN,"no control program loaded"); return true; }
    static const char *const fields[]={"op","expected_generation","expected_parameter_revision","values",NULL};
    static const char *const read_fields[]={"op",NULL};
    static const char *const lifecycle_fields[]={"op","expected_generation",NULL};
    if (!json_fields(&j,0,(get || snapshot) ? read_fields : set ? fields : lifecycle_fields)) { result(a,id,STATUS_PROTOCOL,"invalid control management fields"); return true; }
    uint32_t generation,revision;
    if ((set || pause || resume) && (!json_u32(&j,json_get(&j,0,"expected_generation"),&generation) || generation!=a->generation)) {
        result(a,id,STATUS_CONFIG,"stale program generation"); return true;
    }
    Control *c=&a->control;
    if (set) {
        if (!json_u32(&j,json_get(&j,0,"expected_parameter_revision"),&revision) || revision!=c->parameter_revision || revision==UINT32_MAX) {
            result(a,id,STATUS_CONFIG,"stale parameter revision"); return true;
        }
        int object=json_get(&j,0,"values");
        if (object<0 || j.tokens[object].type!=JSON_OBJECT) { result(a,id,STATUS_CONFIG,"values must be an object"); return true; }
        ParameterValue pending[CONTROL_PARAMETER_LIMIT]; bool changed[CONTROL_PARAMETER_LIMIT]={false};
        for (int t=object+1;t<j.tokens[object].next;t=j.tokens[t+1].next) {
            char name[33]; uint32_t index;
            if (!json_string(&j,t,name,sizeof(name))) { result(a,id,STATUS_CONFIG,"invalid parameter name"); return true; }
            for (index=0;index<c->parameter_count;++index) if (!strcmp(name,c->parameters[index].name)) break;
            if (index==c->parameter_count || !value(&j,t+1,c->parameters[index].kind,&pending[index]) || !in_bounds(&c->parameters[index],pending[index])) {
                result(a,id,STATUS_CONFIG,"unknown parameter or invalid value/bounds"); return true;
            }
            changed[index]=true;
        }
        for (uint32_t i=0;i<c->parameter_count;++i) if (changed[i]) c->parameters[i].value=pending[i];
        ++c->parameter_revision;
    }
    if (pause) a->playing=false;
    if (resume) {
        if (!c->initialized || c->faulted) { result(a,id,STATUS_RUN,"initialize with PLAY/FRAME; reactivate a faulted program"); return true; }
        a->playing=true; a->last_frame=platform_time_us(); a->next_frame=a->last_frame;
    }
    ControlWriter writer={.vm=a,.id=id}; ControlWriter *w=&writer;
    if (snapshot) {
        if (c->output_valid && a->output) emit(a,id,RESP_PIXELS,a->output->rgb,(size_t)a->output->count*3);
        text(w,"{\"state\":"); describe(w,false); text(w,"}");
    } else describe(w,true);
    flush(w);
    result(a,id,STATUS_OK,"control management completed"); return true;
}
