// Original LightVM control profile. MIT License, copyright 2026 Kaj Wik Siebert.
#include "control.h"
#include "vm.h"
#include "vm_allocator.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char invalid[]="invalid control resource, parameter or value";
static const char *const import_names[]={"resource","count","write","fill","read","rgb","hsv","param_int","param_float","param_bool","param_color",NULL};
typedef struct { const uint8_t *bytes; size_t pos,end; } Reader;
static bool leb(Reader *r,uint32_t *value) {
    uint32_t n=0;
    for (unsigned shift=0;shift<35;shift+=7) {
        if (r->pos==r->end) return false;
        uint8_t b=r->bytes[r->pos++];
        if (shift==28 && (b&0xf0)) return false;
        n|=(uint32_t)(b&127)<<shift;
        if (!(b&128)) { *value=n; return true; }
    }
    return false;
}
static bool string(Reader *r,char *out,size_t capacity) {
    uint32_t size;
    if (!leb(r,&size) || size>=capacity || size>r->end-r->pos || memchr(r->bytes+r->pos,0,size)) return false;
    memcpy(out,r->bytes+r->pos,size); out[size]=0; r->pos+=size; return true;
}
static bool value(const Json *j,int token,ParameterKind kind,ParameterValue *out) {
    bool b;
    if (kind==PARAM_BOOL) { if (!json_bool(j,token,&b)) return false; out->integer=b; return true; }
    if (kind==PARAM_FLOAT) return json_f64(j,token,&out->real);
    if (!json_i64(j,token,&out->integer)) return false;
    return kind!=PARAM_RGB || (out->integer>=0 && out->integer<=0xffffff);
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
M3Result control_probe(const uint8_t *bytes,size_t size,Control *c) {
    memset(c,0,sizeof(*c));
    if (size<8 || memcmp(bytes,"\0asm\1\0\0\0",8)) return "invalid WASM header";
    Reader r={bytes,8,size};
    while (r.pos<r.end) {
        uint8_t id=bytes[r.pos++]; uint32_t n;
        if (!leb(&r,&n) || n>r.end-r.pos) return "invalid WASM section bounds";
        Reader s={bytes,r.pos,r.pos+n}; r.pos+=n;
        if (id==0) {
            // Unknown custom sections are allowed, including long debug names.
            uint32_t name_size;
            if (!leb(&s,&name_size) || name_size>s.end-s.pos) return "invalid custom section";
            bool ours=name_size==15 && !memcmp(bytes+s.pos,"lightvm.program",15);
            s.pos+=name_size;
            if (ours) {
                if (c->enabled || !manifest(bytes+s.pos,s.end-s.pos,c)) return "invalid or duplicate LightVM manifest";
                c->manifest_offset=(uint32_t)s.pos; c->manifest_size=(uint32_t)(s.end-s.pos);
            }
        }
    }
    r.pos=8;
    while (r.pos<r.end) {
        uint8_t id=bytes[r.pos++]; uint32_t n;
        if (!leb(&r,&n) || n>r.end-r.pos) return "invalid WASM section";
        Reader s={bytes,r.pos,r.pos+n}; r.pos+=n;
        if (c->enabled && ((id>=4 && id<=5) || id==8 || id==9 || id>=11)) return "control profile requires memoryless scalar WASM without start, tables or data";
        if (id==2) {
            uint32_t count;
            if (!leb(&s,&count)) return "invalid import section";
            for (uint32_t i=0;i<count;++i) {
                char space[128],name[128]; uint32_t type;
                if (!string(&s,space,sizeof(space)) || !string(&s,name,sizeof(name)) || s.pos==s.end) return "invalid import";
                uint8_t kind=bytes[s.pos++];
                if (!c->enabled) {
                    if (!strncmp(space,"lightvm.control.",16)) return "control imports require a LightVM manifest";
                    // wasm3 validates arbitrary legacy import encodings.
                    break;
                }
                if (kind!=0 || !leb(&s,&type) || strcmp(space,CONTROL_NAMESPACE)) return "unsupported control import";
                bool found=false;
                for (unsigned k=0;import_names[k];++k) if (!strcmp(import_names[k],name)) found=true;
                if (!found) return "unsupported control import";
            }
            if (c->enabled && s.pos!=s.end) return "invalid control import section";
        }
    }
    return m3Err_none;
}
static VmInstance *owner(IM3ImportContext context) { return context->userdata; }
static bool handle_valid(VmInstance *a,uint32_t handle) {
    return a->control.in_step && a->control.allocation && handle==a->generation && a->control.count;
}
static bool index_valid(VmInstance *a,uint32_t handle,int64_t index) {
    return handle_valid(a,handle) && index>=0 && (uint64_t)index<a->control.count;
}
static void paint(Control *c,uint32_t index,uint32_t color) {
    c->working[index*3]=(uint8_t)(color>>16);
    c->working[index*3+1]=(uint8_t)(color>>8);
    c->working[index*3+2]=(uint8_t)color;
}
m3ApiRawFunction(resource) {
    (void)runtime; (void)_mem; m3ApiReturnType(uint32_t); m3ApiGetArg(uint32_t,index);
    VmInstance *a=owner(_ctx);
    if (index || !a->control.in_step || !a->control.allocation) return invalid;
    m3ApiReturn(a->generation);
}
m3ApiRawFunction(count) {
    (void)runtime; (void)_mem; m3ApiReturnType(int64_t); m3ApiGetArg(uint32_t,handle);
    VmInstance *a=owner(_ctx); if (!handle_valid(a,handle)) return invalid;
    m3ApiReturn((int64_t)a->control.count);
}
m3ApiRawFunction(write_pixel) {
    (void)runtime; (void)_mem;
    m3ApiGetArg(uint32_t,handle); m3ApiGetArg(int64_t,index); m3ApiGetArg(uint32_t,color);
    VmInstance *a=owner(_ctx);
    if (!index_valid(a,handle,index) || color>0xffffff) return invalid;
    paint(&a->control,(uint32_t)index,color); return m3_Yield();
}
m3ApiRawFunction(fill) {
    (void)runtime; (void)_mem; m3ApiGetArg(uint32_t,handle); m3ApiGetArg(uint32_t,color);
    VmInstance *a=owner(_ctx); if (!handle_valid(a,handle) || color>0xffffff) return invalid;
    for (uint32_t i=0;i<a->control.count;++i) paint(&a->control,i,color);
    return m3_Yield();
}
m3ApiRawFunction(read_pixel) {
    (void)runtime; (void)_mem; m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t,handle); m3ApiGetArg(int64_t,index);
    VmInstance *a=owner(_ctx); if (!index_valid(a,handle,index)) return invalid;
    uint8_t *p=a->control.working+(size_t)index*3;
    m3ApiReturn(((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2]);
}
m3ApiRawFunction(rgb) {
    (void)runtime; (void)_mem; m3ApiReturnType(uint32_t);
    m3ApiGetArg(int64_t,r); m3ApiGetArg(int64_t,g); m3ApiGetArg(int64_t,b);
    if (r<0 || r>255 || g<0 || g>255 || b<0 || b>255 || !owner(_ctx)->control.in_step) return invalid;
    m3ApiReturn(((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b);
}
m3ApiRawFunction(hsv) {
    (void)runtime; (void)_mem; m3ApiReturnType(uint32_t);
    m3ApiGetArg(double,h); m3ApiGetArg(double,s); m3ApiGetArg(double,v);
    if (!isfinite(h) || !isfinite(s) || !isfinite(v) || s<0 || s>1 || v<0 || v>1 || !owner(_ctx)->control.in_step) return invalid;
    uint8_t channels[3]; PixelBuffer p={.rgb=channels,.count=1};
    uint32_t hue=(uint32_t)((h-floor(h))*65536.0+0.5)&65535u;
    pixels_set_hsv(&p,0,hue,(uint32_t)(s*255.0+0.5),(uint32_t)(v*255.0+0.5));
    m3ApiReturn(((uint32_t)channels[0]<<16)|((uint32_t)channels[1]<<8)|channels[2]);
}
static ControlParameter *parameter(VmInstance *a,uint32_t slot,ParameterKind kind) {
    return a->control.in_step && slot<a->control.parameter_count && a->control.parameters[slot].kind==kind ? &a->control.parameters[slot] : NULL;
}
m3ApiRawFunction(param_int) {
    (void)runtime; (void)_mem; m3ApiReturnType(int64_t); m3ApiGetArg(uint32_t,slot);
    ControlParameter *p=parameter(owner(_ctx),slot,PARAM_INT); if (!p) return invalid;
    m3ApiReturn(p->value.integer);
}
m3ApiRawFunction(param_float) {
    (void)runtime; (void)_mem; m3ApiReturnType(double); m3ApiGetArg(uint32_t,slot);
    ControlParameter *p=parameter(owner(_ctx),slot,PARAM_FLOAT); if (!p) return invalid;
    m3ApiReturn(p->value.real);
}
m3ApiRawFunction(param_bool) {
    (void)runtime; (void)_mem; m3ApiReturnType(uint32_t); m3ApiGetArg(uint32_t,slot);
    ControlParameter *p=parameter(owner(_ctx),slot,PARAM_BOOL); if (!p) return invalid;
    m3ApiReturn((uint32_t)p->value.integer);
}
m3ApiRawFunction(param_color) {
    (void)runtime; (void)_mem; m3ApiReturnType(uint32_t); m3ApiGetArg(uint32_t,slot);
    ControlParameter *p=parameter(owner(_ctx),slot,PARAM_RGB); if (!p) return invalid;
    m3ApiReturn((uint32_t)p->value.integer);
}
M3Result control_link(VmInstance *a) {
    struct Binding { const char *name,*signature; M3RawCall function; } bindings[]={
        {"resource","i(i)",resource},{"count","I(i)",count},
        {"write","v(iIi)",write_pixel},{"fill","v(ii)",fill},
        {"read","i(iI)",read_pixel},
        {"rgb","i(III)",rgb},{"hsv","i(FFF)",hsv},
        {"param_int","I(i)",param_int},{"param_float","F(i)",param_float},
        {"param_bool","i(i)",param_bool},{"param_color","i(i)",param_color}
    };
    for (size_t i=0;i<sizeof(bindings)/sizeof(bindings[0]);++i) {
        M3Result error=m3_LinkRawFunctionEx(a->module,CONTROL_NAMESPACE,bindings[i].name,bindings[i].signature,bindings[i].function,a);
        if (error && error!=m3Err_functionLookupFailed) return error;
    }
    return m3Err_none;
}
void control_destroy(VmInstance *a) {
    demo_free(a->control.allocation);
    vm_heap_uncharge(a->control.output_charge);
    memset(&a->control,0,sizeof(a->control));
}
M3Result control_prepare(VmInstance *a) {
    Control *c=&a->control;
    if (c->faulted) return "control program faulted; reactivate to recover";
    if (c->initialized) return m3Err_none;
    if (!a->ready || !a->target || !a->target->count || a->target->count>PIXEL_CAPACITY) return "assigned pixel contribution is unavailable";
    M3Result error=m3_FindFunction(&c->step,a->runtime,"control_step");
    if (error) return error;
    if (m3_GetArgCount(c->step)!=3 || m3_GetArgType(c->step,0)!=c_m3Type_i32 ||
        m3_GetArgType(c->step,1)!=c_m3Type_i64 || m3_GetArgType(c->step,2)!=c_m3Type_i64 ||
        m3_GetRetCount(c->step)!=1 || m3_GetRetType(c->step,0)!=c_m3Type_i32) return "control_step must be (i32, i64, i64) -> i32";
    if (!c->allocation) {
        c->count=a->target->count;
        size_t rgb_bytes=(size_t)c->count*3;
        c->buffer_bytes=rgb_bytes*2;
        if (a->output && !vm_heap_charge(rgb_bytes)) return "control output exceeds aggregate heap budget";
        c->output_charge=a->output ? rgb_bytes : 0;
        c->allocation=demo_calloc(1,c->buffer_bytes);
        if (!c->allocation) { vm_heap_uncharge(c->output_charge); c->output_charge=0; return m3Err_mallocFailed; }
        c->committed=c->allocation; c->working=c->committed+rgb_bytes;
    }
    error=control_step(a,0,0);
    if (!error) c->initialized=true;
    return error;
}
M3Result control_step(VmInstance *a,uint32_t trigger,uint64_t delta) {
    Control *c=&a->control;
    if (!c->allocation || c->faulted || trigger>1) return "control contribution is not prepared or faulted";
    size_t n=(size_t)c->count*3;
    if (delta>INT64_MAX || c->program_us>(uint64_t)INT64_MAX-delta) return "program clock overflow";
    if (trigger) c->program_us+=delta;
    memset(c->working,0,n);
    c->in_step=true;
    uint64_t started=platform_time_us();
    M3Result error=m3_CallV(c->step,(int32_t)trigger,(int64_t)c->program_us,(int64_t)delta);
    int32_t result=0;
    if (!error) error=m3_GetResultsV(c->step,&result);
    c->in_step=false;
    if (!error && result) error="control_step returned failure";
    if (error) { c->faulted=true; a->playing=false; return error; }
    memcpy(c->committed,c->working,n);
    c->contribution_valid=true; ++c->contribution_frame;
    a->render_us=(uint32_t)(platform_time_us()-started);
    // Rendering commits only this instance's contribution. The native scheduler
    // owns output publication; an offscreen slot never calls a physical driver.
    return m3Err_none;
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
        text(w,",\"manifest\":"); bytes(w,a->bytes+c->manifest_offset,c->manifest_size);
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
bool control_set_float(VmInstance *a,const char *name,uint32_t generation,uint32_t revision,double real) {
    Control *c=&a->control;
    if (!a->ready || !c->enabled || generation!=a->generation || revision!=c->parameter_revision || revision==UINT32_MAX) return false;
    ParameterValue pending={.real=real};
    for (uint32_t i=0;i<c->parameter_count;++i) {
        ControlParameter *p=&c->parameters[i];
        if (!strcmp(p->name,name) && p->kind==PARAM_FLOAT && isfinite(real) && in_bounds(p,pending)) {
            p->value=pending; ++c->parameter_revision; return true;
        }
    }
    return false;
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
