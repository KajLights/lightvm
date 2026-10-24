/* Live scalar inputs: admitted storage, single-owner publications and render snapshots. */
#include "inputs.h"
#include "portable.h"
#include "platform.h"
#include "assets.h"
#include "outputs.h"
#include "vm_allocator.h"
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const types[]={"","int","float","rgb8","bool"};
static unsigned scalar_type(const Json *j,int token){
    for(unsigned n=1;n<=4;n++)if(json_equal(j,token,types[n]))return n;
    return 0;
}
bool input_value_parse(const Json *j,int token,unsigned type,InputValue *out){
    bool b;double f;int64_t i;
    if(type==T_BOOL){if(!json_bool(j,token,&b))return false;out->u=b;return true;}
    if(type==T_FLOAT){if(!json_f64(j,token,&f)||fabs(f)>FLT_MAX)return false;out->f=(float)f;return isfinite(out->f);}
    if(!json_i64(j,token,&i)||i<INT32_MIN||i>INT32_MAX||(type==T_COLOR&&(i<0||i>0xffffff)))return false;
    out->i=(int32_t)i;return true;
}
double input_value_number(InputValue v,unsigned type){return type==T_FLOAT?v.f:v.i;}
static bool in_bounds(const InputPort *port,InputValue value){
    double v=input_value_number(value,port->type);
    return isfinite(v)&&(!(port->bounds&1)||v>=input_value_number(port->minimum,port->type))&&
        (!(port->bounds&2)||v<=input_value_number(port->maximum,port->type));
}
static bool mode(const Json *j,int token,uint8_t *out){
    static const char *const names[]={"once","repeat","pingpong"};
    for(unsigned n=0;n<3;n++)if(json_equal(j,token,names[n])){*out=(uint8_t)(n+1);return true;}
    return false;
}
static bool easing(const Json *j,int token,uint8_t *out){
    if(json_equal(j,token,"linear")){*out=0;return true;}
    if(json_equal(j,token,"smoothstep")){*out=1;return true;}
    return false;
}
static bool light_control(const Json *j,int token,const InputPort *port,uint8_t *out){
    if(json_equal(j,token,"color")&&port->type==T_COLOR){*out=LIGHT_CONTROL_COLOR;return true;}
    if(json_equal(j,token,"brightness")&&port->type==T_FLOAT&&
       in_bounds(port,(InputValue){.f=0})&&in_bounds(port,(InputValue){.f=1})){
        *out=LIGHT_CONTROL_BRIGHTNESS;return true;
    }
    return false;
}
/* Shared by input ramps and native transition timing. interval==0 means no dwell. */
double input_ramp(double from,double to,double elapsed,double duration,double interval,unsigned kind){
    double span=interval>0?interval:duration;
    if(span<=0)return to;
    double leg=floor(fmax(0,elapsed)/span),phase=fmax(0,elapsed);
    if(kind>1)phase=fmod(phase,span);
    double u=duration>0?fmin(1,phase/duration):1;
    if(kind==3&&fmod(leg,2)>=1)u=1-u;
    return from+(to-from)*u;
}
static void default_driver(InputPort *port){
    memset(&port->driver,0,sizeof(port->driver));
    port->driver.kind=port->default_control?INPUT_CONTROL:port->default_mode?INPUT_RAMP:INPUT_CONSTANT;
    port->driver.control=port->default_control;
    port->driver.value=port->initial;port->driver.to=1;
    port->driver.duration=port->default_duration;
    port->driver.mode=port->default_mode;port->driver.easing=port->default_easing;
}
const char *inputs_load(Portable *p,const Json *j){
    const char *error="invalid input declarations";
    int declarations=json_get(j,0,"inputs");
    if(declarations<0){error=NULL;goto done;}
    if(j->tokens[declarations].type!=JSON_ARRAY||(unsigned)j->tokens[declarations].children>INPUT_LIMIT)goto done;
    p->ninputs=(unsigned)j->tokens[declarations].children;
    if(p->ninputs){p->inputs=demo_calloc(p->ninputs,sizeof(*p->inputs));if(!p->inputs){error="inputs exceed memory budget";goto done;}}
    unsigned index=0;
    for(int t=declarations+1;t<j->tokens[declarations].next;t=j->tokens[t].next,index++){
        static const char *const fields[]={"name","type","default","minimum","maximum","unit","driver","control",NULL};
        InputPort *port=&p->inputs[index];
        if(!json_fields(j,t,fields)||!json_string(j,json_get(j,t,"name"),port->name,sizeof(port->name))||!object_name_valid(port->name)||
           !(port->type=scalar_type(j,json_get(j,t,"type")))||!input_value_parse(j,json_get(j,t,"default"),port->type,&port->initial))goto done;
        for(unsigned n=0;n<index;n++)if(!strcmp(port->name,p->inputs[n].name))goto done;
        int unit=json_get(j,t,"unit");
        if(unit>=0){
            if(!json_string(j,unit,port->unit,sizeof(port->unit)))goto done;
            for(const char *s=port->unit;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||strchr("_./%-",*s)))goto done;
        }
        const char *const bounds[]={"minimum","maximum"};
        for(unsigned n=0;n<2;n++){
            int bound=json_get(j,t,bounds[n]);if(bound<0)continue;
            if((port->type!=T_INT&&port->type!=T_FLOAT)||!input_value_parse(j,bound,port->type,n?&port->maximum:&port->minimum))goto done;
            port->bounds|=1u<<n;
        }
        if(!in_bounds(port,port->initial))goto done;
        int control=json_get(j,t,"control");
        if(control>=0&&!light_control(j,control,port,&port->default_control))goto done;
        int driver=json_get(j,t,"driver");
        if(driver>=0){
            if(control>=0)goto done;
            static const char *const driver_fields[]={"kind","duration","mode","easing",NULL};
            if(port->type!=T_FLOAT||!json_fields(j,driver,driver_fields)||!json_equal(j,json_get(j,driver,"kind"),"ramp")||
               !json_f64(j,json_get(j,driver,"duration"),&port->default_duration)||port->default_duration<=0||port->default_duration>1e9||
               !mode(j,json_get(j,driver,"mode"),&port->default_mode)||!easing(j,json_get(j,driver,"easing"),&port->default_easing)||
               !in_bounds(port,(InputValue){.f=0})||!in_bounds(port,(InputValue){.f=1}))goto done;
        }
        port->snapshot=port->last=port->initial;default_driver(port);
    }
    error=NULL;
done:
    return error;
}
void inputs_attach(Portable *p,InputRegistry *registry,const LightControls *light){
    p->input_registry=registry;
    for(unsigned n=0;n<p->ninputs;n++)p->inputs[n].light=light;
}
bool inputs_call_valid(Portable *p,unsigned binding,unsigned reg){
    return binding>=55&&binding<=58&&pbc_type(p,reg)==T_INT&&pbc_role(p,reg)==R_CONST&&
        pbc_initial(p,reg)<p->ninputs&&p->inputs[pbc_initial(p,reg)].type==binding-54;
}
static InputSignal *signal(InputRegistry *registry,const char *name){
    if(registry)for(unsigned n=0;n<registry->native_count;n++)if(!strcmp(registry->native[n].name,name))return &registry->native[n];
    if(registry)for(unsigned n=0;n<registry->count;n++)if(!strcmp(registry->signals[n].name,name))return &registry->signals[n];
    return NULL;
}
static bool source_value(InputRegistry *registry,InputPort *port,uint64_t now,InputValue *out){
    InputDriver *driver=&port->driver;InputSignal *source=signal(registry,driver->source);
    if(source&&source->valid){
        bool ok=!driver->mapped&&source->type==port->type;InputValue value=source->value;
        if(driver->mapped&&(source->type==T_FLOAT||source->type==T_INT)){
            double u=(input_value_number(source->value,source->type)-driver->input_min)/(driver->input_max-driver->input_min);
            double v=driver->output_min+fmax(0,fmin(1,u))*(driver->output_max-driver->output_min);
            if(driver->palette&&port->type==T_COLOR){
                uint32_t from=(uint32_t)driver->output_min,to=(uint32_t)driver->output_max;value.u=0;
                u=fmax(0,fmin(1,u));
                for(unsigned shift=0;shift<24;shift+=8){
                    double a=(from>>shift)&255,b=(to>>shift)&255;
                    value.u|=(uint32_t)round(a+(b-a)*u)<<shift;
                }
            }else if(port->type==T_FLOAT)value.f=(float)v;
            else if(port->type==T_INT&&v>=INT32_MIN&&v<=INT32_MAX)value.i=(int32_t)round(v);
            else return false;
            ok=true;
        }
        if(ok&&in_bounds(port,value)){port->last=value;port->last_received=source->received;port->has_last=true;}
    }
    if(port->has_last&&(driver->hold||!driver->timeout_ms||now-port->last_received<=(uint64_t)driver->timeout_ms*1000)){
        *out=port->last;return true;
    }
    return false;
}
static InputValue evaluate(InputRegistry *registry,InputPort *port,uint64_t time,uint64_t now,bool *fallback){
    InputDriver *driver=&port->driver;*fallback=false;
    if(driver->kind==INPUT_CONSTANT)return driver->value;
    if(driver->kind==INPUT_CONTROL){
        if(!port->light){*fallback=true;return port->initial;}
        return driver->control==LIGHT_CONTROL_COLOR?(InputValue){.u=port->light->color}:(InputValue){.f=port->light->brightness};
    }
    if(driver->kind==INPUT_SOURCE){InputValue value;if(source_value(registry,port,now,&value))return value;*fallback=true;return port->initial;}
    double elapsed=time>=driver->started?(double)(time-driver->started)/1000000:0;
    double u=input_ramp(0,1,elapsed+driver->phase*driver->duration,driver->duration,0,driver->mode);
    if(driver->easing)u=u*u*(3-2*u);
    return (InputValue){.f=(float)(driver->from+(driver->to-driver->from)*u)};
}
static double inverse_ease(double u){
    double lo=0,hi=1;
    for(unsigned n=0;n<24;n++){double v=(lo+hi)/2;if(v*v*(3-2*v)<u)lo=v;else hi=v;}
    return (lo+hi)/2;
}
const char *input_port_bind(InputRegistry *registry,InputPort *port,const Json *j,int object,uint64_t time){
    static const char *const fields[]={"value","driver","source","control","map","palette","timeout_ms","fallback","default",NULL};
    if(!json_fields(j,object,fields))return "invalid input binding fields";
    int value=json_get(j,object,"value"),ramp=json_get(j,object,"driver"),source=json_get(j,object,"source"),reset=json_get(j,object,"default"),control=json_get(j,object,"control");
    if((value>=0)+(ramp>=0)+(source>=0)+(reset>=0)+(control>=0)!=1)return "input binding needs one value, driver, source, control or default";
    bool unused;InputValue current=evaluate(registry,port,time,platform_time_us(),&unused);InputDriver old=port->driver;
    memset(&port->driver,0,sizeof(port->driver));InputDriver *driver=&port->driver;
    driver->started=time;
    if(control>=0){
        if(j->tokens[object].children!=1||!light_control(j,control,port,&driver->control))return "light control does not match the input type or bounds";
        driver->kind=INPUT_CONTROL;return NULL;
    }
    if(value>=0){
        if(j->tokens[object].children!=1||!input_value_parse(j,value,port->type,&driver->value)||!in_bounds(port,driver->value))return "input value is outside its declared type or bounds";
        return NULL;
    }
    if(reset>=0){
        bool enabled;if(j->tokens[object].children!=1||!json_bool(j,reset,&enabled)||!enabled)return "default must be true";
        default_driver(port);driver->started=time;return NULL;
    }
    if(ramp>=0){
        static const char *const ramp_fields[]={"kind","duration","mode","easing","from","to","restart",NULL};
        if(j->tokens[object].children!=1||port->type!=T_FLOAT||!json_fields(j,ramp,ramp_fields)||!json_equal(j,json_get(j,ramp,"kind"),"ramp")||
           !json_f64(j,json_get(j,ramp,"duration"),&driver->duration)||driver->duration<=0||driver->duration>1e9)return "ramp requires a float input and a positive duration";
        driver->kind=INPUT_RAMP;driver->mode=1;driver->to=1;
        int m=json_get(j,ramp,"mode"),e=json_get(j,ramp,"easing"),from=json_get(j,ramp,"from"),to=json_get(j,ramp,"to"),restart=json_get(j,ramp,"restart");
        bool initial=false;
        if((m>=0&&!mode(j,m,&driver->mode))||(e>=0&&!easing(j,e,&driver->easing))||(from>=0&&!json_f64(j,from,&driver->from))||
           (to>=0&&!json_f64(j,to,&driver->to))||(restart>=0&&!json_bool(j,restart,&initial))||fabs(driver->from)>FLT_MAX||fabs(driver->to)>FLT_MAX||
           !in_bounds(port,(InputValue){.f=(float)driver->from})||!in_bounds(port,(InputValue){.f=(float)driver->to}))return "invalid ramp range, mode or easing";
        if(!initial&&driver->from!=driver->to){
            double u=fmax(0,fmin(1,(current.f-driver->from)/(driver->to-driver->from)));
            if(driver->easing)u=inverse_ease(u);
            if(driver->mode==3&&old.kind==INPUT_RAMP&&old.mode==3){
                double phase=old.phase+(time>=old.started?(double)(time-old.started)/1000000/old.duration:0);
                if(fmod(phase,2)>=1)u=2-u;
            }
            driver->phase=u;
        }
        return NULL;
    }
    driver->kind=INPUT_SOURCE;driver->timeout_ms=1000;
    if(!json_string(j,source,driver->source,sizeof(driver->source))||!object_name_valid(driver->source))return "invalid source name";
    int timeout=json_get(j,object,"timeout_ms"),fallback=json_get(j,object,"fallback"),map=json_get(j,object,"map"),palette=json_get(j,object,"palette");
    if(timeout>=0&&!json_u32(j,timeout,&driver->timeout_ms))return "invalid source timeout";
    if(fallback>=0){if(json_equal(j,fallback,"hold"))driver->hold=true;else if(!json_equal(j,fallback,"default"))return "fallback must be hold or default";}
    if(palette>=0){
        static const char *const palette_fields[]={"input_min","input_max","from","to",NULL};uint32_t from,to;
        if(map>=0||port->type!=T_COLOR||!json_fields(j,palette,palette_fields)||
           !json_f64(j,json_get(j,palette,"input_min"),&driver->input_min)||
           !json_f64(j,json_get(j,palette,"input_max"),&driver->input_max)||driver->input_min>=driver->input_max||
           fabs(driver->input_min)>FLT_MAX||fabs(driver->input_max)>FLT_MAX||
           !json_u32(j,json_get(j,palette,"from"),&from)||from>0xffffff||
           !json_u32(j,json_get(j,palette,"to"),&to)||to>0xffffff)return "invalid numeric-to-color palette";
        driver->mapped=driver->palette=true;driver->output_min=from;driver->output_max=to;
    }
    if(map>=0){
        static const char *const map_fields[]={"input_min","input_max","output_min","output_max",NULL};
        double values[4];
        if((port->type!=T_INT&&port->type!=T_FLOAT)||!json_fields(j,map,map_fields))return "mapping requires a numeric input";
        for(unsigned n=0;n<4;n++)if(!json_f64(j,json_get(j,map,map_fields[n]),&values[n])||fabs(values[n])>FLT_MAX)return "invalid input mapping";
        driver->input_min=values[0];driver->input_max=values[1];driver->output_min=values[2];driver->output_max=values[3];driver->mapped=true;
        if(driver->input_min>=driver->input_max)return "mapping source range must increase";
        for(unsigned n=2;n<4;n++){
            InputValue endpoint;
            if(port->type==T_FLOAT)endpoint.f=(float)values[n];
            else {if(values[n]<INT32_MIN||values[n]>INT32_MAX)return "mapping exceeds int32";endpoint.i=(int32_t)round(values[n]);}
            if(!in_bounds(port,endpoint))return "mapping exceeds input bounds";
        }
    }
    InputSignal *published=signal(registry,driver->source);
    if(published&&!(driver->mapped?(published->type==T_INT||published->type==T_FLOAT):published->type==port->type))return "source and input types differ";
    port->has_last=false;return NULL;
}
const char *inputs_bind(Portable *p,const Json *j,int object,uint64_t time){
    if(object<0)return NULL;
    if(j->tokens[object].type!=JSON_OBJECT)return "inputs must be an object";
    if(!p->ninputs)return j->tokens[object].children?"program has no live inputs":NULL;
    if(p->input_revision==UINT32_MAX)return "input revision exhausted";
    InputPort *candidate=demo_calloc(p->ninputs,sizeof(*candidate));
    if(!candidate)return "input bindings exceed peak memory budget";
    memcpy(candidate,p->inputs,p->ninputs*sizeof(*candidate));const char *error=NULL;
    for(int t=object+1;t<j->tokens[object].next;t=j->tokens[t+1].next){
        char name[33];unsigned n;
        if(!json_string(j,t,name,sizeof(name))){error="invalid input name";break;}
        for(n=0;n<p->ninputs;n++)if(!strcmp(candidate[n].name,name))break;
        if(n==p->ninputs){error="unknown input";break;}
        error=input_port_bind(p->input_registry,&candidate[n],j,t+1,time);if(error)break;
    }
    if(!error){memcpy(p->inputs,candidate,p->ninputs*sizeof(*candidate));p->input_revision++;}
    demo_free(candidate);return error;
}
void inputs_capture(Portable *p,uint64_t time,bool baseline){
    uint64_t now=platform_time_us();
    for(unsigned n=0;n<p->ninputs;n++){
        InputPort *port=&p->inputs[n];
        if(baseline){port->snapshot=port->initial;port->fallback=false;}
        else port->snapshot=evaluate(p->input_registry,port,time,now,&port->fallback);
    }
}
bool input_port_capture(InputRegistry *registry,InputPort *port,uint64_t time){
    InputValue previous=port->snapshot;bool fallback=port->fallback;
    port->snapshot=evaluate(registry,port,time,platform_time_us(),&port->fallback);
    return previous.u!=port->snapshot.u||fallback!=port->fallback;
}
void inputs_sample(Portable *p,uint64_t time,uint32_t *values,uint8_t *fallbacks){
    uint64_t now=platform_time_us();*fallbacks=0;
    for(unsigned n=0;n<p->ninputs;n++){
        bool fallback;values[n]=evaluate(p->input_registry,&p->inputs[n],time,now,&fallback).u;
        if(fallback)*fallbacks|=(uint8_t)(1u<<n);
    }
}
bool inputs_changed(Portable *p,uint64_t time){
    uint64_t now=platform_time_us();
    for(unsigned n=0;n<p->ninputs;n++){
        InputPort *port=&p->inputs[n];bool fallback;
        InputValue value=evaluate(p->input_registry,port,time,now,&fallback);
        if(value.u!=port->snapshot.u||fallback!=port->fallback)return true;
    }
    return false;
}
static void value_text(char *b,size_t capacity,InputValue value,unsigned type){
    if(type==T_FLOAT)snprintf(b,capacity,"%.17g",(double)value.f);
    else if(type==T_BOOL)snprintf(b,capacity,"%s",value.u?"true":"false");
    else snprintf(b,capacity,"%" PRId32,value.i);
}
void input_binding_describe(const InputPort *port,void (*write)(void *,const char *),void *context){
        char b[384],v[32];const InputDriver *driver=&port->driver;write(context,"{");
        if(driver->kind==INPUT_CONSTANT){value_text(v,sizeof(v),driver->value,port->type);snprintf(b,sizeof(b),"\"value\":%s",v);write(context,b);}
        else if(driver->kind==INPUT_RAMP){
            static const char *const modes[]={"","once","repeat","pingpong"};
            snprintf(b,sizeof(b),"\"driver\":{\"kind\":\"ramp\",\"duration\":%.17g,\"mode\":\"%s\",\"easing\":\"%s\",\"from\":%.17g,\"to\":%.17g}",driver->duration,modes[driver->mode],driver->easing?"smoothstep":"linear",driver->from,driver->to);write(context,b);
        }else if(driver->kind==INPUT_CONTROL){
            snprintf(b,sizeof(b),"\"control\":\"%s\"",driver->control==LIGHT_CONTROL_COLOR?"color":"brightness");write(context,b);
        }else{
            snprintf(b,sizeof(b),"\"source\":\"%s\",\"timeout_ms\":%" PRIu32 ",\"fallback\":\"%s\"",driver->source,driver->timeout_ms,driver->hold?"hold":"default");write(context,b);
            if(driver->palette){snprintf(b,sizeof(b),",\"palette\":{\"input_min\":%.17g,\"input_max\":%.17g,\"from\":%u,\"to\":%u}",driver->input_min,driver->input_max,(unsigned)driver->output_min,(unsigned)driver->output_max);write(context,b);}
            else if(driver->mapped){snprintf(b,sizeof(b),",\"map\":{\"input_min\":%.17g,\"input_max\":%.17g,\"output_min\":%.17g,\"output_max\":%.17g}",driver->input_min,driver->input_max,driver->output_min,driver->output_max);write(context,b);}
        }
        write(context,"}");
}
void inputs_describe(Portable *p,void (*write)(void *,const char *),void *context){
    char b[160],v[32];snprintf(b,sizeof(b),",\"inputs_revision\":%" PRIu32 ",\"inputs\":{",p->input_revision);write(context,b);
    for(unsigned n=0;n<p->ninputs;n++){
        InputPort *port=&p->inputs[n];value_text(v,sizeof(v),port->snapshot,port->type);
        snprintf(b,sizeof(b),"%s\"%s\":{\"value\":%s,\"fallback\":%s,\"binding\":",n?",":"",port->name,v,port->fallback?"true":"false");write(context,b);
        input_binding_describe(port,write,context);write(context,"}");
    }
    write(context,"}");
}
void input_registry_destroy(InputRegistry *registry){demo_free(registry->signals);memset(registry,0,sizeof(*registry));}
const char *input_registry_claim(InputRegistry *registry,const Json *j,int declarations,uint32_t *generation){
    if(declarations<0||j->tokens[declarations].type!=JSON_ARRAY||!j->tokens[declarations].children||j->tokens[declarations].children>(int)INPUT_SOURCE_LIMIT)return "signals must be a bounded nonempty array";
    if(registry->generation==UINT32_MAX)return "publisher generation exhausted";
    InputSignal *candidate=demo_calloc(INPUT_SOURCE_LIMIT,sizeof(*candidate));if(!candidate)return "sources exceed peak memory budget";
    if(registry->count)memcpy(candidate,registry->signals,registry->count*sizeof(*candidate));
    unsigned count=registry->count;uint32_t next=registry->generation+1;const char *error="invalid source declaration";
    for(int t=declarations+1;t<j->tokens[declarations].next;t=j->tokens[t].next){
        static const char *const fields[]={"name","type",NULL};char name[33];unsigned type,index;
        if(!json_fields(j,t,fields)||!json_string(j,json_get(j,t,"name"),name,sizeof(name))||!object_name_valid(name)||!(type=scalar_type(j,json_get(j,t,"type"))))goto done;
        if(!strncmp(name,"board.",6)){error="board sources are reserved and read-only";goto done;}
        if(!strncmp(name,"light.",6)){error="light controls are reserved; use a control binding";goto done;}
        for(index=0;index<count;index++)if(!strcmp(candidate[index].name,name))break;
        if(index==count){if(count==INPUT_SOURCE_LIMIT){error="source limit exceeded";goto done;}count++;}
        else if(candidate[index].publisher==next)goto done;
        else {uint32_t previous=candidate[index].publisher;for(unsigned n=0;n<count;n++)if(candidate[n].publisher==previous){candidate[n].publisher=0;candidate[n].valid=false;}}
        memset(&candidate[index],0,sizeof(candidate[index]));strcpy(candidate[index].name,name);candidate[index].type=type;candidate[index].publisher=next;
    }
    demo_free(registry->signals);registry->signals=candidate;registry->count=count;registry->generation=next;*generation=next;return NULL;
done:
    demo_free(candidate);return error;
}
const char *input_registry_publish(InputRegistry *registry,const Json *j,int values,uint32_t generation,uint32_t sequence){
    if(values<0||j->tokens[values].type!=JSON_OBJECT||!generation||!sequence)return "values, a publisher generation and a positive sequence are required";
    InputValue staged[INPUT_SOURCE_LIMIT];uint32_t mask=0,expected=0;
    for(unsigned n=0;n<registry->count;n++)if(registry->signals[n].publisher==generation)expected|=1u<<n;
    if(!expected)return "stale publisher generation";
    for(int t=values+1;t<j->tokens[values].next;t=j->tokens[t+1].next){
        char name[33];InputSignal *source;
        if(!json_string(j,t,name,sizeof(name))||!(source=signal(registry,name))||source->publisher!=generation||sequence<=source->sequence)return "unknown source or stale publisher sequence";
        unsigned n=(unsigned)(source-registry->signals);
        if(mask&(1u<<n)||!input_value_parse(j,t+1,source->type,&staged[n]))return "invalid source value";
        mask|=1u<<n;
    }
    if(mask!=expected)return "publish all signals owned by this generation atomically";
    uint64_t now=platform_time_us();
    for(unsigned n=0;n<registry->count;n++)if(mask&(1u<<n)){InputSignal *source=&registry->signals[n];source->value=staged[n];source->sequence=sequence;source->received=now;source->valid=true;}
    return NULL;
}
