/* Controller-owned scene operations. Every tick performs bounded units and
 * returns to the caller; no pending work holds a session callback or C stack. */
#include "preparation.h"
#include "app.h"
#include "animation.h"
#include "vm_allocator.h"
#include "runtime.h"
#include "draw.h"
#include <stdio.h>
#include <string.h>

#define PREPARATION_PENDING_LIMIT 4u
#define PREPARATION_HISTORY 8u
static const char *const timed_stages[]={"accepted","load","decode","verify","admit","initialise","first_render","compose","ready","refresh","persist","cleanup"};
#define PREPARATION_STAGE_COUNT (sizeof(timed_stages)/sizeof(*timed_stages))

typedef struct {
    uint32_t id;
    bool terminal;
    char output[33],error[100];
    const char *stage;
    uint32_t code,value,units,target_generation,target_revision,presentation_before;
    bool recalled,scene_activated;
    uint64_t source,accepted,activated,first_presented,fade_completed,finished,max_step;
    uint64_t stage_us[PREPARATION_STAGE_COUNT];
} PreparationRecord;
typedef struct PreparationOperation {
    struct PreparationOperation *next;
    PreparedScene scene;
    PreparationRecord record;
    char *document;
    size_t size,loaded;
    int pin;
    PbcValue reset_settings[16];
    uint32_t reset_mask;
    InputPort *reset_inputs;
    unsigned reset_input_count;
    bool cancelled;
    char *config_document;size_t config_size;
    PreparationRequest config_request;
} PreparationOperation;
typedef struct PreparationQueue {
    PreparationOperation *head,*tail;
    uint32_t sequence;
    unsigned count,history_cursor,phase,index,total,capture_index;
    PreparationRecord history[PREPARATION_HISTORY];
    Json *json;
    JsonParser parser;
    PbcLoader loader;
    AnimationPreparation animation;
    VmInstance *vms[COMPOSITION_LAYER_LIMIT+2];
    int tokens[COMPOSITION_LAYER_LIMIT+2];
    size_t baseline_offset;
    StoreWrite write;unsigned persist_index;uint32_t persist_light_revision;uint8_t persist_data[64];bool refreshed;
    HardwareConfig *configuration;AppOutput *validation_output;unsigned config_index;
} PreparationQueue;

/* Batch progress JSON into protocol-sized frames. Per-field frames made the
 * observer itself stall preparation while USB drained dozens of short packets. */
typedef struct { App *app;uint16_t id;char bytes[FRAME_LIMIT];size_t used; } RecordWriter;
static void record_flush(RecordWriter *w){
    if(w->used)w->app->send(w->app->transport,RESP_DATA,w->id,(const uint8_t *)w->bytes,w->used);
    w->used=0;
}
static void record_text(RecordWriter *w,const char *text){
    while(*text){
        w->bytes[w->used++]=*text++;
        if(w->used==sizeof(w->bytes))record_flush(w);
    }
}
static void record_number(RecordWriter *w,uint64_t value){
    char text[21];json_format_u64(text,value);record_text(w,text);
}
static void record_string(RecordWriter *w,const char *text){
    static const char hex[]="0123456789abcdef";
    record_text(w,"\"");
    for(const unsigned char *p=(const unsigned char *)text;*p;p++){
        char escaped[7]={(char)*p,0};
        if(*p=='"'||*p=='\\'){escaped[0]='\\';escaped[1]=(char)*p;escaped[2]=0;}
        else if(*p<32){memcpy(escaped,"\\u00",4);escaped[4]=hex[*p>>4];escaped[5]=hex[*p&15];escaped[6]=0;}
        record_text(w,escaped);
    }
    record_text(w,"\"");
}
static void record_reply(App *a,uint16_t id,const PreparationRecord *r){
    a->last_code=STATUS_OK;RecordWriter w={.app=a,.id=id};
    record_text(&w,"{\"operation\":{\"boot_id\":");record_string(&w,a->boot_id);
    record_text(&w,",\"id\":");record_number(&w,r->id);
    record_text(&w,",\"output\":");record_string(&w,r->output);
    record_text(&w,",\"stage\":");record_string(&w,r->stage);
    record_text(&w,",\"units\":");record_number(&w,r->units);
    record_text(&w,",\"code\":");record_number(&w,r->code);
    record_text(&w,",\"value\":");record_number(&w,r->value);
    record_text(&w,",\"accepted_us\":");record_number(&w,r->accepted);
    record_text(&w,",\"activated_us\":");record_number(&w,r->activated);
    record_text(&w,",\"first_presented_us\":");record_number(&w,r->first_presented);
    record_text(&w,",\"fade_completed_us\":");record_number(&w,r->fade_completed);
    record_text(&w,",\"terminal\":");record_text(&w,r->terminal?"true":"false");
    record_text(&w,",\"finished_us\":");record_number(&w,r->finished);
    record_text(&w,",\"max_step_us\":");record_number(&w,r->max_step);
    record_text(&w,",\"error\":");record_string(&w,r->error);
    record_text(&w,",\"stage_us\":{");
    for(unsigned n=0;n<PREPARATION_STAGE_COUNT;n++){
        if(n)record_text(&w,",");
        record_string(&w,timed_stages[n]);record_text(&w,":");record_number(&w,r->stage_us[n]);
    }
    record_text(&w,"}}}");record_flush(&w);
    send_status(a->send,a->transport,id,STATUS_OK,(int32_t)r->id,"scene operation state");
}
static PreparationRecord *lookup(App *a,uint32_t id){
    PreparationQueue *q=a->preparation;if(!q)return NULL;
    for(PreparationOperation *o=q->head;o;o=o->next)if(o->record.id==id)return &o->record;
    for(unsigned n=0;n<PREPARATION_HISTORY;n++)if(q->history[n].id==id)return &q->history[n];
    return NULL;
}
bool preparation_source_result(App *a,uint64_t source,uint32_t *last,const char **error){
    if(!a->preparation)return false;
    PreparationRecord *latest=NULL;
    for(unsigned n=0;n<PREPARATION_HISTORY;n++){
        PreparationRecord *r=&a->preparation->history[n];
        if(r->source==source&&r->id>*last&&(!latest||r->id>latest->id))latest=r;
    }
    if(!latest)return false;
    *last=latest->id;*error=latest->error;return true;
}
bool preparation_result(App *a,uint32_t id,uint32_t *code,int32_t *value,const char **error){
    PreparationRecord *r=lookup(a,id);if(!r||!r->terminal)return false;
    *code=r->code;*value=(int32_t)r->value;*error=r->error[0]?r->error:r->stage;return true;
}
void preparation_reply(App *a,uint16_t id,uint32_t operation){
    PreparationRecord *r=lookup(a,operation);if(r)record_reply(a,id,r);
}
bool preparation_pending(const App *a){
    if(a->preparation&&a->preparation->head)return true;
    for(unsigned n=0;n<a->output_count;n++)if(a->outputs[n]->startup_pending)return true;
    return false;
}
bool preparation_has_room(const App *a){return !a->preparation||a->preparation->count<PREPARATION_PENDING_LIMIT;}
bool preparation_manage(App *a,uint16_t id,const Json *j,const char *op){
    if(strcmp(op,"operation_get")&&strcmp(op,"operation_cancel"))return false;
    static const char *const fields[]={"op","boot_id","id",NULL};uint32_t key;
    if(!json_fields(j,0,fields)||!json_equal(j,json_get(j,0,"boot_id"),a->boot_id)||!json_u32(j,json_get(j,0,"id"),&key)){
        send_status(a->send,a->transport,id,STATUS_CONFIG,0,"operation requires this boot_id and id");return true;
    }
    PreparationRecord *r=lookup(a,key);
    if(!r){send_status(a->send,a->transport,id,STATUS_CONFIG,0,"operation unknown or expired");return true;}
    if(!strcmp(op,"operation_cancel"))for(PreparationOperation *o=a->preparation->head;o;o=o->next)if(o->record.id==key)o->cancelled=true;
    record_reply(a,id,r);return true;
}
void preparation_cancel_output(App *a,const char *output){
    for(unsigned n=0;n<a->output_count;n++)if(!output||!strcmp(a->outputs[n]->hardware.id,output))a->outputs[n]->startup_pending=false;
    if(a->preparation)for(PreparationOperation *o=a->preparation->head;o;o=o->next)
        if(!output||!strcmp(o->record.output,output))o->cancelled=true;
}
const char *preparation_submit(App *a,AppOutput *d,const PreparationRequest *request,const void *data,size_t size,uint32_t *id){
    if(size>CONFIG_LIMIT)return "scene request exceeds document limit";
    bool configuration=request->kind>=PREPARE_CONFIG_VALIDATE;
    if(a->preparation&&a->preparation->head&&(a->preparation->configuration||a->preparation->head->scene.request.kind>=PREPARE_CONFIG_VALIDATE)&&!configuration)return "configuration operation is in progress";
    if(configuration)preparation_abort_all(a);
    PreparationQueue *q=a->preparation;
    if(!q){q=demo_calloc(1,sizeof(*q));if(!q)return "operation queue exceeds memory budget";a->preparation=q;}
    bool replaces_pending=false;
    for(PreparationOperation *p=q->head?q->head->next:NULL;p;p=p->next)if(!strcmp(p->record.output,d->hardware.id))replaces_pending=true;
    if(q->count==PREPARATION_PENDING_LIMIT&&!replaces_pending)return "scene operation queue is full";
    if(q->sequence==UINT32_MAX)return "scene operation identifiers exhausted";
    PreparationOperation *o=demo_calloc(1,sizeof(*o));if(!o)return "scene request exceeds memory budget";
    o->pin=-1;o->scene.request=*request;
    if(request->entry.size){o->pin=store_pin(&a->store,&request->entry);if(o->pin<0){demo_free(o);return "package reference limit exceeded";}}
    if(request->reset_from&&request->reset_from->portable){
        Portable *p=request->reset_from->portable;
        o->scene.sources=p->source_scope;input_scope_retain(o->scene.sources);
        o->reset_input_count=p->ninputs;
        if(p->ninputs){o->reset_inputs=demo_alloc(p->ninputs*sizeof(*p->inputs));if(!o->reset_inputs){store_unpin(&a->store,o->pin);input_scope_release(o->scene.sources);demo_free(o);return "reset inputs exceed memory budget";}memcpy(o->reset_inputs,p->inputs,p->ninputs*sizeof(*p->inputs));}
        for(unsigned n=0;n<p->nsettings;n++){
            if(p->initialized){o->reset_settings[n]=pbc_value(p,pbc_setting(p,n).reg);o->reset_mask|=1u<<n;}
            else if(p->override_mask&(1u<<n)){o->reset_settings[n]=p->overrides[n];o->reset_mask|=1u<<n;}
        }
    }
    o->scene.request.reset_from=NULL;
    if(size){o->document=demo_alloc(size);if(!o->document){store_unpin(&a->store,o->pin);demo_free(o->reset_inputs);input_scope_release(o->scene.sources);demo_free(o);return "scene document exceeds memory budget";}memcpy(o->document,data,size);o->size=size;o->loaded=size;}
    /* Only the head can own a candidate. Replace queued requests immediately;
       an active candidate is cancelled at its next safe boundary. */
    for(PreparationOperation *previous=q->head;previous&&previous->next;){
        PreparationOperation *pending=previous->next;
        if(strcmp(pending->record.output,d->hardware.id)){previous=pending;continue;}
        previous->next=pending->next;if(q->tail==pending)q->tail=previous;q->count--;
        pending->record.stage="superseded";pending->record.code=STATUS_CONFIG;pending->record.terminal=true;pending->record.finished=platform_time_us();
        q->history[q->history_cursor++%PREPARATION_HISTORY]=pending->record;
        store_unpin(&a->store,pending->pin);demo_free(pending->document);demo_free(pending->reset_inputs);input_scope_release(pending->scene.sources);demo_free(pending);
    }
    if(q->head&&!strcmp(q->head->record.output,d->hardware.id)){q->head->cancelled=true;q->head->record.stage="superseded";}
    if(!request->boot)d->startup_pending=false;
    o->record.source=a->source;
    o->record.id=++q->sequence;o->record.stage="accepted";o->record.accepted=platform_time_us();strcpy(o->record.output,d->hardware.id);
    if(q->tail)q->tail->next=o;else q->head=o;q->tail=o;q->count++;*id=o->record.id;return NULL;
}
static const char *bind_program(App *a,AppOutput *d,VmInstance *vm,const StoreEntry *entry,PixelBuffer *target,bool direct){
    if(!entry)return "program does not exist";
    vm_init(vm,target,NULL,NULL);vm->output=direct?&d->pixels:NULL;
    if(d->layer_generation<d->vm.generation)d->layer_generation=d->vm.generation;
    if(d->layer_generation>=UINT32_MAX-1)return "program generation exhausted";
    vm->generation=++d->layer_generation;strcpy(vm->output_id,d->hardware.id);
    return vm_bind_storage(vm,&a->store,entry)?NULL:"package reference limit exceeded";
}
static const char *decode_scene(App *a,AppOutput *d,PreparationOperation *o){
    PreparationQueue *q=a->preparation;PreparedScene *s=&o->scene;Json *j=q->json;
    PreparationKind kind=s->request.kind;
    if(!s->sources){
        const char *error=input_scope_parse(&a->audio,j,j?json_get(j,0,"sources"):-1,&s->sources);if(error)return error;
        if(!s->sources&&kind==PREPARE_LAYER){
            Composition *existing=d->transition?d->transition->compositions[s->request.slot]:d->composition;
            if(existing){s->sources=existing->sources;input_scope_retain(s->sources);}
        }
    }
    if(s->sources&&!s->source_demand&&kind!=PREPARE_PRESET_SAVE&&!q->configuration){
        const char *error=input_scope_demand(s->sources,true);if(error)return error;s->source_demand=true;
    }
    if(kind==PREPARE_TRANSITION){
        for(unsigned n=0;n<2;n++){
            int token=s->request.effect==TRANSITION_PROGRAM?(n?-1:0):json_get(j,0,n?"paint":"mask");
            if(token<0)continue;
            TransitionProgram **slot=n?&s->paint:&s->transition;
            *slot=demo_calloc(1,sizeof(**slot));if(!*slot)return "transition exceeds memory budget";
            TransitionProgram *program=*slot;
            static const char *const fields[]={"program","settings","inputs",NULL};
            if((s->request.effect!=TRANSITION_PROGRAM&&!json_fields(j,token,fields))||!json_string(j,json_get(j,token,"program"),program->name,sizeof(program->name))||!object_name_valid(program->name))return "invalid transition program";
            const StoreEntry *entry=store_find(&a->store,s->request.effect==TRANSITION_PROGRAM?STORE_TRANSITION:STORE_ANIMATION,program->name);
            const char *error=bind_program(a,d,&program->vm,entry,&d->pixels,false);if(error)return error;
            q->vms[q->total]=&program->vm;q->tokens[q->total++]=token;
        }
        return NULL;
    }
    if(kind==PREPARE_DIRECT||kind==PREPARE_SLOT||kind==PREPARE_RESET||kind==PREPARE_LAYER){
        VmInstance *vm;
        if(kind!=PREPARE_SLOT){s->direct=demo_calloc(1,sizeof(*s->direct));vm=s->direct;}
        else{
            s->composition=composition_create(&d->pixels,1);if(!s->composition)return "composition exceeds memory budget";
            CompositionLayer *layer=&s->composition->layers[0];strcpy(layer->id,"main");strcpy(layer->program,s->request.name);layer->crc=s->request.entry.crc;vm=&layer->vm;
        }
        if(!vm)return "candidate exceeds memory budget";
        bool direct=kind==PREPARE_DIRECT||(kind==PREPARE_RESET&&!s->request.layer[0]);
        s->target.count=d->pixels.count;
        PixelBuffer *target=kind==PREPARE_SLOT?&s->composition->layers[0].target:direct?d->vm.target:&s->target;
        const char *error=bind_program(a,d,vm,&s->request.entry,target,direct);
        if(error)return error;
        if(kind==PREPARE_SLOT)strcpy(vm->layer,"main");
        if(direct&&s->request.play&&!target)return "slot has no configured output";
        q->vms[0]=vm;q->tokens[0]=0;q->total=1;return NULL;
    }
    if(kind==PREPARE_RECALL&&!s->request.on&&!s->request.name[0])return NULL;
    static const char *const fields[]={"schema","layers","entry_transition","sources",NULL};
    bool preset=kind==PREPARE_PRESET||kind==PREPARE_RECALL||kind==PREPARE_PRESET_SAVE;
    if(!j||!json_fields(j,0,fields)||!json_equal(j,json_get(j,0,"schema"),preset?"lightvm.preset":"lightvm.composition"))return "invalid scene document";
    int layers=json_get(j,0,"layers");
    if(layers<0||j->tokens[layers].type!=JSON_ARRAY||(unsigned)j->tokens[layers].children>COMPOSITION_LAYER_LIMIT||(preset&&!j->tokens[layers].children))return "invalid scene layers";
    unsigned count=(unsigned)j->tokens[layers].children;
    if(kind==PREPARE_RECALL&&!s->request.on)count=0;
    if(count||kind!=PREPARE_RECALL){s->composition=composition_create(&d->pixels,count);if(!s->composition)return "composition exceeds memory budget";}
    if(s->composition){s->composition->sources=s->sources;input_scope_retain(s->sources);}
    unsigned n=0;
    for(int token=layers+1;n<count;token=j->tokens[token].next,n++){
        static const char *const layer_fields[]={"id","program","crc32","settings","inputs","opacity","blend",NULL};
        CompositionLayer *layer=&s->composition->layers[n];uint32_t opacity=255,crc;
        if(!json_fields(j,token,layer_fields)||!json_string(j,json_get(j,token,"id"),layer->id,sizeof(layer->id))||!object_name_valid(layer->id)||
           !json_string(j,json_get(j,token,"program"),layer->program,sizeof(layer->program))||!object_name_valid(layer->program))return "invalid scene layer";
        for(unsigned k=0;k<n;k++)if(!strcmp(layer->id,s->composition->layers[k].id))return "duplicate layer id";
        int alpha=json_get(j,token,"opacity"),blend=json_get(j,token,"blend"),fingerprint=json_get(j,token,"crc32");
        if(alpha>=0&&(!json_u32(j,alpha,&opacity)||opacity>255))return "layer opacity must be 0..255";
        layer->opacity=(uint8_t)opacity;
        if(blend>=0){bool found=false;for(unsigned b=0;b<4;b++)if(json_equal(j,blend,draw_blend_name((DrawBlend)b))){layer->blend=(uint8_t)b;found=true;break;}if(!found)return "invalid layer blend";}
        const StoreEntry *entry=store_find(&a->store,STORE_ANIMATION,layer->program);
        if(!entry)return "layer program does not exist";
        if((preset||fingerprint>=0)&&(!json_u32(j,fingerprint,&crc)||crc!=entry->crc))return "layer program version differs from saved reference";
        layer->crc=entry->crc;const char *error=bind_program(a,d,&layer->vm,entry,&layer->target,false);if(error)return error;
        strcpy(layer->vm.layer,layer->id);q->vms[n]=&layer->vm;q->tokens[n]=token;
    }
    q->total=count;
    if(preset&&s->composition){strcpy(s->composition->preset,s->request.name);s->composition->preset_crc=s->request.entry.crc;}
    int transition=json_get(j,0,"entry_transition");
    if(!preset&&transition>=0)return "composition has no entry transition";
    if(preset){char message[160];if(!transition_spec_parse(j,transition,&s->transition_spec,message,sizeof(message)))return "invalid preset entry transition";}
    if((kind==PREPARE_RECALL||kind==PREPARE_PRESET_SAVE)&&s->transition_spec.program[0]){
        const StoreEntry *entry=store_find(&a->store,STORE_TRANSITION,s->transition_spec.program);
        if(!entry||entry->crc!=s->transition_spec.crc)return "transition version differs from saved reference";
        s->transition=demo_calloc(1,sizeof(*s->transition));if(!s->transition)return "transition exceeds memory budget";
        const char *error=bind_program(a,d,&s->transition->vm,entry,&d->pixels,false);if(error)return error;
        strcpy(s->transition->name,s->transition_spec.program);q->vms[q->total]=&s->transition->vm;q->tokens[q->total++]=transition;
    }
    return NULL;
}
static const char *step(App *a,AppOutput *d,PreparationOperation *o,bool *done){
    PreparationQueue *q=a->preparation;PreparedScene *s=&o->scene;const char *error=NULL;
    if(q->phase==100){
        while(q->config_index<q->configuration->count){
            HardwareOutput *hardware=&q->configuration->outputs[q->config_index++];
            if(hardware->startup_mode==STARTUP_RESTORE_LAST&&!store_info(&a->store).writable)return "restore_last requires writable storage";
            if(hardware->startup_mode!=STARTUP_PRESET)return NULL;
            const StoreEntry *entry=store_find(&a->store,STORE_PRESET,hardware->startup_preset);if(!entry)return "startup preset does not exist";
            q->validation_output=demo_calloc(1,sizeof(*q->validation_output));if(!q->validation_output)return "startup validation exceeds memory budget";
            q->validation_output->hardware=*hardware;q->validation_output->pixels.count=hardware->pixels.count;
            q->validation_output->pixels.light=(LightControls){.color=0xffffff,.brightness=1};
            s->request=(PreparationRequest){.kind=PREPARE_RECALL,.on=true,.entry=*entry,.light=q->validation_output->pixels.light};
            strcpy(s->request.name,hardware->startup_preset);
            o->pin=store_pin(&a->store,entry);if(o->pin<0)return "startup preset reference limit exceeded";
            q->phase=q->index=q->total=q->capture_index=0;o->size=o->loaded=0;return NULL;
        }
        int persisted=app_preparation_config_persist(a,&o->config_request,o->config_document,o->config_size,&q->write,&q->persist_index,q->persist_data,&error);
        if(persisted<0)return error;
        if(!persisted){o->record.stage="persist";return NULL;}
        error=app_preparation_config_commit(a,&o->config_request,q->configuration,o->config_document,o->config_size);
        if(error)return error;
        o->record.value=o->config_request.kind==PREPARE_CONFIG_SAVE?a->store.config_revision:a->revision;
        *done=true;return NULL;
    }
    switch(q->phase){
    case 0:
        o->record.stage="load";
        if(!o->document&&s->request.entry.type==STORE_PRESET){o->size=s->request.entry.size;o->document=demo_alloc(o->size);if(!o->document)return "preset document exceeds memory budget";}
        q->phase=1;return NULL;
    case 1:
        if(o->loaded<o->size){size_t n=o->size-o->loaded;if(n>256)n=256;if(!store_read(&a->store,&s->request.entry,(uint32_t)o->loaded,o->document+o->loaded,n))return "preset read failed";o->loaded+=n;return NULL;}
        if(o->size){q->json=demo_calloc(1,sizeof(Json));if(!q->json)return "scene parser exceeds memory budget";json_begin(&q->parser,q->json,o->document,o->size);}
        q->phase=2;o->record.stage="decode";return NULL;
    case 2:
        if(q->json){int status=json_step(&q->parser,256);if(status<0)return "invalid scene JSON";if(!status)return NULL;}
        q->phase=3;return NULL;
    case 3:
        if(s->request.kind>=PREPARE_CONFIG_VALIDATE){
            q->configuration=demo_calloc(1,sizeof(*q->configuration));if(!q->configuration)return "configuration exceeds memory budget";
            if(!hardware_decode(q->json,q->configuration,o->record.error,sizeof(o->record.error)))return o->record.error;
            demo_free(q->json);q->json=NULL;o->config_document=o->document;o->config_size=o->size;o->document=NULL;o->size=o->loaded=0;
            o->config_request=s->request;q->config_index=0;q->phase=100;return NULL;
        }
        error=decode_scene(a,d,o);if(error)return error;
        q->phase=4;q->index=0;o->record.stage="verify";return NULL;
    case 4:
        if(q->index<q->total){
            VmInstance *vm=q->vms[q->index];
            int status=pbc_open_step(&q->loader,&vm->portable,vm_read,vm,vm->expected);
            if(status<0)return q->loader.error;
            if(!status)return NULL;
            error=animation_validate(vm->portable);if(error)return error;
            bool transition=(s->transition&&vm==&s->transition->vm)||(s->paint&&vm==&s->paint->vm);
            bool named=transition&&(s->request.kind!=PREPARE_TRANSITION||s->request.effect==TRANSITION_PROGRAM);
            if(vm->portable->profile!=(named?4u:3u))return "program profile does not match scene role";
            if(transition){vm->portable->deterministic_random=true;vm->portable->default_seed=s->request.kind==PREPARE_TRANSITION?s->request.seed:s->transition_spec.seed;}
            control_loaded(vm);
            if(s->request.kind==PREPARE_RESET){
                Portable *p=vm->portable;memcpy(p->overrides,o->reset_settings,sizeof(p->overrides));p->override_mask=o->reset_mask;
                if(o->reset_input_count!=p->ninputs)return "reset input signature differs";
                if(p->ninputs)memcpy(p->inputs,o->reset_inputs,p->ninputs*sizeof(*p->inputs));
                for(unsigned n=0;n<p->ninputs;n++){p->inputs[n].driver.started=0;p->inputs[n].driver.phase=0;p->inputs[n].has_last=false;}
                demo_free(o->reset_inputs);o->reset_inputs=NULL;
            }
            inputs_attach(vm->portable,&a->inputs,&s->request.light);
            inputs_scope(vm->portable,s->sources);
            if(s->source_demand){error=inputs_demand(vm->portable,true);if(error)return error;}
            if(s->composition&&!s->composition->sources){s->composition->sources=s->sources;input_scope_retain(s->sources);}
            if(q->json){int token=q->tokens[q->index];error=control_settings(vm,q->json,json_get(q->json,token,"settings"));if(!error)error=inputs_bind(vm->portable,q->json,json_get(q->json,token,"inputs"),0);if(error)return error;}
            vm->ready=true;q->index++;memset(&q->loader,0,sizeof(q->loader));return NULL;
        }
        /* Parsing storage is released before any guest initialization executes. */
        demo_free(q->json);q->json=NULL;
        if(s->request.kind==PREPARE_PRESET_SAVE){s->document=o->document;s->document_size=o->size;}
        else{demo_free(o->document);o->document=NULL;}
        q->phase=5;q->index=0;o->record.stage="admit";return NULL;
    case 5:
        if(!s->request.light_attached||d->light_revision!=s->request.light_revision)s->request.light=d->pixels.light;
        if(q->index<q->total){
            VmInstance *vm=q->vms[q->index];
            if(!vm->target){q->index++;return NULL;}
            int status=control_prepare_step(&q->animation,vm);
            o->record.stage=q->animation.stage>=3?"first_render":q->animation.stage?"initialise":"admit";
            if(status<0)return q->animation.error;
            if(!status)return NULL;
            vm->playing=s->request.play;vm->last_frame=platform_time_us();vm->next_frame=vm->last_frame;
            q->index++;memset(&q->animation,0,sizeof(q->animation));return NULL;
        }
        q->phase=6;o->record.stage="compose";return NULL;
    case 6:
        if(q->capture_index<2){
            TransitionProgram *program=q->capture_index?s->paint:s->transition;
            if(program){int status=transition_program_capture_step(program,&q->baseline_offset);if(status<0)return "transition baseline exceeds memory budget";if(!status)return NULL;}
            q->capture_index++;q->baseline_offset=0;return NULL;
        }
        if(s->composition&&!composition_prepare_step(s->composition))return NULL;
        q->phase=7;o->record.stage="ready";return NULL;
    case 7:{
        LightControls latest=s->request.light_attached&&d->light_revision==s->request.light_revision?s->request.light:d->pixels.light;
        s->request.light=latest;bool changed=false;
        for(unsigned n=0;n<q->total;n++)if(q->vms[n]->control.initialized&&q->vms[n]->portable->profile==3&&inputs_changed(q->vms[n]->portable,0))changed=true;
        if(changed&&!q->refreshed){q->refreshed=true;q->index=0;q->phase=8;o->record.stage="refresh";return NULL;
        }
        if(q->configuration){
            composition_destroy(s->composition);transition_program_destroy(s->transition);s->composition=NULL;s->transition=NULL;
            if(s->source_demand)input_scope_demand(s->sources,false);
            input_scope_release(s->sources);s->sources=NULL;s->source_demand=false;
            store_unpin(&a->store,o->pin);o->pin=-1;
            demo_free(q->validation_output);q->validation_output=NULL;
            s->request=o->config_request;q->phase=100;memset(&q->animation,0,sizeof(q->animation));q->baseline_offset=0;return NULL;
        }
        error=app_preparation_admit(a,d,s);if(error)return error;
        q->persist_light_revision=d->light_revision;q->phase=9;o->record.stage="persist";return NULL;
    }
    case 9:{
        if(d->composition_revision!=s->request.revision)return "scene changed during preparation";
        int status=app_preparation_persist(a,d,s,&q->write,&q->persist_index,q->persist_data,&error);
        if(status<0)return error;
        if(!status)return NULL;
        bool newer_controls=q->persist_light_revision!=d->light_revision;
        error=app_preparation_commit(a,d,s);if(error)return error;
        if(newer_controls&&s->request.kind==PREPARE_RECALL)d->light_save_due=platform_time_us()+500000u;
        o->record.scene_activated=s->request.kind!=PREPARE_PRESET_SAVE;
        if(o->record.scene_activated)o->record.activated=platform_time_us();
        o->record.value=s->request.kind==PREPARE_DIRECT?d->vm.control.count:s->request.kind==PREPARE_PRESET_SAVE?a->store.generation:d->composition_revision;
        o->record.target_revision=d->composition_revision;o->record.recalled=s->request.kind==PREPARE_RECALL;
        o->record.presentation_before=d->transition?d->transition->frames+(d->transition->pending?1u:0u):d->composition?d->composition->frames+(d->composition->pending?1u:0u):d->vm.control.output_frame;
        if(s->request.kind==PREPARE_DIRECT||(s->request.kind==PREPARE_RESET&&!s->request.layer[0]))o->record.target_generation=d->vm.generation;
        else if(s->request.kind!=PREPARE_TRANSITION){
            unsigned slot=o->record.recalled?1:s->request.slot;
            Composition *target=d->transition?d->transition->compositions[slot]:d->composition;
            if(target)for(unsigned n=0;n<target->layer_count;n++)if(target->layers[n].opacity){
                if(s->request.layer[0]&&strcmp(s->request.layer,target->layers[n].id))continue;
                o->record.target_generation=target->layers[n].vm.generation;break;
            }
        }
        *done=true;return NULL;
    }
    case 8:
        if(q->index<q->total){
            VmInstance *vm=q->vms[q->index];
            if(!vm->control.initialized||vm->portable->profile==4){q->index++;return NULL;}
            bool ready=false;error=animation_poll_budget(vm,0,&ready,256);if(error)return error;
            if(ready)q->index++;
            return NULL;
        }
        if(s->composition)composition_invalidate(s->composition);
        q->phase=6;return NULL;
    default:return "invalid preparation phase";
    }
}
static void finish(App *a,PreparationOperation *o,const char *error,bool cancelled){
    PreparationQueue *q=a->preparation;
    uint64_t began=platform_time_us();store_write_cancel(&q->write);
    pbc_open_cancel(&q->loader);demo_free(q->json);q->json=NULL;
    if(o->scene.direct){control_destroy(o->scene.direct);demo_free(o->scene.direct);o->scene.direct=NULL;}
    composition_destroy(o->scene.wrapper);transition_destroy(o->scene.presenter);
    composition_destroy(o->scene.composition);transition_program_destroy(o->scene.transition);transition_program_destroy(o->scene.paint);
    audio_source_plan_release(&o->scene.retained_sources);
    if(o->scene.source_demand)input_scope_demand(o->scene.sources,false);
    input_scope_release(o->scene.sources);o->scene.sources=NULL;
    o->record.terminal=true;
    if(error){o->record.code=q->configuration||o->scene.request.kind>=PREPARE_CONFIG_VALIDATE?STATUS_CONFIG:STATUS_LOAD;if(error!=o->record.error)snprintf(o->record.error,sizeof(o->record.error),"%s",error);o->record.stage="failed";}
    else if(cancelled){o->record.code=STATUS_CONFIG;if(strcmp(o->record.stage,"superseded"))o->record.stage="cancelled";}
    else o->record.stage="complete";
    if(q->configuration){hardware_destroy(q->configuration);demo_free(q->configuration);q->configuration=NULL;}
    demo_free(q->validation_output);q->validation_output=NULL;demo_free(o->config_document);o->config_document=NULL;
    q->head=o->next;if(!q->head)q->tail=NULL;q->count--;
    q->phase=q->index=q->total=q->capture_index=q->persist_index=0;q->refreshed=false;q->baseline_offset=0;memset(&q->loader,0,sizeof(q->loader));memset(&q->animation,0,sizeof(q->animation));
    store_unpin(&a->store,o->pin);demo_free(o->reset_inputs);demo_free(o->document);
    o->record.finished=platform_time_us();
    uint64_t cleanup=o->record.finished-began;if(cleanup>o->record.max_step)o->record.max_step=cleanup;
    o->record.stage_us[PREPARATION_STAGE_COUNT-1]+=cleanup;
    q->history[q->history_cursor++%PREPARATION_HISTORY]=o->record;demo_free(o);
}
static void milestones(App *a,PreparationRecord *r){
    if(!r->terminal||r->code||!r->id||!r->scene_activated)return;
    AppOutput *d=app_output(a,r->output);if(!d||d->composition_revision!=r->target_revision)return;
    uint64_t now=platform_time_us();bool presented=false;
    if(r->target_generation){
        if(!d->composition&&!d->transition)presented=d->vm.generation==r->target_generation&&d->vm.control.displayed.frame;
        else for(unsigned slot=0;slot<2;slot++){
            Composition *scene=d->transition?d->transition->compositions[slot]:slot?NULL:d->composition;
            if(scene)for(unsigned n=0;n<scene->layer_count;n++)if(scene->layers[n].vm.generation==r->target_generation)
                presented=scene->layers[n].vm.control.displayed.frame!=0;
        }
    }else presented=(d->transition?d->transition->frames:d->composition?d->composition->frames:0)>r->presentation_before;
    if(r->recalled&&d->transition&&!r->fade_completed&&d->transition->driver==TRANSITION_MANUAL&&d->transition->progress==100)r->fade_completed=now;
    if(presented&&!r->first_presented)r->first_presented=now;
}
void preparation_tick(App *a){
    PreparationQueue *q=a->preparation;if(!q)return;
    for(unsigned n=0;n<PREPARATION_HISTORY;n++)milestones(a,&q->history[n]);
    if(!q->head)return;
    uint64_t tick_started=platform_time_us();
    /* Coalesce cheap decode/zeroing/verification units. Guest resumes still
       enforce their quantum, and the count cap also bounds frozen-clock hosts.
       Return to ingress after at most 4 ms plus one indivisible unit. */
    for(unsigned unit=0;unit<32;unit++){
        PreparationOperation *o=q->head;AppOutput *d=q->validation_output?q->validation_output:app_output(a,o->record.output);
        if(o->cancelled||!d){finish(a,o,NULL,true);return;}
        if(!q->configuration&&o->scene.request.kind<PREPARE_CONFIG_VALIDATE&&d->composition_revision!=o->scene.request.revision){
            finish(a,o,"scene changed during preparation",false);return;
        }
        bool done=false;uint64_t began=platform_time_us();const char *stage=o->record.stage;
        /* Preparation never recursively enters the renderer/transport service hook. */
        runtime_defer_begin();const char *error=step(a,d,o,&done);runtime_defer_end();
        uint64_t elapsed=platform_time_us()-began;if(elapsed>o->record.max_step)o->record.max_step=elapsed;o->record.units++;
        for(unsigned n=0;n<PREPARATION_STAGE_COUNT;n++)if(!strcmp(stage,timed_stages[n])){o->record.stage_us[n]+=elapsed;break;}
        if(error&&o->record.error!=error)snprintf(o->record.error,sizeof(o->record.error),"%s",error);
        if(error&&q->configuration){finish(a,o,o->record.error,false);return;}
        if(error){snprintf(d->light_error,sizeof(d->light_error),"%.95s",error);if(o->scene.request.boot)snprintf(a->boot_error,sizeof(a->boot_error),"outputs[%s].startup: %.90s",d->hardware.id,error);}
        if(error||done){finish(a,o,error,false);return;}
        if(platform_time_us()-tick_started>=4000u)return;
    }
}
size_t preparation_retained_bytes(const App *a){return demo_allocation_bytes(a->preparation);}
void preparation_abort_all(App *a){
    for(unsigned n=0;n<a->output_count;n++)a->outputs[n]->startup_pending=false;
    if(a->preparation)while(a->preparation->head)finish(a,a->preparation->head,NULL,true);
}
void preparation_destroy(App *a){
    if(!a->preparation)return;
    while(a->preparation->head)finish(a,a->preparation->head,NULL,true);
    demo_free(a->preparation);a->preparation=NULL;
}
