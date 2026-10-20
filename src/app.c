#include "app.h"
#include "release.h"
#include "runtime.h"
#include "json.h"
#include "vm_allocator.h"
#include "draw.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <math.h>

typedef struct { App *app; uint16_t id; uint8_t bytes[FRAME_LIMIT]; size_t used; } Writer;
static void flush(Writer *w) {
    if (w->used && !w->app->booting) w->app->send(w->app->transport, RESP_DATA, w->id, w->bytes, w->used);
    w->used = 0;
}
static void bytes(Writer *w, const void *data, size_t size) {
    const uint8_t *p = data;
    while (size) {
        size_t n = FRAME_LIMIT - w->used;
        if (n > size) n = size;
        memcpy(w->bytes + w->used, p, n); w->used += n; p += n; size -= n;
        if (w->used == FRAME_LIMIT) flush(w);
    }
}
static void literal(Writer *w, const char *s) { bytes(w, s, strlen(s)); }
static void number(Writer *w, uint32_t n) { char s[16]; snprintf(s, sizeof(s), "%lu", (unsigned long)n); literal(w, s); }
static void wide(Writer *w,uint64_t n){char s[24];snprintf(s,sizeof(s),"%llu",(unsigned long long)n);literal(w,s);}
static void quoted(Writer *w, const char *s) {
    literal(w, "\"");
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c == '"' || c == '\\') { literal(w, "\\"); bytes(w, &c, 1); }
        else if (c < 32) { char escaped[7]; snprintf(escaped, sizeof(escaped), "\\u%04x", c); literal(w, escaped); }
        else bytes(w, &c, 1);
    }
    literal(w, "\"");
}
static void reply(App *a, uint16_t id, enum Status code, int32_t value, const char *message) {
    a->last_code=code;
    if (a->booting) { if (code) snprintf(a->boot_error,sizeof(a->boot_error),"%s",message); return; }
    send_status(a->send, a->transport, id, code, value, message);
}
static void error_reply(App *a, uint16_t id, enum Status code, const char *error, const char *message) {
    Writer w = {.app=a, .id=id};
    literal(&w, "{\"error\":"); quoted(&w, error); literal(&w, ",\"message\":"); quoted(&w, message); literal(&w, "}"); flush(&w);
    reply(a, id, code, 0, message);
}
static void vm_send(void *context, uint8_t type, uint16_t id, const uint8_t *p, size_t size) {
    App *a = context;
    if (type == RESP_STATUS && size >= 8) {
        a->last_code = read_u32(p);
        if (a->booting && a->last_code) snprintf(a->boot_error, sizeof(a->boot_error), "%.*s", (int)(size - 8), (const char *)p + 8);
    }
    if (!a->booting) a->send(a->transport, type, id, p, size);
}
AppOutput *app_output(App *a,const char *id){
    if(!id||!id[0])return a->hardware.default_output>=0&&a->output_count?a->outputs[a->hardware.default_output]:&a->fallback;
    for(unsigned i=0;i<a->output_count;i++)if(!strcmp(id,a->outputs[i]->hardware.id))return a->outputs[i];
    return NULL;
}
static const char *composition_preset(const App *a,const Composition *s){
    if(!s||!s->preset[0])return NULL;
    const StoreEntry *entry=store_find(&a->store,STORE_PRESET,s->preset);
    return entry&&entry->crc==s->preset_crc?s->preset:NULL;
}
const char *app_output_preset(const App *a,const AppOutput *d){
    const Composition *s=d->composition;
    if(d->transition){
        if(d->transition->progress==0)s=d->transition->compositions[0];
        else if(d->transition->progress==100)s=d->transition->compositions[1];
        else return NULL;
    }
    return composition_preset(a,s);
}
bool app_output_power(const AppOutput *d){
    /* Pausing a clock retains the light; a rendered black frame is still on. */
    return d->power_on;
}
static void output_command_power(AppOutput *d,uint8_t command){
    if(command==CMD_PLAY||(command==CMD_FRAME&&!d->recall_owned))d->power_on=true;
    else if(command==CMD_STOP||command==CMD_RESET)d->power_on=false;
}
const char *app_output_selected_preset(const App *a,const AppOutput *d){
    if(!d->recall_owned)return app_output_preset(a,d);
    const StoreEntry *entry=store_find(&a->store,STORE_PRESET,d->selected_preset);
    return entry&&entry->crc==d->selected_preset_crc?d->selected_preset:NULL;
}
static bool program_color_control(const VmInstance *vm){
    const Portable *p=vm->portable;
    if(p)for(unsigned n=0;n<p->ninputs;n++)
        if(p->inputs[n].driver.kind==INPUT_CONTROL&&p->inputs[n].driver.control==LIGHT_CONTROL_COLOR)return true;
    return false;
}
bool app_output_color_control(const AppOutput *d){
    if(!d->composition&&!d->transition)return program_color_control(&d->vm);
    for(unsigned slot=0;slot<2;slot++){
        if(d->transition&&((d->transition->progress==0&&slot==1)||(d->transition->progress==100&&slot==0)))continue;
        const Composition *s=slot?(d->transition?d->transition->compositions[1]:NULL):d->composition;
        if(s)for(unsigned n=0;n<s->layer_count;n++)if(s->layers[n].opacity&&!s->layers[n].vm.control.faulted&&program_color_control(&s->layers[n].vm))return true;
    }
    return false;
}
size_t app_output_light_json(const App *a,const AppOutput *d,char *buffer,size_t capacity,bool mqtt){
    const char *preset=app_output_selected_preset(a,d);
    if(mqtt&&preset){
        bool exposed=false;for(unsigned n=0;n<d->hardware.ha_preset_count;n++)if(!strcmp(preset,d->hardware.ha_presets[n]))exposed=true;
        if(!exposed)preset=NULL;
    }
    uint32_t color=d->pixels.light.color;
    int n;
    if(mqtt)n=snprintf(buffer,capacity,
        "{\"state\":\"%s\",\"brightness\":%u,\"color_mode\":\"%s\",\"color\":{\"r\":%u,\"g\":%u,\"b\":%u},\"effect\":\"%s\"}",
        app_output_power(d)?"ON":"OFF",(unsigned)lroundf(d->pixels.light.brightness*255),
        app_output_color_control(d)?"rgb":"brightness",(unsigned)((color>>16)&255),(unsigned)((color>>8)&255),(unsigned)(color&255),preset?preset:"Custom");
    else n=snprintf(buffer,capacity,
        "{\"revision\":%lu,\"composition_revision\":%lu,\"power\":%s,\"brightness\":%.9g,\"color\":%u,\"color_control\":%s,\"preset\":%s%s%s}",
        (unsigned long)d->light_revision,(unsigned long)d->composition_revision,app_output_power(d)?"true":"false",
        (double)d->pixels.light.brightness,(unsigned)color,app_output_color_control(d)?"true":"false",preset?"\"":"",preset?preset:"null",preset?"\"":"");
    return n>=0&&(size_t)n<capacity?(size_t)n:0;
}
static void composition_modified(Composition *s){if(s){s->preset[0]=0;s->preset_crc=0;}}
static void composition_state_for(Writer *w,const AppOutput *d,const Composition *s){
    literal(w,"{\"revision\":");number(w,d->composition_revision);
    literal(w,",\"active\":");literal(w,s?"true":"false");
    const char *preset=composition_preset(w->app,s);
    literal(w,",\"preset\":");if(preset)quoted(w,preset);else literal(w,"null");
    if(s){
        literal(w,",\"playing\":");literal(w,s->playing?"true":"false");
        literal(w,",\"frames\":");number(w,s->frames);
        literal(w,",\"composite_frames\":");number(w,s->composite_frames);
        literal(w,",\"composite_us\":");wide(w,s->composite_us);
        literal(w,",\"output_us\":");wide(w,s->output_us);
        literal(w,",\"error\":");quoted(w,s->error);
        literal(w,",\"layers\":[");
        for(unsigned i=0;i<s->layer_count;i++){
            const CompositionLayer *l=&s->layers[i];const VmInstance *vm=&l->vm;
            if(i)literal(w,",");
            literal(w,"{\"id\":");quoted(w,l->id);
            literal(w,",\"program\":");quoted(w,l->program);
            literal(w,",\"generation\":");number(w,vm->generation);
            literal(w,",\"opacity\":");number(w,l->opacity);
            literal(w,",\"blend\":");quoted(w,draw_blend_name((DrawBlend)l->blend));
            literal(w,",\"playing\":");literal(w,vm->playing?"true":"false");
            literal(w,",\"faulted\":");literal(w,vm->control.faulted?"true":"false");
            literal(w,",\"frames\":");number(w,vm->control.contribution_frame);
            literal(w,",\"render_us\":");number(w,vm->render_us);
            literal(w,",\"render_total_us\":");wide(w,vm->render_total_us);
            literal(w,",\"native_draw_us\":");wide(w,vm->portable?vm->portable->native_draw_us:0);
            literal(w,",\"error\":");quoted(w,vm->animation_error);
            literal(w,"}");
        }
        literal(w,"]");
    }
    literal(w,"}");
}
static void composition_state(Writer *w,const AppOutput *d){composition_state_for(w,d,d->composition);}
static unsigned request_slot(const Json *j){return json_equal(j,json_get(j,0,"slot"),"B")?1u:0u;}
static Composition *output_composition(AppOutput *d,unsigned slot){return slot?(d->transition?d->transition->compositions[1]:NULL):d->composition;}
static void clear_output_composition(AppOutput *d){
    Composition *old=d->composition;Transition *t=d->transition;d->composition=NULL;d->transition=NULL;
    d->recall_owned=false;d->selected_preset[0]=0;d->selected_preset_crc=0;
    transition_invalidate(t);
    if(t){composition_destroy(t->compositions[1]);composition_destroy(old);transition_destroy(t);}
    else composition_destroy(old);
}
static void destroy_output(AppOutput **slot){
    AppOutput *d=*slot;*slot=NULL;
    if(!d)return;
    if(d->indicator_pending)d->pixels.type->cancel(d->pixels.driver);
    clear_output_composition(d);vm_destroy(&d->vm);pixels_destroy(&d->pixels);platform_free(d->indicators);platform_free(d);
}
static void empty_config(App *a) {
    digital_inputs_destroy(&a->digital_inputs);
    hardware_destroy(&a->hardware);
    for(unsigned i=0;i<a->output_count;i++)destroy_output(&a->outputs[i]);
    platform_free(a->outputs);a->outputs=NULL;a->output_count=0;a->upload_output=&a->fallback;
    a->config_size=(uint32_t)strlen(EMPTY_CONFIG);memcpy(a->config_json,EMPTY_CONFIG,a->config_size);
}
static void startup_error(char *error,size_t capacity,const HardwareOutput *output,const char *failure) {
    snprintf(error,capacity,"outputs[%s].startup: %s",output->id,failure);
}
static const char *prepare_instance(App *a,VmInstance *candidate,const StoreEntry *entry,const Json *request,int args,int inputs,const LightControls *light) {
    const char *error=vm_bind_storage(candidate,&a->store,entry)?vm_load(candidate):"package reference limit exceeded";
    if(!error&&candidate->portable->profile!=3)error="composition playback requires an animation; select transitions by program name";
    if(!error)error=control_settings(candidate,request,args);
    if(!error)inputs_attach(candidate->portable,&a->inputs,light);
    if(!error)error=inputs_bind(candidate->portable,request,inputs,0);
    if(!error){candidate->ready=true;if(candidate->target)error=control_prepare(candidate);}
    return error;
}
static const char *prepare_recall_transition(App *,PixelBuffer *,const void *,const TransitionSpec *,TransitionProgram **);
static const char *validate_startup(App *,const HardwareOutput *);
static void start_output(App *,AppOutput *);
static bool config_validate(App *a, const void *data, uint32_t size, HardwareConfig *config, char *error, size_t capacity) {
    if(!hardware_parse(data,size,config,error,capacity))return false;
    /* Boot admits hardware first, then attempts each invocation independently. */
    for(unsigned i=0;!a->booting&&i<config->count;i++){
        HardwareOutput *output=&config->outputs[i];
        const char *failure=validate_startup(a,output);
        if(failure){startup_error(error,capacity,output,failure);hardware_destroy(config);return false;}
    }
    return true;
}
static bool same_output(const HardwareOutput *a,const HardwareOutput *b){
    return !strcmp(a->id,b->id)&&!strcmp(a->type,b->type)&&a->pixels.pin==b->pixels.pin&&a->pixels.count==b->pixels.count&&
        a->pixels.brightness==b->pixels.brightness&&!strcmp(a->pixels.order,b->pixels.order)&&a->indicators==b->indicators&&
        a->pixels.clock_pin==b->pixels.clock_pin&&a->pixels.enable_pin==b->pixels.enable_pin&&a->pixels.frequency==b->pixels.frequency;
}
static bool config_apply(App *a,HardwareConfig *config,const void *data,uint32_t size){
    AppOutput **next=platform_calloc(config->count,sizeof(*next));
    if(config->count&&!next){hardware_destroy(config);return false;}
    a->hardware_changing=true;
    digital_inputs_apply(&a->digital_inputs,config->inputs,config->input_count);
    /* Output identity, not array position, owns its running instance. */
    for(unsigned n=0;n<config->count;n++)for(unsigned i=0;i<a->output_count;i++)
        if(a->outputs[i]&&same_output(&a->outputs[i]->hardware,&config->outputs[n])){next[n]=a->outputs[i];a->outputs[i]=NULL;break;}
    for(unsigned i=0;i<a->output_count;i++)destroy_output(&a->outputs[i]);
    platform_free(a->outputs);a->outputs=next;a->output_count=config->count;
    hardware_destroy(&a->hardware);a->hardware=*config;memset(config,0,sizeof(*config));config->default_output=-1;
    ++a->revision;
    for(unsigned i=0;i<a->output_count;i++)if(!a->outputs[i]){
        AppOutput *d=platform_calloc(1,sizeof(*d));
        if(!d){ /* Make partial construction safe for cleanup. */
            for(unsigned n=i;n<a->output_count;n++)if(!a->outputs[n])a->outputs[n]=NULL;
            goto failed;
        }
        a->outputs[i]=d;d->hardware=a->hardware.outputs[i];vm_init(&d->vm,NULL,vm_send,a);
        strcpy(d->vm.output_id,d->hardware.id);
        if(!pixels_init(&d->pixels,&d->hardware.pixels,output_type(d->hardware.type)))goto failed;
        if(d->hardware.indicators){
            d->indicators=platform_calloc(d->pixels.count,sizeof(*d->indicators));if(!d->indicators)goto failed;
            for(unsigned n=0;n<d->pixels.count;n++){memset(d->indicators[n].rgb,255,3);d->indicators[n].brightness=255;}
        }
        d->vm.target=&d->pixels;d->vm.output=&d->pixels;
    }
    for(unsigned i=0;i<a->output_count;i++)a->outputs[i]->hardware=a->hardware.outputs[i];
    for(unsigned i=0;i<a->digital_inputs.count;i++){
        DigitalInput *input=&a->digital_inputs.devices[i];uint8_t mask[2];input->press_feedback=0;
        const StoreEntry *entry=store_find(&a->store,STORE_KEY_FEEDBACK,input->config.id);
        if(entry){
            if(entry->size!=sizeof(mask)||!store_read(&a->store,entry,0,mask,sizeof(mask)))goto failed;
            input->press_feedback=(uint16_t)(mask[0]|(mask[1]<<8));
        }
    }
    memmove(a->config_json,data,size);a->config_size=size;a->upload_output=&a->fallback;a->hardware_changing=false;return true;
failed:
    for(unsigned i=0;i<a->output_count;i++)destroy_output(&a->outputs[i]);
    platform_free(a->outputs);a->outputs=NULL;a->output_count=0;empty_config(a);a->hardware_changing=false;return false;
}
/* Package extents are immutable and pinned independently of catalogue names. */
static bool update_store(App *a,StoreType type,const char *name,const void *data,uint32_t size,bool remove) {
    return remove?store_delete(&a->store,type,name):store_put(&a->store,type,name,data,size);
}
static const char *install(App *a,AppOutput *d,const StoreEntry *entry,const char *name,bool play,const Json *request,int settings) {
    if(d->vm.generation>=UINT32_MAX-1){
        return "program generation exhausted";
    }
    VmInstance *candidate=demo_calloc(1,sizeof(*candidate));
    if(!candidate){
        return "replacement exceeds peak memory budget";
    }
    vm_init(candidate,d->vm.target,d->vm.send,d->vm.transport);candidate->output=&d->pixels;candidate->generation=d->vm.generation+1;
    memcpy(candidate->slot,d->vm.slot,sizeof(candidate->slot));
    memcpy(candidate->output_id,d->vm.output_id,sizeof(candidate->output_id));
    const char *error=prepare_instance(a,candidate,entry,request,settings,request?json_get(request,0,"inputs"):-1,&d->pixels.light);
    if(!error&&play&&!candidate->target)error="slot has no configured output";
    bool was_playing=d->vm.playing,output_attempted=false,output_succeeded=false;
    if(!error&&play&&candidate->output){
        if(d->composition)d->composition->command_active=true;
        if(d->transition){d->transition->command_active=true;if(d->transition->pending){d->pixels.type->cancel(d->pixels.driver);d->transition->pending=false;}}
        if(d->composition&&d->composition->pending){d->pixels.type->cancel(d->pixels.driver);d->composition->pending=false;}
        d->vm.playing=false;output_attempted=true;vm_output_cancel(&d->vm);
        PixelBuffer *output=candidate->output;const uint8_t *frame=control_output_frame(candidate);
        output_succeeded=output->driver&&output->type&&output->rgb&&output->count==candidate->control.count&&pixels_show_frame(output,frame,output->count);
        control_presented(candidate,output_succeeded);
    }
    if(error){
        if(output_attempted&&d->vm.control.output_valid&&d->vm.output)
            (void)pixels_show_frame(d->vm.output,d->vm.output->rgb,d->vm.output->count);
        if(d->composition)d->composition->command_active=false;
        if(d->transition)d->transition->command_active=false;
        d->vm.playing=was_playing;control_destroy(candidate);demo_free(candidate);return error;
    }
    if(output_succeeded){memcpy(candidate->output->rgb,control_output_frame(candidate),(size_t)candidate->control.count*3);candidate->control.output_valid=true;candidate->control.output_frame++;}
    clear_output_composition(d);
    control_destroy(&d->vm);d->vm=*candidate;demo_free(candidate);vm_rebind(&d->vm);
    d->vm.playing=play;d->vm.last_frame=platform_time_us();d->vm.next_frame=d->vm.last_frame;
    if(play)d->power_on=true;
    if(name){strcpy(d->active_program,name);d->active_crc=entry->crc;}else{d->active_program[0]=0;d->active_crc=0;}
    return NULL;
}
static bool activate(App *a,const char *name,uint16_t id,const char *action,const char *output,const Json *request,int settings) {
    AppOutput *d=app_output(a,output);
    if(!d){error_reply(a,id,STATUS_CONFIG,"unknown_output","output is not configured");return false;}
    const StoreEntry *entry=store_find(&a->store,STORE_ANIMATION,name);
    if(!entry||!strcmp(name,STORE_CONFIG)||!strcmp(name,STORE_NETWORK)){error_reply(a,id,STATUS_STORAGE,"not_found","stored program does not exist");return false;}
    if(strcmp(action,"play")){error_reply(a,id,STATUS_EXECUTION,"invalid_action","animation profiles execute through PLAY/FRAME");return false;}
    const char *error=install(a,d,entry,name,true,request,settings);
    if(error){if(!a->booting)error_reply(a,id,STATUS_LOAD,"activation_failed",error);return false;}
    reply(a,id,STATUS_OK,(int32_t)d->vm.control.count,"animation activated");return true;
}
static void indicator_tick(App *,AppOutput *,uint64_t);
static const char *save_output_state(App *,const AppOutput *,bool,const char *,const LightControls *);
static void service(void *context) {
    App *a=context;
    if(!a->hardware_changing)digital_inputs_tick(&a->digital_inputs,platform_time_us());
    bool previous=a->command_active;a->command_active=true;
    for(unsigned i=0;i<a->output_count;i++)if(a->outputs[i]){
        if(a->outputs[i]->hardware.indicators){indicator_tick(a,a->outputs[i],platform_time_us());continue;}
        if(a->outputs[i]->transition){
            if(!a->outputs[i]->transition->start_pending)transition_tick(a->outputs[i]->transition,64);
        }
        else if(a->outputs[i]->composition)composition_tick(a->outputs[i]->composition,64);
        else if(!runtime_owns(&a->outputs[i]->vm))vm_tick_budget(&a->outputs[i]->vm,64);
    }
    if(a->service_io)a->service_io(a->service_context);
    a->command_active=previous;
    if(!previous&&a->disconnect_pending){a->disconnect_pending=false;app_disconnect(a,a->upload_owner);}
}
void app_init(App *a, SendMessage send, void *transport) {
    memset(a,0,sizeof(*a)); a->connection_sequence=1; a->upload.pin=-1; a->send=send; a->transport=transport;
#if LIGHTVM_BOOT_SEQUENCE
    (void)boot_sequence_init(&a->boot_sequence);
#endif
    network_init(&a->network);
    a->network.recovery=LIGHTVM_POWER_RECOVERY && app_boot_sequence_triggered(a);
    platform_identity(a->controller_id,sizeof(a->controller_id),a->boot_id,sizeof(a->boot_id));
    vm_init(&a->fallback.vm,NULL,vm_send,a);
    empty_config(a);runtime_services(service,a);
    a->booting=true;
    if (!store_mount(&a->store)) snprintf(a->boot_error,sizeof(a->boot_error),"storage unsupported or corrupt; repair/reprovision required");
    const StoreEntry *config=store_find(&a->store,STORE_CONFIGURATION,STORE_CONFIG);
    if (config) {
        HardwareConfig hardware;
        char error[160];
        if (!store_read(&a->store,config,0,a->config_json,config->size)) { snprintf(a->boot_error,sizeof(a->boot_error),"configuration read failed"); empty_config(a); }
        else if (!config_validate(a,a->config_json,config->size,&hardware,error,sizeof(error))) { snprintf(a->boot_error,sizeof(a->boot_error),"%s",error); empty_config(a); }
        else if (!config_apply(a,&hardware,a->config_json,config->size)) snprintf(a->boot_error,sizeof(a->boot_error),"native output initialization failed");
        else if(!a->network.recovery) {
            for(unsigned i=0;i<a->output_count;i++)start_output(a,a->outputs[i]);
        }
    }
    const StoreEntry *saved_network=store_find(&a->store,STORE_NETWORK_SETTINGS,STORE_NETWORK);
    if (saved_network) {
        uint8_t *data=platform_calloc(1,saved_network->size);
        NetworkConfig candidate;
        if (!data || !store_read(&a->store,saved_network,0,data,saved_network->size) ||
            !network_parse(data,saved_network->size,NULL,&candidate,a->network_error,sizeof(a->network_error))) {
            if (!a->network_error[0]) strcpy(a->network_error,"network storage read failed");
        } else { a->network.config=candidate; a->network.saved_valid=true; }
        platform_free(data);
    }
    a->booting=false;
}
void app_destroy(App *a) {
    runtime_services(NULL,NULL);
    store_upload_cancel(&a->upload);
    empty_config(a);vm_destroy(&a->fallback.vm);store_destroy(&a->store);
    input_registry_destroy(&a->inputs);
}
bool app_boot_sequence_triggered(const App *a) {
#if LIGHTVM_BOOT_SEQUENCE
    return a->boot_sequence.triggered;
#else
    (void)a; return false;
#endif
}
static const char *const indicator_effects[]={"steady","slow_blink","rapid_blink","010100"};
static uint8_t indicator_brightness(const IndicatorLight *l,bool pressed,uint64_t now) {
    if(pressed)return 255;
    if(!l->on)return 0;
    uint64_t elapsed=now-l->effect_started;
    switch(l->effect){
    case INDICATOR_SLOW_BLINK: if((elapsed/500000)%2)return 0;break;
    case INDICATOR_RAPID_BLINK: if((elapsed/125000)%2)return 0;break;
    case INDICATOR_010100: { unsigned step=(unsigned)((elapsed/150000)%6);if(step!=1&&step!=3)return 0;break; }
    default: break;
    }
    return l->brightness;
}
static void indicator_tick(App *a,AppOutput *d,uint64_t now) {
    if(a->hardware_changing||runtime_owns(d))return;
    const void *previous=runtime_enter(d);
    if(d->indicator_pending){
        int status=d->pixels.type->poll(d->pixels.driver);
        if(!status)goto done;
        d->indicator_pending=false;d->indicator_fault=status<0;
        if(status<0){d->indicator_dirty=true;d->indicator_retry=now+1000000;}
    }
    // Poll before touching the frame: an asynchronous driver may still borrow it.
    uint16_t pressed=0;
    for(unsigned n=0;n<a->digital_inputs.count;n++){
        const DigitalInput *input=&a->digital_inputs.devices[n];
        if(input->available&&!strcmp(input->config.indicators,d->hardware.id))pressed=input->stable&input->press_feedback;
    }
    for(unsigned p=0;p<d->pixels.count;p++){
        uint8_t brightness=indicator_brightness(&d->indicators[p],p<16&&(pressed&(1u<<p)),now);
        for(unsigned c=0;c<3;c++){
            const IndicatorLight *l=&d->indicators[p];
            uint8_t value=(uint8_t)((l->rgb[c]*brightness+127u)/255u);
            if(d->pixels.rgb[p*3+c]!=value){d->pixels.rgb[p*3+c]=value;d->indicator_dirty=true;}
        }
    }
    if(d->indicator_dirty&&now>=d->indicator_retry){
        if(pixels_submit(&d->pixels,d->pixels.rgb,d->pixels.count)){d->indicator_pending=true;d->indicator_dirty=false;}
        else {d->indicator_fault=true;d->indicator_retry=now+1000000;}
    }
done:
    runtime_leave(previous);
}
void app_tick(App *a) {
    uint64_t now=platform_time_us();
    digital_inputs_tick(&a->digital_inputs,now);
#if LIGHTVM_BOOT_SEQUENCE
    boot_sequence_tick(&a->boot_sequence,now);
#endif
    if (a->upload_owner && now>=a->upload_deadline) app_disconnect(a,a->upload_owner);
    if (a->network_pending && now>=a->network_due) {
        a->network_pending=false; a->network.initial_setup=false;
        network_apply(&a->network,&a->pending_network);
        memset(&a->pending_network,0,sizeof(a->pending_network));
    }
    if (!a->network_pending) network_tick(&a->network,now);
    unsigned first=a->output_count?a->schedule_cursor++%a->output_count:0;
    for(unsigned i=0;i<a->output_count;i++){
        AppOutput *d=a->outputs[(first+i)%a->output_count];
        if(d->hardware.indicators){indicator_tick(a,d,platform_time_us());continue;}
        if(d->transition)transition_tick(d->transition,4096);else if(d->composition)composition_tick(d->composition,4096);else vm_tick(&d->vm);
        if(d->light_save_due&&now>=d->light_save_due&&!a->command_active){
            d->light_save_due=0;
            const char *error=save_output_state(a,d,app_output_power(d),app_output_selected_preset(a,d),&d->pixels.light);
            snprintf(d->light_error,sizeof(d->light_error),"%s",error?error:"");
        }
    }
}
static void resume_clock(App *a) {
    // Persistence can stall physical output; retain guest state and discard the gap.
    for(unsigned i=0;i<a->output_count;i++){
        AppOutput *d=a->outputs[i];uint64_t now=platform_time_us();
        d->vm.last_frame=now;d->vm.next_frame=now+FRAME_INTERVAL_US;
        for(unsigned slot=0;slot<2;slot++){Composition *s=output_composition(d,slot);if(s)for(unsigned n=0;n<s->layer_count;n++){s->layers[n].vm.last_frame=now;s->layers[n].vm.next_frame=now+FRAME_INTERVAL_US;}}
    }
}
static void transition_state(Writer *,AppOutput *);
static void describe(App *a, uint16_t id) {
    Writer w={.app=a,.id=id};
    const PlatformInfo *info=platform_info();
    literal(&w,"{\"version\":\"" LIGHTVM_VERSION "\",\"connections\":true,\"light_controls\":true,\"schema\":\"lightvm.config\",\"platform\":"); quoted(&w,info->platform);
    literal(&w,",\"controller_id\":"); quoted(&w,a->controller_id);
    literal(&w,",\"boot_id\":"); quoted(&w,a->boot_id);
    literal(&w,",\"wifi_supported\":"); literal(&w,LIGHTVM_WIFI?"true":"false");
    literal(&w,",\"wifi_ap_supported\":"); literal(&w,LIGHTVM_WIFI?"true":"false");
    literal(&w,",\"recovery_supported\":"); literal(&w,LIGHTVM_POWER_RECOVERY?"true":"false");
    literal(&w,",\"mqtt_supported\":"); literal(&w,LIGHTVM_MQTT?"true":"false");
    literal(&w,",\"http_supported\":"); literal(&w,LIGHTVM_HTTP?"true":"false");
    literal(&w,",\"websocket_supported\":"); literal(&w,LIGHTVM_WEBSOCKET?"true":"false");
    literal(&w,",\"boot_sequence_supported\":"); literal(&w,LIGHTVM_BOOT_SEQUENCE?"true":"false");
#if LIGHTVM_BOOT_SEQUENCE
    literal(&w,",\"boot_sequence\":{\"count\":"); number(&w,a->boot_sequence.count);
    literal(&w,",\"triggered\":"); literal(&w,app_boot_sequence_triggered(a)?"true":"false");
    literal(&w,",\"error\":"); quoted(&w,a->boot_sequence.error); literal(&w,"}");
#endif
    literal(&w,",\"network_revision\":"); number(&w,a->store.network_revision);
    literal(&w,",\"vm_heap_limit\":"); number(&w,VM_HEAP_LIMIT);
    literal(&w,",\"board\":"); quoted(&w,info->board);
    literal(&w,",\"output_types\":[{\"type\":\"ws2812b\",\"required\":[\"pin\",\"count\"],\"order_default\":\"GRB\",\"brightness_default\":255}");
    if(platform_apa102_supported())literal(&w,",{\"type\":\"apa102\",\"required\":[\"data_pin\",\"clock_pin\",\"count\"],\"order_default\":\"BGR\",\"brightness_default\":255}");
    literal(&w,"],\"pins\":[");
    size_t count; const BoardPin *pins=platform_pins(&count);
    for (size_t i=0;i<count;++i) { if (i) literal(&w,","); quoted(&w,pins[i].name); }
    literal(&w,"],\"pixel_capacity\":"); number(&w,PIXEL_CAPACITY);
    literal(&w,",\"outputs\":[");unsigned described=0;
    for(unsigned i=0;i<a->output_count;i++){
        AppOutput *d=a->outputs[i];HardwareOutput *hw=&d->hardware;
        VmInstance *vm=d->composition&&d->composition->layer_count?&d->composition->layers[0].vm:&d->vm;
        if(described++)literal(&w,",");
        literal(&w,"{\"id\":");quoted(&w,hw->id);literal(&w,",\"type\":");quoted(&w,hw->type);
        literal(&w,",\"mode\":");quoted(&w,hw->indicators?"indicators":"animation");
        if(hw->indicators){literal(&w,",\"indicator_fault\":");literal(&w,d->indicator_fault?"true":"false");}
        literal(&w,",\"slot\":");quoted(&w,vm->slot);literal(&w,",\"pixels\":");number(&w,hw->pixels.count);
        literal(&w,",\"ready\":");literal(&w,d->transition||d->composition||vm->ready?"true":"false");
        literal(&w,",\"playing\":");literal(&w,(d->transition?d->transition->playing:d->composition?d->composition->playing:vm->playing)?"true":"false");
        literal(&w,",\"frames\":");number(&w,d->transition?d->transition->frames:d->composition?d->composition->frames:vm->frame_count);
        literal(&w,",\"render_us\":");number(&w,vm->render_us);
        literal(&w,",\"show_us\":");number(&w,d->transition?(d->transition->frames?(uint32_t)(d->transition->output_us/d->transition->frames):0):d->composition?(d->composition->frames?(uint32_t)(d->composition->output_us/d->composition->frames):0):vm->show_us);
        const char *preset=app_output_preset(a,d);literal(&w,",\"preset\":");if(preset)quoted(&w,preset);else literal(&w,"null");
        literal(&w,",\"power\":");literal(&w,app_output_power(d)?"true":"false");
        char light[384];app_output_light_json(a,d,light,sizeof(light),false);
        literal(&w,",\"light\":");literal(&w,light);
        literal(&w,",\"light_error\":");quoted(&w,d->light_error);
        const char *selection=app_output_selected_preset(a,d);literal(&w,",\"selected_preset\":");if(selection)quoted(&w,selection);else literal(&w,"null");
        literal(&w,",\"active_program\":");quoted(&w,d->transition?"transition":d->composition?"composition":d->active_program);
        literal(&w,",\"animation_error\":");quoted(&w,d->transition?d->transition->error:d->composition?d->composition->error:vm->animation_error);
        if(d->composition){literal(&w,",\"composition\":");composition_state(&w,d);}
        if(d->transition){literal(&w,",\"transition\":");transition_state(&w,d);}
        literal(&w,"}");
    }
    literal(&w,"]");
    literal(&w,",\"inputs\":[");
    for(unsigned i=0;i<a->digital_inputs.count;i++){
        const DigitalInput *d=&a->digital_inputs.devices[i];if(i)literal(&w,",");
        literal(&w,"{\"id\":");quoted(&w,d->config.id);literal(&w,",\"type\":\"tca9555\",\"available\":");literal(&w,d->available?"true":"false");
        literal(&w,",\"pressed\":");if(d->available)number(&w,d->stable);else literal(&w,"null");literal(&w,"}");
    }
    literal(&w,"],\"input_event_overflow\":");number(&w,a->digital_inputs.overflow);
    literal(&w,",\"config_limit\":"); number(&w,CONFIG_LIMIT);
    literal(&w,",\"module_limit\":"); number(&w,MODULE_LIMIT);
    literal(&w,",\"instance_context_bytes\":"); number(&w,sizeof(VmInstance));
    literal(&w,",\"app_context_bytes\":"); number(&w,sizeof(App));
    literal(&w,",\"upload_workspace_bytes\":"); number(&w,sizeof(StoreUpload));
    literal(&w,",\"upload_page_bytes\":"); number(&w,STORE_PAGE_BYTES);
    literal(&w,",\"animation_reset\":true,\"activation_settings\":true,\"slots\":[\"A\",\"B\"],\"slot_binding\":\"output\",\"pixel_format\":\"rgb8\",\"frame_policy\":\"transparent_before_render\",\"alpha\":true,\"compositions\":true,\"composition_layer_limit\":8,\"layer_pixel_format\":\"rgba8-premul\",\"composition\":true,\"program_generation\":"); number(&w,app_output(a,NULL)->vm.generation);
    literal(&w,",\"ab_transitions\":true,\"transition_sources\":true,\"transition_programs\":true,\"custom_transitions\":true,\"transition_effects\":[");
    for(unsigned n=0;n<TRANSITION_PROGRAM;n++){if(n)literal(&w,",");quoted(&w,transition_effect_name((TransitionEffect)n));}literal(&w,"]");
    literal(&w,",\"transient_settings\":true,\"preset_transitions\":true,\"presets\":true,\"typed_storage\":true");
    literal(&w,",\"live_inputs\":true,\"input_limit\":8,\"input_source_limit\":16");
    literal(&w,",\"composition_blend_modes\":[\"over\",\"add\",\"screen\",\"multiply\"]");
    literal(&w,",\"storage_available\":"); literal(&w,a->store.available ? "true" : "false");
    StoreInfo storage=store_info(&a->store);
    literal(&w,",\"storage_backend\":");quoted(&w,store_backend_name(&a->store));
    literal(&w,",\"storage_writable\":");literal(&w,storage.writable?"true":"false");
    literal(&w,",\"storage_bytes\":");number(&w,storage.total_bytes);
    literal(&w,",\"free_bytes\":");number(&w,storage.free_bytes);
    literal(&w,",\"largest_free_bytes\":");number(&w,storage.largest_free_bytes);
    literal(&w,",\"storage_allocation_unit\":");number(&w,storage.allocation_unit);
    literal(&w,",\"catalog_revision\":"); number(&w,a->store.generation);
    literal(&w,",\"active_revision\":"); number(&w,a->revision);
    literal(&w,",\"saved_revision\":"); number(&w,a->store.config_revision);
    literal(&w,",\"active_program\":"); quoted(&w,app_output(a,NULL)->active_program);
    literal(&w,",\"active_crc32\":"); number(&w,app_output(a,NULL)->active_crc);
    literal(&w,",\"boot_error\":"); quoted(&w,a->boot_error);
    literal(&w,",\"animation_error\":"); quoted(&w,app_output(a,NULL)->vm.animation_error);
    literal(&w,",\"target\":\"A\",\"uploading\":"); literal(&w,a->uploading ? "true" : "false");
    literal(&w,"}"); flush(&w); reply(a,id,STATUS_OK,0,"capabilities");
}
typedef struct { char op[33],name[33],action[16],slot[33],output[33]; StoreType type; uint32_t revision,saved_revision,command,delta_ms; bool has_revision,has_saved_revision; } Arguments;
static const StoreEntry *find_program(const Store *store,StoreType type,const char *name) {
    if(type!=STORE_ANY)return store_find(store,type,name);
    const StoreEntry *animation=store_find(store,STORE_ANIMATION,name),*transition=store_find(store,STORE_TRANSITION,name);
    return animation&&transition?NULL:animation?animation:transition;
}
static bool arguments(const uint8_t *p,size_t size,Arguments *args) {
    Json j;
    memset(args,0,sizeof(*args)); memcpy(args->action,"play",5); memcpy(args->slot,"A",2);
    static const char *const fields[]={"op","name","type","action","slot","output","expected_revision","expected_saved_revision","settings","inputs","command","delta_ms",NULL};
    if (!json_parse(&j,p,size) || !json_fields(&j,0,fields) || !json_string(&j,json_get(&j,0,"op"),args->op,sizeof(args->op))) return false;
    if(json_get(&j,0,"settings")>=0&&strcmp(args->op,"program_activate"))return false;
    if(json_get(&j,0,"inputs")>=0&&strcmp(args->op,"program_activate"))return false;
    int type=json_get(&j,0,"type");char type_name[16];
    if(type>=0&&(!json_string(&j,type,type_name,sizeof(type_name))||
       ((args->type=store_type(type_name))!=STORE_ANIMATION&&args->type!=STORE_TRANSITION)))return false;
    if(type>=0&&strcmp(args->op,"program_list")&&strcmp(args->op,"program_get")&&strcmp(args->op,"program_save")&&strcmp(args->op,"program_delete"))return false;
    int name=json_get(&j,0,"name"),action=json_get(&j,0,"action");
    if (name>=0 && (!json_string(&j,name,args->name,sizeof(args->name)) || !object_name_valid(args->name))) return false;
    if (action>=0 && !json_string(&j,action,args->action,sizeof(args->action))) return false;
    int slot=json_get(&j,0,"slot");
    if (slot>=0 && !json_string(&j,slot,args->slot,sizeof(args->slot))) return false;
    int command=json_get(&j,0,"command"),delta=json_get(&j,0,"delta_ms");
    if(command>=0&&!json_u32(&j,command,&args->command))return false;
    if(delta>=0&&!json_u32(&j,delta,&args->delta_ms))return false;
    if(strcmp(args->slot,"A"))return false;
    int output=json_get(&j,0,"output");
    if(output>=0&&(!json_string(&j,output,args->output,sizeof(args->output))||!object_name_valid(args->output)))return false;
    int revision=json_get(&j,0,"expected_revision"),saved=json_get(&j,0,"expected_saved_revision");
    args->has_revision=revision>=0; args->has_saved_revision=saved>=0;
    return (!args->has_revision || json_u32(&j,revision,&args->revision)) && (!args->has_saved_revision || json_u32(&j,saved,&args->saved_revision));
}
static bool preset_references(App *a,StoreType type,const char *name) {
    uint32_t cursor=0;const StoreEntry *entry;
    while((entry=store_list(&a->store,STORE_PRESET,&cursor))){
        uint8_t *data=platform_calloc(1,entry->size);Json *j=platform_calloc(1,sizeof(*j));
        bool found=!data||!j||!store_read(&a->store,entry,0,data,entry->size)||!json_parse(j,data,entry->size);
        if(!found&&type==STORE_TRANSITION){
            int transition=json_get(j,0,"entry_transition");found=transition>=0&&json_equal(j,json_get(j,transition,"program"),name);
        }else if(!found){
            int layers=json_get(j,0,"layers");
            if(layers<0)found=true;
            else for(int t=layers+1;t<j->tokens[layers].next;t=j->tokens[t].next)
                if(json_equal(j,json_get(j,t,"program"),name)){found=true;break;}
        }
        platform_free(j);platform_free(data);if(found)return true;
    }
    return false;
}
static bool hardware_references(const HardwareConfig *hardware,StoreType type,const char *name){
    for(unsigned i=0;i<hardware->count;i++){
        const HardwareOutput *d=&hardware->outputs[i];
        if(type==STORE_PRESET&&!strcmp(d->startup_preset,name))return true;
    }
    return false;
}
static bool configuration_references(App *a,StoreType type,const char *name){
    if(type!=STORE_PRESET)return false;
    if(hardware_references(&a->hardware,type,name))return true;
    const StoreEntry *entry=store_find(&a->store,STORE_CONFIGURATION,STORE_CONFIG);
    if(!entry)return false;
    uint8_t *data=platform_calloc(1,entry->size);if(!data)return true;
    HardwareConfig hardware;char error[160];
    bool valid=store_read(&a->store,entry,0,data,entry->size)&&hardware_parse(data,entry->size,&hardware,error,sizeof(error));
    platform_free(data);if(!valid)return true;
    bool found=hardware_references(&hardware,type,name);hardware_destroy(&hardware);return found;
}
static bool referenced(App *a,StoreType type,const char *name) {
    if(preset_references(a,type,name)||configuration_references(a,type,name))return true;
    for(unsigned i=0;i<a->output_count;i++){
        AppOutput *d=a->outputs[i];
        if(type==STORE_ANIMATION&&!strcmp(d->active_program,name))return true;
        if(type==STORE_ANIMATION)for(unsigned slot=0;slot<2;slot++){
            Composition *s=output_composition(d,slot);if(s)for(unsigned n=0;n<s->layer_count;n++)if(!strcmp(s->layers[n].program,name))return true;
        }
        if(d->transition)for(unsigned n=0;n<2;n++)if(d->transition->programs[n]&&d->transition->programs[n]->vm.code.type==type&&!strcmp(d->transition->programs[n]->name,name))return true;
    }
    return false;
}
static void read_object(App *a,uint16_t id,const StoreEntry *entry) {
    uint8_t buffer[FRAME_LIMIT];
    for (uint32_t offset=0;offset<entry->size;) {
        size_t n=entry->size-offset; if (n>sizeof(buffer)) n=sizeof(buffer);
        if (!store_read(&a->store,entry,offset,buffer,n)) { error_reply(a,id,STATUS_STORAGE,"read_failed","object read failed"); return; }
        a->send(a->transport,RESP_DATA,id,buffer,n); offset+=(uint32_t)n;
    }
    reply(a,id,STATUS_OK,(int32_t)entry->size,"object read");
}
static bool layer_blend(const Json *j,int token,uint8_t *mode){
    for(unsigned n=0;n<DRAW_BLEND_COUNT;n++)if(json_equal(j,token,draw_blend_name((DrawBlend)n))){*mode=(uint8_t)n;return true;}
    return false;
}
static const char *prepare_layer(App *a,AppOutput *d,CompositionLayer *layer,const Json *j,int object,const LightControls *light){
    static const char *const fields[]={"id","program","crc32","settings","inputs","opacity","blend",NULL};
    if(!json_fields(j,object,fields)||
       !json_string(j,json_get(j,object,"id"),layer->id,sizeof(layer->id))||!object_name_valid(layer->id)||
       !json_string(j,json_get(j,object,"program"),layer->program,sizeof(layer->program))||!object_name_valid(layer->program))return "invalid composition layer";
    uint32_t opacity=255;int alpha=json_get(j,object,"opacity");
    if(alpha>=0&&(!json_u32(j,alpha,&opacity)||opacity>255))return "layer opacity must be 0..255";
    layer->opacity=(uint8_t)opacity;
    int blend=json_get(j,object,"blend");
    if(blend>=0&&!layer_blend(j,blend,&layer->blend))return "layer blend must be over, add, screen or multiply";
    const StoreEntry *entry=store_find(&a->store,STORE_ANIMATION,layer->program);
    if(!entry||!strcmp(layer->program,STORE_CONFIG)||!strcmp(layer->program,STORE_NETWORK))return "layer program does not exist";
    uint32_t crc;int fingerprint=json_get(j,object,"crc32");
    if(fingerprint>=0&&(!json_u32(j,fingerprint,&crc)||crc!=entry->crc))return "layer program version differs from the saved preset";
    if(d->layer_generation<d->vm.generation)d->layer_generation=d->vm.generation;
    if(d->layer_generation==UINT32_MAX)return "layer generation exhausted";
    vm_init(&layer->vm,&layer->target,vm_send,a);
    layer->vm.generation=++d->layer_generation;
    strcpy(layer->vm.output_id,d->hardware.id);strcpy(layer->vm.layer,layer->id);
    layer->crc=entry->crc;
    const char *error=prepare_instance(a,&layer->vm,entry,j,json_get(j,object,"settings"),json_get(j,object,"inputs"),light);
    if(!error){layer->vm.playing=true;layer->vm.last_frame=platform_time_us();layer->vm.next_frame=layer->vm.last_frame+FRAME_INTERVAL_US;}
    return error;
}
static const char *prepare_composition_data(App *a,AppOutput *d,Composition **out,const void *data,uint32_t size,const char *schema,TransitionSpec *entry_transition,const LightControls *light){
    *out=NULL;Json *j=platform_calloc(1,sizeof(*j));
    if(!j)return "composition parser allocation failed";
    static const char *const fields[]={"schema","layers","entry_transition",NULL};
    const char *error="invalid composition document";
    if(!json_parse(j,data,size)||!json_fields(j,0,fields)||!json_equal(j,json_get(j,0,"schema"),schema))goto done;
    int layers=json_get(j,0,"layers");
    if(layers<0||j->tokens[layers].type!=JSON_ARRAY||(unsigned)j->tokens[layers].children>COMPOSITION_LAYER_LIMIT)goto done;
    int transition=json_get(j,0,"entry_transition");
    if(strcmp(schema,"lightvm.preset")&&transition>=0)goto done;
    if(entry_transition){char message[160];if(!transition_spec_parse(j,transition,entry_transition,message,sizeof(message))){error="invalid preset entry transition";goto done;}}
    if(!strcmp(schema,"lightvm.preset")){
        if(!j->tokens[layers].children){error="preset requires at least one layer";goto done;}
        for(int t=layers+1;t<j->tokens[layers].next;t=j->tokens[t].next)
            if(json_get(j,t,"crc32")<0){error="preset layers require crc32 version references";goto done;}
    }
    Composition *candidate=composition_create(&d->pixels,(unsigned)j->tokens[layers].children);
    if(!candidate){error="composition exceeds memory budget";goto done;}
    *out=candidate;error=NULL;unsigned n=0;
    for(int t=layers+1;t<j->tokens[layers].next;t=j->tokens[t].next,n++){
        error=prepare_layer(a,d,&candidate->layers[n],j,t,light?light:&d->pixels.light);
        if(error)break;
        for(unsigned previous=0;previous<n;previous++)if(!strcmp(candidate->layers[previous].id,candidate->layers[n].id)){error="duplicate layer id";break;}
        if(error)break;
    }
    if(!error)composition_prepare(candidate);
done:platform_free(j);return error;
}
static void apply_composition(AppOutput *d,Composition *candidate){
    clear_output_composition(d);vm_output_cancel(&d->vm);d->vm.output=NULL;control_destroy(&d->vm);d->vm.playing=false;d->vm.ready=false;
    d->composition=candidate;candidate->revision=++d->composition_revision;d->active_program[0]=0;d->active_crc=0;
    d->power_on=true;
}
/* Admit the presenter and any direct-to-composition wrapper before moving ownership. */
static const char *prepare_presenter(AppOutput *d,Transition **out,Composition **wrapper){
    *out=NULL;*wrapper=NULL;if(d->transition)return NULL;
    Transition *t=transition_create(&d->pixels);if(!t)return "transition exceeds aggregate memory budget";
    if(!d->composition&&d->vm.ready){*wrapper=composition_create(&t->target,1);if(!*wrapper){transition_destroy(t);return "composition wrapper exceeds memory budget";}}
    *out=t;return NULL;
}
static void attach_presenter(App *a,AppOutput *d,Transition *t,Composition *wrapper){
    vm_output_cancel(&d->vm);
    if(d->composition){if(d->composition->prepared)composition_publish(d->composition);composition_offscreen(d->composition,&t->target);}
    else if(wrapper){
        CompositionLayer *layer=&wrapper->layers[0];strcpy(layer->id,"main");strcpy(layer->program,d->active_program);layer->crc=d->active_crc;
        layer->vm=d->vm;layer->vm.output=NULL;layer->vm.target=&layer->target;strcpy(layer->vm.layer,"main");
        vm_heap_uncharge(layer->vm.control.output_charge);layer->vm.control.output_charge=0;vm_rebind(&layer->vm);
        uint32_t generation=d->vm.generation;bool uploading=d->vm.uploading;
        layer->vm.uploading=false;vm_init(&d->vm,&d->pixels,vm_send,a);d->vm.generation=generation;d->vm.uploading=uploading;
        if(generation>d->layer_generation)d->layer_generation=generation;
        composition_prepare(wrapper);composition_publish(wrapper);d->composition=wrapper;
    }
    d->vm.output=NULL;t->assignment=a->transition_sequence;t->compositions[0]=d->composition;d->transition=t;d->active_program[0]=0;d->active_crc=0;
}
static const char *ensure_transition(App *a,AppOutput *d){
    Transition *t=NULL;Composition *wrapper=NULL;
    const char *error=prepare_presenter(d,&t,&wrapper);
    if(!error&&t)attach_presenter(a,d,t,wrapper);
    return error;
}

static const char *apply_composition_slot(App *a,AppOutput *d,Composition *candidate,unsigned slot){
    if(!slot&&!d->transition){apply_composition(d,candidate);return NULL;}
    const char *error=ensure_transition(a,d);if(error)return error;
    Transition *t=d->transition;transition_invalidate(t);
    composition_offscreen(candidate,&t->target);composition_prepare(candidate);composition_publish(candidate);
    for(unsigned n=0;n<candidate->layer_count;n++)strcpy(candidate->layers[n].vm.slot,slot?"B":"A");
    Composition *old=t->compositions[slot];t->compositions[slot]=candidate;if(!slot)d->composition=candidate;
    composition_destroy(old);transition_invalidate(t);candidate->revision=++d->composition_revision;d->recall_owned=false;d->power_on=true;return NULL;
}
static void activate_composition_slot(App *a,AppOutput *d,uint16_t id,const Json *j,unsigned slot){
    static const char *const fields[]={"op","name","output","slot","action","settings","inputs",NULL};
    char name[OBJECT_NAME_LIMIT+1];
    int action=json_get(j,0,"action");
    if(!json_fields(j,0,fields)||!json_string(j,json_get(j,0,"name"),name,sizeof(name))||!object_name_valid(name)||(action>=0&&!json_equal(j,action,"play"))){error_reply(a,id,STATUS_CONFIG,"invalid_activation","expected a program name and play action");return;}
    const StoreEntry *entry=store_find(&a->store,STORE_ANIMATION,name);
    if(d->layer_generation<d->vm.generation)d->layer_generation=d->vm.generation;
    if(!entry||d->layer_generation==UINT32_MAX||d->composition_revision==UINT32_MAX){error_reply(a,id,STATUS_CONFIG,"preparation_failed","program missing or generation exhausted");return;}
    Composition *candidate=composition_create(&d->pixels,1);
    if(!candidate){error_reply(a,id,STATUS_LOAD,"preparation_failed","composition exceeds peak memory budget");return;}
    CompositionLayer *layer=&candidate->layers[0];strcpy(layer->id,"main");strcpy(layer->program,name);layer->crc=entry->crc;
    vm_init(&layer->vm,&layer->target,vm_send,a);layer->vm.generation=++d->layer_generation;
    strcpy(layer->vm.output_id,d->hardware.id);strcpy(layer->vm.slot,slot?"B":"A");strcpy(layer->vm.layer,"main");
    const char *error=prepare_instance(a,&layer->vm,entry,j,json_get(j,0,"settings"),json_get(j,0,"inputs"),&d->pixels.light);
    if(!error){layer->vm.playing=true;layer->vm.last_frame=platform_time_us();layer->vm.next_frame=layer->vm.last_frame+FRAME_INTERVAL_US;error=apply_composition_slot(a,d,candidate,slot);}
    if(error){composition_destroy(candidate);error_reply(a,id,STATUS_LOAD,"preparation_failed",error);return;}
    reply(a,id,STATUS_OK,(int32_t)d->composition_revision,"composition slot activated");
}
static void real_number(Writer *w,double value){char text[40];snprintf(text,sizeof(text),"%.17g",value);literal(w,text);}
static void binding_text(void *context,const char *text){literal(context,text);}
static void transition_state(Writer *w,AppOutput *d){
    Transition *t=d->transition;if(t)transition_update(t,platform_time_us());
    literal(w,"{\"revision\":");number(w,d->composition_revision);literal(w,",\"active\":");literal(w,t?"true":"false");
    literal(w,",\"progress\":");real_number(w,t?t->progress:0);literal(w,",\"effect\":");quoted(w,transition_effect_name(t?t->effect:TRANSITION_CROSSFADE));
    literal(w,",\"seed\":");number(w,t?t->seed:0);literal(w,",\"playing\":");literal(w,t&&t->playing?"true":"false");
    literal(w,",\"driver\":{\"kind\":");quoted(w,!t||t->driver==TRANSITION_MANUAL?"manual":t->driver==TRANSITION_TIMED?"timed":t->driver==TRANSITION_INPUT?"input":"source");
    literal(w,",\"assignment\":");number(w,t?t->assignment:0);
    if(t&&t->driver==TRANSITION_SOURCE){literal(w,",\"source\":");quoted(w,t->source);}
    if(t&&t->driver==TRANSITION_INPUT){literal(w,",\"binding\":");input_binding_describe(t->input,binding_text,w);literal(w,",\"fallback\":");literal(w,t->input->fallback?"true":"false");}
    if(t&&t->driver==TRANSITION_TIMED){literal(w,",\"start\":");real_number(w,t->start);literal(w,",\"target\":");real_number(w,t->target_progress);literal(w,",\"duration_ms\":");real_number(w,t->duration/1000);if(t->interval){literal(w,",\"interval_ms\":");real_number(w,t->interval/1000);}}
    literal(w,"},\"recall\":");
    if(d->recall_owned){literal(w,"{\"power\":");literal(w,d->power_on?"true":"false");literal(w,",\"preset\":");const char *name=app_output_selected_preset(w->app,d);if(name)quoted(w,name);else literal(w,"null");literal(w,"}");}else literal(w,"null");
    literal(w,",\"A\":");
    if(!d->composition&&d->vm.ready&&!t){
        literal(w,"{\"revision\":");number(w,d->composition_revision);literal(w,",\"active\":true,\"layers\":[{\"id\":\"main\",\"program\":");quoted(w,d->active_program);
        literal(w,",\"generation\":");number(w,d->vm.generation);literal(w,"}]}");
    }else composition_state_for(w,d,d->composition);
    literal(w,",\"B\":");composition_state_for(w,d,output_composition(d,1));
    literal(w,",\"frames\":");number(w,t?t->frames:0);literal(w,",\"composite_frames\":");number(w,t?t->composite_frames:0);
    literal(w,",\"composite_us\":");wide(w,t?t->composite_us:0);literal(w,",\"output_us\":");wide(w,t?t->output_us:0);
    literal(w,",\"error\":");quoted(w,t?t->error:"");
    literal(w,",\"program\":");quoted(w,t&&t->effect==TRANSITION_PROGRAM&&t->programs[0]?t->programs[0]->name:"");
    literal(w,",\"program_crc32\":");number(w,t&&t->effect==TRANSITION_PROGRAM&&t->programs[0]?t->programs[0]->vm.code.crc:0);
    literal(w,",\"mask\":");quoted(w,t&&t->effect==TRANSITION_CUSTOM&&t->programs[0]?t->programs[0]->name:"");literal(w,",\"paint\":");quoted(w,t&&t->effect==TRANSITION_CUSTOM&&t->programs[1]?t->programs[1]->name:"");literal(w,"}");
}
static const char *prepare_transition_program(App *a,PixelBuffer *output,const Json *j,int token,uint32_t seed,bool transition_profile,TransitionProgram **out){
    static const char *const fields[]={"program","settings","inputs",NULL};
    TransitionProgram *program=demo_calloc(1,sizeof(*program));if(!program)return "transition program exceeds memory budget";
    *out=program;
    if((!transition_profile&&!json_fields(j,token,fields))||!json_string(j,json_get(j,token,"program"),program->name,sizeof(program->name))||!object_name_valid(program->name))return "transition requires program and optional settings";
    const StoreEntry *entry=store_find(&a->store,transition_profile?STORE_TRANSITION:STORE_ANIMATION,program->name);if(!entry)return "transition program does not exist";
    vm_init(&program->vm,output,vm_send,a);program->vm.output=NULL;
    if(a->transition_program_generation==UINT32_MAX)return "transition program generation exhausted";
    program->vm.generation=++a->transition_program_generation;
    for(unsigned n=0;n<a->output_count;n++)if(&a->outputs[n]->pixels==output)strcpy(program->vm.output_id,a->outputs[n]->hardware.id);
    const char *error=vm_bind_storage(&program->vm,&a->store,entry)?vm_load(&program->vm):"package reference limit exceeded";
    if(!error&&program->vm.portable->profile!=(transition_profile?4u:3u))error=transition_profile?"program requires the transition profile":"custom mask and paint require animation profiles";
    if(!error){program->vm.portable->deterministic_random=true;program->vm.portable->default_seed=seed;error=control_settings(&program->vm,j,json_get(j,token,"settings"));}
    if(!error)inputs_attach(program->vm.portable,&a->inputs,&output->light);
    if(!error)error=inputs_bind(program->vm.portable,j,json_get(j,token,"inputs"),0);
    if(!error){program->vm.ready=true;error=control_prepare(&program->vm);}
    if(!error&&!transition_program_capture(program))error="transition baseline exceeds memory budget";
    return error;
}
static const char *prepare_recall_transition(App *a,PixelBuffer *output,const void *data,const TransitionSpec *spec,TransitionProgram **out){
    *out=NULL;if(!spec->program[0])return NULL;
    const StoreEntry *entry=store_find(&a->store,STORE_TRANSITION,spec->program);
    if(!entry||entry->crc!=spec->crc)return "transition program missing or version differs from saved reference";
    Json *j=platform_calloc(1,sizeof(*j));
    const char *error="transition parser allocation failed";
    if(j){
        error=json_parse(j,(const uint8_t *)data+spec->offset,spec->size)?
            prepare_transition_program(a,output,j,0,spec->seed,true,out):"invalid transition document";
        platform_free(j);
    }
    return error;
}
static void bind_composition_slot(Composition *s,unsigned slot){
    if(s)for(unsigned n=0;n<s->layer_count;n++)strcpy(s->layers[n].vm.slot,slot?"B":"A");
}
static void swap_compositions(AppOutput *d){
    Transition *t=d->transition;Composition *old=t->compositions[0];
    t->compositions[0]=t->compositions[1];t->compositions[1]=old;d->composition=t->compositions[0];
    bind_composition_slot(t->compositions[0],0);bind_composition_slot(t->compositions[1],1);
    t->progress=100-t->progress;
}
#define OUTPUT_STATE_NAME 9u
#define OUTPUT_STATE_BYTES (OUTPUT_STATE_NAME + OBJECT_NAME_LIMIT + 1u)
static const char *save_output_state(App *a,const AppOutput *d,bool on,const char *name,const LightControls *light){
    if(a->booting||!store_info(&a->store).writable)return NULL;
    uint8_t state[OUTPUT_STATE_BYTES]={0},previous[OUTPUT_STATE_BYTES];state[0]=on;
    const StoreEntry *entry=store_find(&a->store,STORE_OUTPUT_STATE,d->hardware.id);
    bool known=entry&&entry->size==sizeof(previous)&&store_read(&a->store,entry,0,previous,sizeof(previous));
    uint32_t brightness;memcpy(&brightness,&light->brightness,sizeof(brightness));
    write_u32(state+1,brightness);write_u32(state+5,light->color);
    if(name&&name[0])snprintf((char *)state+OUTPUT_STATE_NAME,OBJECT_NAME_LIMIT+1,"%s",name);
    else if(known&&memchr(previous+OUTPUT_STATE_NAME,0,OBJECT_NAME_LIMIT+1)){
        memcpy(state+OUTPUT_STATE_NAME,previous+OUTPUT_STATE_NAME,OBJECT_NAME_LIMIT+1);
        if(on)state[0]=previous[0];
    }else state[0]=0;
    if(known&&!memcmp(state,previous,sizeof(state)))return NULL;
    bool ok=store_put(&a->store,STORE_OUTPUT_STATE,d->hardware.id,state,sizeof(state));resume_clock(a);
    return ok?NULL:"could not persist output light state";
}
/* Everything that can fail is admitted before changing either live slot/driver.
 * No intermediate frame is required: the next fade starts at the old destination. */
static const char *begin_recall(App *a,AppOutput *d,Composition *candidate,TransitionProgram *program,double duration,bool on,const char *name,uint32_t crc,const LightControls *light){
    if(d->composition_revision==UINT32_MAX||a->transition_sequence==UINT32_MAX)return "recall revision or assignment counter exhausted";
    bool was_on=app_output_power(d),owned=d->recall_owned;
    Transition *presenter=NULL;Composition *wrapper=NULL;
    const char *error=prepare_presenter(d,&presenter,&wrapper);if(error)return error;
    error=save_output_state(a,d,on,name,light?light:&d->pixels.light);
    if(error){composition_destroy(wrapper);transition_destroy(presenter);return error;}
    if(light){d->pixels.light=*light;d->light_revision++;}
    d->light_save_due=0;d->light_error[0]=0;
    if(candidate)for(unsigned n=0;n<candidate->layer_count;n++)inputs_attach(candidate->layers[n].vm.portable,&a->inputs,&d->pixels.light);
    if(presenter)attach_presenter(a,d,presenter,wrapper);
    Transition *t=d->transition;t->command_active=true;
    transition_hold(t);transition_invalidate(t);
    if(t->pending){d->pixels.type->cancel(d->pixels.driver);t->pending=false;}
    if(!owned&&!was_on){
        for(unsigned n=0;n<2;n++){composition_destroy(t->compositions[n]);t->compositions[n]=NULL;}
        d->composition=NULL;t->progress=0;
    }else if(owned||t->progress>0){
        t->progress=100;swap_compositions(d);
    }
    Composition *old=t->compositions[1];t->compositions[1]=NULL;composition_destroy(old);
    if(candidate){composition_offscreen(candidate,&t->target);composition_prepare(candidate);composition_publish(candidate);bind_composition_slot(candidate,1);}
    t->compositions[1]=candidate;
    for(unsigned n=0;n<2;n++){transition_program_destroy(t->programs[n]);t->programs[n]=NULL;}
    t->programs[0]=program;t->effect=program?TRANSITION_PROGRAM:TRANSITION_CROSSFADE;t->seed=program?program->vm.portable->default_seed:0;
    t->error[0]=0;t->progress=0;t->assignment=a->transition_sequence;
    (void)transition_run(t,100,duration);a->transition_sequence=t->assignment;
    t->start_pending=a->booting&&t->driver==TRANSITION_TIMED;
    transition_play(t,true);transition_invalidate(t);t->command_active=false;
    d->recall_owned=true;d->power_on=on;
    snprintf(d->selected_preset,sizeof(d->selected_preset),"%s",name?name:"");d->selected_preset_crc=crc;
    ++d->composition_revision;return NULL;
}
static bool transition_manage(App *a,AppOutput *d,uint16_t id,const Json *j,const char *op){
    if(strncmp(op,"transition_",11))return false;
    static const char *const fields[]={"op","output","expected_revision","expected_assignment","binding","progress","target","duration_ms","interval_ms","source","assignment","effect","seed","mask","paint","program","settings","inputs",NULL};
    if(!json_fields(j,0,fields)||!d->pixels.count){error_reply(a,id,STATUS_CONFIG,"invalid_transition","invalid transition request or output");return true;}
    if(!strcmp(op,"transition_get")){Writer w={.app=a,.id=id};transition_state(&w,d);flush(&w);reply(a,id,STATUS_OK,0,"A/B transition state");return true;}
    const char *error=NULL;double value=0,duration=0,interval=0;char source[OBJECT_NAME_LIMIT+1]="";uint32_t assignment=0,seed=0;
    bool swap=!strcmp(op,"transition_swap"),select=!strcmp(op,"transition_select"),set=!strcmp(op,"transition_set"),run=!strcmp(op,"transition_run"),assign=!strcmp(op,"transition_assign"),update=!strcmp(op,"transition_update"),hold=!strcmp(op,"transition_hold"),bind=!strcmp(op,"transition_bind");
    TransitionEffect effect=TRANSITION_CROSSFADE;TransitionProgram *programs[2]={0};
    if(!swap&&!select&&!set&&!run&&!assign&&!update&&!hold&&!bind)error="unknown transition operation";
    if(bind){
        uint32_t expected;int binding=json_get(j,0,"binding");
        if(!json_u32(j,json_get(j,0,"expected_assignment"),&expected)||expected!=(d->transition?d->transition->assignment:0))error="stale driver assignment";
        else{
            InputPort port={.type=T_FLOAT,.bounds=3,.maximum={.f=1},.light=&d->pixels.light};
            if(d->transition)transition_update(d->transition,platform_time_us());
            port.initial.f=port.snapshot.f=port.driver.value.f=d->transition?(float)(d->transition->progress/100):0;
            error=input_port_bind(&a->inputs,&port,j,binding,platform_time_us());
        }
    }
    if(set||run){if(!json_f64(j,json_get(j,0,set?"progress":"target"),&value)||!transition_percentage(value))error="progress must be a finite number in 0..100";}
    if(run&&(!json_f64(j,json_get(j,0,"duration_ms"),&duration)||duration<0||!isfinite(duration*1000)))error="duration_ms must be finite and nonnegative";
    if(run){int token=json_get(j,0,"interval_ms");
        if(token>=0&&(!json_f64(j,token,&interval)||interval<0||!isfinite(interval*1000)||(interval>0&&interval<duration)))error="interval_ms must be zero or finite and at least duration_ms";
    }
    if((assign||update)&&(!json_string(j,json_get(j,0,"source"),source,sizeof(source))||!object_name_valid(source)))error="source requires a valid name";
    if(update){
        bool valid=json_u32(j,json_get(j,0,"assignment"),&assignment)&&json_f64(j,json_get(j,0,"progress"),&value);
        bool accepted=!error&&valid&&d->transition&&transition_source(d->transition,source,assignment,value);
        reply(a,id,STATUS_OK,accepted?1:0,accepted?"source value applied":"source value unavailable, invalid or no longer assigned");return true;
    }
    if(select||swap){
        uint32_t revision;if(!json_u32(j,json_get(j,0,"expected_revision"),&revision)||revision!=d->composition_revision||revision==UINT32_MAX){error_reply(a,id,STATUS_CONFIG,"stale_revision","expected composition revision must match");return true;}
        if(swap){
            if(d->transition)transition_update(d->transition,platform_time_us());
            if(!d->transition||(d->transition->progress!=0&&d->transition->progress!=100))error="swap requires an A/B endpoint";
        }
    }
    if(select){
        bool named=json_get(j,0,"program")>=0,found=named;
        if(named){effect=TRANSITION_PROGRAM;if(json_get(j,0,"effect")>=0||json_get(j,0,"mask")>=0||json_get(j,0,"paint")>=0)error="program cannot be combined with legacy effect, mask or paint";}
        else for(unsigned n=0;n<TRANSITION_PROGRAM;n++)if(json_equal(j,json_get(j,0,"effect"),transition_effect_name((TransitionEffect)n))){effect=(TransitionEffect)n;found=true;}
        if(!found)error="unknown transition effect";
        if(!named&&json_get(j,0,"settings")>=0)error="settings require a named transition program";
        int token=json_get(j,0,"seed");if(token>=0&&!json_u32(j,token,&seed))error="seed must be an unsigned integer";
        if(!error&&named)error=prepare_transition_program(a,&d->pixels,j,0,seed,true,&programs[0]);
        else if(!error&&effect==TRANSITION_CUSTOM){
            for(unsigned n=0;n<2&&!error;n++){token=json_get(j,0,n?"paint":"mask");if(token>=0)error=prepare_transition_program(a,&d->pixels,j,token,seed,false,&programs[n]);}
        }else if(json_get(j,0,"mask")>=0||json_get(j,0,"paint")>=0)error="mask and paint require the custom effect";
    }
    if(!error&&(swap||set||run||assign||hold||bind)&&a->transition_sequence==UINT32_MAX)error="driver assignment counter exhausted";
    if(!error)error=ensure_transition(a,d);
    if(!error){Transition *t=d->transition;
        if(swap||set||run||assign||hold||bind)t->assignment=a->transition_sequence;
        d->recall_owned=false;
        if(set||run||assign||update||bind)d->power_on=true;
        if(swap){transition_invalidate(t);swap_compositions(d);(void)transition_set(t,t->progress);++d->composition_revision;}
        else if(select){transition_invalidate(t);for(unsigned n=0;n<2;n++){transition_program_destroy(t->programs[n]);t->programs[n]=programs[n];programs[n]=NULL;}t->effect=effect;t->seed=seed;++d->composition_revision;}
        else if(set&&!transition_set(t,value))error="driver assignment counter exhausted";
        else if(run&&!transition_run_interval(t,value,duration,interval))error="driver assignment counter exhausted";
        else if(assign&&!transition_assign(t,source))error="driver assignment counter exhausted";
        else if(hold)transition_hold(t);
        else if(bind)error=transition_bind(t,&a->inputs,j,json_get(j,0,"binding"));
        if(!error&&(swap||set||run||assign||hold||bind))a->transition_sequence=t->assignment;
    }
    for(unsigned n=0;n<2;n++)transition_program_destroy(programs[n]);
    if(error)error_reply(a,id,STATUS_CONFIG,"invalid_transition",error);
    else{Writer w={.app=a,.id=id};transition_state(&w,d);flush(&w);reply(a,id,STATUS_OK,0,"transition updated");}
    return true;
}
/* One saved preset invocation supplies selection, power and startup transitions.
 * OFF needs only that recipe's transition, without allocating its animation layers. */
static const char *prepare_preset_recall(App *a,AppOutput *d,const char *name,bool on,Composition **candidate,TransitionProgram **program,double *duration,uint32_t *crc,const LightControls *light){
    if(!on&&!name[0])return NULL;
    const StoreEntry *entry=store_find(&a->store,STORE_PRESET,name);
    if(!entry)return "stored preset does not exist";
    uint32_t size=entry->size;*crc=entry->crc;
    uint8_t *data=platform_calloc(1,size);TransitionSpec spec={0};
    const char *error=!data||!store_read(&a->store,entry,0,data,size)?"preset read failed":NULL;
    if(!error&&on){
        error=prepare_composition_data(a,d,candidate,data,size,"lightvm.preset",&spec,light);
        if(!error){strcpy((*candidate)->preset,name);(*candidate)->preset_crc=*crc;}
    }else if(!error){
        Json *j=platform_calloc(1,sizeof(*j));char message[160];
        static const char *const fields[]={"schema","layers","entry_transition",NULL};
        if(!j)error="preset parser allocation failed";
        else if(!json_parse(j,data,size)||!json_fields(j,0,fields)||!json_equal(j,json_get(j,0,"schema"),"lightvm.preset")||
                !transition_spec_parse(j,json_get(j,0,"entry_transition"),&spec,message,sizeof(message)))error="invalid preset transition";
        platform_free(j);
    }
    if(!error)error=prepare_recall_transition(a,&d->pixels,data,&spec,program);
    *duration=spec.duration_ms;platform_free(data);return error;
}
static const char *validate_startup(App *a,const HardwareOutput *output){
    if(output->startup_mode==STARTUP_RESTORE_LAST&&!store_info(&a->store).writable)return "restore_last requires writable storage";
    if(output->startup_mode!=STARTUP_PRESET)return NULL;
    AppOutput *d=platform_calloc(1,sizeof(*d));if(!d)return "startup allocation failed";
    d->hardware=*output;d->pixels.count=output->pixels.count;d->pixels.light=(LightControls){.color=0xffffff,.brightness=1};
    Composition *candidate=NULL;TransitionProgram *program=NULL;double duration=0;uint32_t crc=0;
    const char *error=prepare_preset_recall(a,d,output->startup_preset,true,&candidate,&program,&duration,&crc,NULL);
    composition_destroy(candidate);transition_program_destroy(program);platform_free(d);return error;
}
static void start_output(App *a,AppOutput *d){
    if(d->hardware.startup_mode==STARTUP_OFF)return;
    char name[OBJECT_NAME_LIMIT+1]="";bool on=true;const char *error=NULL;
    if(d->hardware.startup_mode==STARTUP_PRESET)strcpy(name,d->hardware.startup_preset);
    else{
        const StoreEntry *entry=store_find(&a->store,STORE_OUTPUT_STATE,d->hardware.id);
        if(!entry)return; /* No previous command: stay off. */
        uint8_t state[OUTPUT_STATE_BYTES];
        if(entry->size!=sizeof(state)||!store_read(&a->store,entry,0,state,sizeof(state))||state[0]>1||!memchr(state+OUTPUT_STATE_NAME,0,OBJECT_NAME_LIMIT+1))error="invalid saved output state";
        else{
            LightControls light={.color=read_u32(state+5)};uint32_t brightness=read_u32(state+1);memcpy(&light.brightness,&brightness,sizeof(brightness));
            if(!isfinite(light.brightness)||light.brightness<0||light.brightness>1||light.color>0xffffff)error="invalid saved light controls";
            else d->pixels.light=light;
            on=state[0]!=0;memcpy(name,state+OUTPUT_STATE_NAME,sizeof(name));if((on||name[0])&&!object_name_valid(name))error="invalid saved preset name";
        }
    }
    Composition *candidate=NULL;TransitionProgram *program=NULL;double duration=0;uint32_t crc=0;
    if(!error&&on){
        error=prepare_preset_recall(a,d,name,true,&candidate,&program,&duration,&crc,NULL);
        if(!error)error=begin_recall(a,d,candidate,program,duration,true,name,candidate->preset_crc,NULL);
    }else if(!error){
        const StoreEntry *entry=store_find(&a->store,STORE_PRESET,name);
        if(name[0]&&!entry)error="stored preset does not exist";
        else{d->recall_owned=true;d->power_on=false;strcpy(d->selected_preset,name);d->selected_preset_crc=entry?entry->crc:0;}
    }
    if(error){composition_destroy(candidate);transition_program_destroy(program);startup_error(a->boot_error,sizeof(a->boot_error),&d->hardware,error);}
}
static bool recall_manage(App *a,AppOutput *d,uint16_t id,const Json *j,const char *op){
    bool power=!strcmp(op,"output_power"),recall=!strcmp(op,"preset_recall");
    if(!power&&!recall)return false;
    static const char *const fields[]={"op","output","name","action","expected_revision",NULL};
    uint32_t revision;bool on=true;char name[OBJECT_NAME_LIMIT+1]="";
    if(!json_fields(j,0,fields)||!d->pixels.count||
       !json_u32(j,json_get(j,0,"expected_revision"),&revision)||revision!=d->composition_revision){
        error_reply(a,id,STATUS_CONFIG,"stale_revision","valid output and current composition revision required");return true;
    }
    if(power){
        if(!json_equal(j,json_get(j,0,"action"),"on")&&!json_equal(j,json_get(j,0,"action"),"off")){
            error_reply(a,id,STATUS_CONFIG,"invalid_power","power action must be on or off");return true;
        }
        on=json_equal(j,json_get(j,0,"action"),"on");
        const char *selected=app_output_selected_preset(a,d);
        if(on==app_output_power(d)){
            const char *error=(!on||selected)?save_output_state(a,d,on,selected,&d->pixels.light):NULL;
            if(error)error_reply(a,id,STATUS_STORAGE,"state_save_failed",error);
            else reply(a,id,STATUS_OK,(int32_t)revision,"power target unchanged");
            return true;
        }
        if(!selected&&d->transition&&d->transition->progress>0)selected=composition_preset(a,d->transition->compositions[1]);
        if(selected)snprintf(name,sizeof(name),"%s",selected);
        if(on){
            int token=json_get(j,0,"name");
            if(token>=0){if(!json_string(j,token,name,sizeof(name))||!object_name_valid(name)){error_reply(a,id,STATUS_CONFIG,"invalid_name","invalid power-on preset name");return true;}}
            else if(!name[0])snprintf(name,sizeof(name),"%s",d->hardware.ha_default_preset);
        }
    }else{
        if(json_get(j,0,"action")>=0||!json_string(j,json_get(j,0,"name"),name,sizeof(name))||!object_name_valid(name)){
            error_reply(a,id,STATUS_CONFIG,"invalid_name","recall requires a stored preset name");return true;
        }
        const char *selected=app_output_selected_preset(a,d);
        if(app_output_power(d)&&selected&&!strcmp(selected,name)){
            const char *error=save_output_state(a,d,true,name,&d->pixels.light);
            if(error)error_reply(a,id,STATUS_STORAGE,"state_save_failed",error);
            else reply(a,id,STATUS_OK,(int32_t)revision,"preset target unchanged");
            return true;
        }
    }
    Composition *candidate=NULL;TransitionProgram *program=NULL;
    const char *error=NULL;uint32_t crc=0;double duration=0;
    error=prepare_preset_recall(a,d,name,on,&candidate,&program,&duration,&crc,NULL);
    if(!error)error=begin_recall(a,d,candidate,program,duration,on,name,crc,NULL);
    if(error){composition_destroy(candidate);transition_program_destroy(program);error_reply(a,id,STATUS_LOAD,"recall_failed",error);}
    else reply(a,id,STATUS_OK,(int32_t)d->composition_revision,"recall started");
    return true;
}
static void refresh_light_frame(AppOutput *d){
    /* Release the physical transfer, keeping every unscaled scene buffer and
       invocation intact. A paused scene can therefore be dimmed immediately. */
    vm_output_cancel(&d->vm);
    if(d->composition&&d->composition->pending){
        d->pixels.type->cancel(d->pixels.driver);d->composition->pending=false;
    }
    if(d->transition&&d->transition->pending){
        d->pixels.type->cancel(d->pixels.driver);d->transition->pending=false;
    }
    snprintf(d->light_error,sizeof(d->light_error),"%s",pixels_show(&d->pixels)?"":"native light output failed");
}
static bool light_manage(App *a,AppOutput *d,uint16_t id,const Json *j,const char *op){
    bool get=!strcmp(op,"output_light_get"),set=!strcmp(op,"output_light_set");
    if(!get&&!set)return false;
    static const char *const read_fields[]={"op","output",NULL};
    static const char *const fields[]={"op","output","expected_revision","expected_composition_revision","brightness","color","power","preset",NULL};
    if(!d->pixels.count||d->hardware.indicators||!json_fields(j,0,get?read_fields:fields)){
        error_reply(a,id,STATUS_CONFIG,"invalid_light","light controls require an animation output and valid fields");return true;
    }
    if(set){
        uint32_t revision;LightControls next=d->pixels.light;
        if(!json_u32(j,json_get(j,0,"expected_revision"),&revision)||revision!=d->light_revision||revision==UINT32_MAX){
            error_reply(a,id,STATUS_CONFIG,"stale_revision","expected light revision must match");return true;
        }
        int brightness=json_get(j,0,"brightness"),color=json_get(j,0,"color"),power=json_get(j,0,"power"),preset=json_get(j,0,"preset");
        double level;bool on=app_output_power(d);
        char name[OBJECT_NAME_LIMIT+1]="";
        const char *selected=app_output_selected_preset(a,d);
        if(selected)snprintf(name,sizeof(name),"%s",selected);
        const char *error=NULL;
        if(brightness<0&&color<0&&power<0&&preset<0)error="at least one light property is required";
        if(brightness>=0){
            if(!json_f64(j,brightness,&level)||level<0||level>1)error="brightness must be in 0..1";
            else next.brightness=(float)level;
        }
        if(color>=0&&(!json_u32(j,color,&next.color)||next.color>0xffffff))error="color must be RGB8";
        if(power>=0&&!json_bool(j,power,&on))error="power must be boolean";
        if(preset>=0){
            if(!json_string(j,preset,name,sizeof(name))||!object_name_valid(name))error="preset requires a valid name";
            if(power<0)on=true;
        }
        if(power>=0||preset>=0){
            uint32_t composition;
            if(!json_u32(j,json_get(j,0,"expected_composition_revision"),&composition)||composition!=d->composition_revision)
                error="expected composition revision must match";
        }
        bool recall=(power>=0||preset>=0)&&(on!=app_output_power(d)||(preset>=0&&(!selected||strcmp(selected,name))));
        if(recall&&on&&!name[0])snprintf(name,sizeof(name),"%s",d->hardware.ha_default_preset);
        Composition *candidate=NULL;TransitionProgram *program=NULL;double duration=0;uint32_t crc=0;
        if(!error&&recall)error=prepare_preset_recall(a,d,name,on,&candidate,&program,&duration,&crc,&next);
        if(!error&&recall)error=begin_recall(a,d,candidate,program,duration,on,name,crc,&next);
        if(error){
            composition_destroy(candidate);transition_program_destroy(program);
            error_reply(a,id,STATUS_CONFIG,"invalid_light",error);return true;
        }
        if(!recall){
            bool dimmer_changed=next.brightness!=d->pixels.light.brightness;
            d->pixels.light=next;++d->light_revision;
            d->light_save_due=platform_time_us()+750000;
            if(dimmer_changed)refresh_light_frame(d);
        }
    }
    char state[384];size_t size=app_output_light_json(a,d,state,sizeof(state),false);
    Writer w={.app=a,.id=id};bytes(&w,state,size);flush(&w);reply(a,id,STATUS_OK,0,"output light state");return true;
}
static bool preset_manage(App *a,AppOutput *d,uint16_t id,const Json *j,const char *op){
    if(strncmp(op,"preset_",7))return false;
    static const char *const fields[]={"op","name","output","slot","expected_revision",NULL};
    if(!json_fields(j,0,fields)){error_reply(a,id,STATUS_PROTOCOL,"invalid_request","invalid preset fields");return true;}
    if(!strcmp(op,"preset_list")){
        Writer w={.app=a,.id=id};literal(&w,"[");unsigned count=0;
        uint32_t cursor=0;const StoreEntry *entry;
        while((entry=store_list(&a->store,STORE_PRESET,&cursor))){
            if(count++)literal(&w,",");
            literal(&w,"{\"name\":");quoted(&w,entry->name);
            literal(&w,",\"size\":");number(&w,entry->size);literal(&w,",\"crc32\":");number(&w,entry->crc);literal(&w,"}");
        }
        literal(&w,"]");flush(&w);reply(a,id,STATUS_OK,(int32_t)count,"stored presets");return true;
    }
    char name[OBJECT_NAME_LIMIT+1];
    if(!json_string(j,json_get(j,0,"name"),name,sizeof(name))||!object_name_valid(name)){
        error_reply(a,id,STATUS_CONFIG,"invalid_name","preset name requires 1..32 letters, digits, dots, underscores or hyphens");return true;
    }
    const StoreEntry *entry=store_find(&a->store,STORE_PRESET,name);
    if(!strcmp(op,"preset_get")){if(entry)read_object(a,id,entry);else error_reply(a,id,STATUS_STORAGE,"not_found","stored preset does not exist");return true;}
    uint32_t revision;
    if(!json_u32(j,json_get(j,0,"expected_revision"),&revision)||revision!=(!strcmp(op,"preset_activate")?d->composition_revision:a->store.generation)){
        error_reply(a,id,STATUS_CONFIG,"stale_revision","expected preset catalog or composition revision must match");return true;
    }
    if(!strcmp(op,"preset_delete")){
        if(configuration_references(a,STORE_PRESET,name)){error_reply(a,id,STATUS_STORAGE,"referenced","preset is referenced by active or saved startup configuration");return true;}
        bool ok=entry&&store_delete(&a->store,STORE_PRESET,name);resume_clock(a);
        if(ok)reply(a,id,STATUS_OK,(int32_t)a->store.generation,"preset deleted; running composition retained");
        else error_reply(a,id,STATUS_STORAGE,"delete_failed","preset missing or storage write failed");
        return true;
    }
    bool saving=!strcmp(op,"preset_save");
    if(!saving&&strcmp(op,"preset_activate")){error_reply(a,id,STATUS_PROTOCOL,"unknown_operation","unknown preset operation");return true;}
    if(!d->pixels.count||(!saving&&d->composition_revision==UINT32_MAX)){error_reply(a,id,STATUS_CONFIG,"invalid_output","configure an output before preparing a preset");return true;}
    if(saving&&(!a->staged||a->object_kind!=5||a->upload_owner!=a->source||strcmp(a->object_name,name))){error_reply(a,id,STATUS_UPLOAD,"no_candidate","stage this named preset first");return true;}
    /* Read saved documents outside staging so another session's candidate is untouched. */
    uint8_t *data=NULL;const void *document=a->staging;uint32_t size=a->expected;
    if(!saving){
        if(!entry){error_reply(a,id,STATUS_STORAGE,"not_found","stored preset does not exist");return true;}
        size=entry->size;data=platform_calloc(1,size);document=data;
        if(!data||!store_read(&a->store,entry,0,data,size)){platform_free(data);error_reply(a,id,STATUS_STORAGE,"read_failed","preset read failed");return true;}
    }
    TransitionSpec entry_transition;TransitionProgram *program=NULL;
    Composition *candidate=NULL;const char *error=prepare_composition_data(a,d,&candidate,document,size,"lightvm.preset",&entry_transition,NULL);
    if(!error&&saving)error=prepare_recall_transition(a,&d->pixels,document,&entry_transition,&program);
    transition_program_destroy(program);
    if(error){composition_destroy(candidate);platform_free(data);error_reply(a,id,STATUS_LOAD,"invalid_preset",error);return true;}
    if(saving){
        composition_destroy(candidate);bool ok=store_put(&a->store,STORE_PRESET,name,document,size);resume_clock(a);
        if(ok){a->staged=false;a->upload_owner=0;reply(a,id,STATUS_OK,(int32_t)a->store.generation,"preset saved; playback unchanged");}
        else error_reply(a,id,STATUS_STORAGE,"save_failed","preset storage unavailable, full or write failed");
    }else{
        strcpy(candidate->preset,name);candidate->preset_crc=entry->crc;
        error=apply_composition_slot(a,d,candidate,request_slot(j));
        if(error){composition_destroy(candidate);error_reply(a,id,STATUS_LOAD,"preparation_failed",error);}
        else reply(a,id,STATUS_OK,(int32_t)d->composition_revision,"preset activated");
    }
    platform_free(data);return true;
}
static bool composition_manage(App *a,AppOutput *d,uint16_t id,const Json *j,const char *op){
    if(strncmp(op,"composition_",12))return false;
    static const char *const fields[]={"op","output","slot","expected_revision","expected_generation","layer","name","settings","inputs","opacity","blend",NULL};
    if(!json_fields(j,0,fields)){error_reply(a,id,STATUS_PROTOCOL,"invalid_request","invalid composition fields");return true;}
    if(!d->pixels.count){error_reply(a,id,STATUS_CONFIG,"unknown_output","configure a physical output first");return true;}
    unsigned slot=request_slot(j);Composition *s=output_composition(d,slot);
    if(!strcmp(op,"composition_get")){
        Writer w={.app=a,.id=id};composition_state_for(&w,d,s);flush(&w);reply(a,id,STATUS_OK,0,"composition state");return true;
    }
    if(!strcmp(op,"composition_snapshot")){
        const uint8_t *frame=d->pixels.rgb;
        if(d->transition&&json_get(j,0,"slot")>=0){if(!s){error_reply(a,id,STATUS_CONFIG,"unknown_slot","composition slot is empty");return true;}frame=s->front;}
        for(size_t offset=0;offset<(size_t)d->pixels.count*3;offset+=FRAME_LIMIT){size_t n=(size_t)d->pixels.count*3-offset;if(n>FRAME_LIMIT)n=FRAME_LIMIT;a->send(a->transport,RESP_PIXELS,id,frame+offset,n);}
        Writer w={.app=a,.id=id};composition_state_for(&w,d,s);flush(&w);reply(a,id,STATUS_OK,0,"last completed composition frame");return true;
    }
    uint32_t revision;
    if(!json_u32(j,json_get(j,0,"expected_revision"),&revision)||revision!=d->composition_revision||revision==UINT32_MAX){error_reply(a,id,STATUS_CONFIG,"stale_revision","expected composition revision must match");return true;}
    if(!strcmp(op,"composition_apply")){
        if(!a->staged||a->object_kind!=4||a->upload_owner!=a->source){error_reply(a,id,STATUS_CONFIG,"no_candidate","upload a composition document first");return true;}
        Composition *candidate=NULL;const char *error=prepare_composition_data(a,d,&candidate,a->staging,a->expected,"lightvm.composition",NULL,NULL);
        if(error){composition_destroy(candidate);error_reply(a,id,STATUS_LOAD,"composition_preparation_failed",error);return true;}
        error=apply_composition_slot(a,d,candidate,slot);
        if(error){composition_destroy(candidate);error_reply(a,id,STATUS_LOAD,"composition_preparation_failed",error);return true;}
        a->staged=false;a->upload_owner=0;reply(a,id,STATUS_OK,(int32_t)d->composition_revision,"composition applied in RAM");return true;
    }
    if(!strcmp(op,"composition_clear")){
        if(d->transition){transition_invalidate(d->transition);d->transition->compositions[slot]=NULL;if(!slot)d->composition=NULL;composition_destroy(s);}
        else if(!slot){clear_output_composition(d);d->vm.output=&d->pixels;pixels_clear(&d->pixels);pixels_show(&d->pixels);d->power_on=false;}
        ++d->composition_revision;reply(a,id,STATUS_OK,(int32_t)d->composition_revision,"composition removed");return true;
    }
    if(!strcmp(op,"composition_pause")||!strcmp(op,"composition_resume")){
        if(!s){error_reply(a,id,STATUS_CONFIG,"unknown_slot","composition is not configured");return true;}
        composition_play(s,!strcmp(op,"composition_resume"));s->revision=++d->composition_revision;
        if(!strcmp(op,"composition_resume"))d->power_on=true;
        reply(a,id,STATUS_OK,(int32_t)d->composition_revision,"composition playback updated");return true;
    }
    char layer_id[OBJECT_NAME_LIMIT+1];
    if(!s||!json_string(j,json_get(j,0,"layer"),layer_id,sizeof(layer_id))){error_reply(a,id,STATUS_CONFIG,"unknown_layer","composition layer is required");return true;}
    CompositionLayer *layer=composition_layer(s,layer_id);
    if(!layer){error_reply(a,id,STATUS_CONFIG,"unknown_layer","composition layer is not configured");return true;}
    if(!strcmp(op,"composition_layer_blend")){
        uint8_t blend;
        if(!layer_blend(j,json_get(j,0,"blend"),&blend)){error_reply(a,id,STATUS_CONFIG,"invalid_blend","layer blend must be over, add, screen or multiply");return true;}
        transition_invalidate(d->transition);composition_invalidate(s);layer->blend=blend;composition_modified(s);s->revision=++d->composition_revision;
        reply(a,id,STATUS_OK,(int32_t)d->composition_revision,"layer blend updated");return true;
    }
    if(!strcmp(op,"composition_layer_opacity")){
        uint32_t opacity;if(!json_u32(j,json_get(j,0,"opacity"),&opacity)||opacity>255){error_reply(a,id,STATUS_CONFIG,"invalid_opacity","layer opacity must be 0..255");return true;}
        transition_invalidate(d->transition);composition_invalidate(s);layer->opacity=(uint8_t)opacity;composition_modified(s);s->revision=++d->composition_revision;
        reply(a,id,STATUS_OK,(int32_t)d->composition_revision,"layer opacity updated");return true;
    }
    if(!strcmp(op,"composition_layer_remove")){
        transition_invalidate(d->transition);composition_remove_layer(s,(unsigned)(layer-s->layers));composition_modified(s);s->revision=++d->composition_revision;
        reply(a,id,STATUS_OK,(int32_t)d->composition_revision,"layer removed");return true;
    }
    if(!strcmp(op,"composition_layer_activate")){
        uint32_t generation;char name[OBJECT_NAME_LIMIT+1];
        if(!json_u32(j,json_get(j,0,"expected_generation"),&generation)||generation!=layer->vm.generation){error_reply(a,id,STATUS_CONFIG,"stale_generation","expected layer generation must match");return true;}
        if(!json_string(j,json_get(j,0,"name"),name,sizeof(name))||!object_name_valid(name)){error_reply(a,id,STATUS_CONFIG,"invalid_name","program name required");return true;}
        const StoreEntry *entry=store_find(&a->store,STORE_ANIMATION,name);
        CompositionLayer *candidate=demo_calloc(1,sizeof(*candidate));
        if(!entry||!candidate||d->layer_generation==UINT32_MAX){demo_free(candidate);error_reply(a,id,STATUS_LOAD,"preparation_failed","program missing or peak budget exceeded");return true;}
        candidate->target.count=s->count;vm_init(&candidate->vm,&candidate->target,vm_send,a);candidate->vm.generation=++d->layer_generation;
        strcpy(candidate->vm.output_id,d->hardware.id);strcpy(candidate->vm.layer,layer->id);strcpy(candidate->vm.slot,slot?"B":"A");
        const char *error=prepare_instance(a,&candidate->vm,entry,j,json_get(j,0,"settings"),json_get(j,0,"inputs"),&d->pixels.light);
        if(error){control_destroy(&candidate->vm);demo_free(candidate);error_reply(a,id,STATUS_LOAD,"preparation_failed",error);return true;}
        transition_invalidate(d->transition);composition_invalidate(s);control_destroy(&layer->vm);layer->vm=candidate->vm;layer->vm.target=&layer->target;vm_rebind(&layer->vm);
        layer->vm.playing=true;layer->vm.last_frame=platform_time_us();layer->vm.next_frame=layer->vm.last_frame;
        strcpy(layer->program,name);layer->crc=entry->crc;demo_free(candidate);composition_modified(s);s->revision=++d->composition_revision;
        reply(a,id,STATUS_OK,(int32_t)d->composition_revision,"layer replaced; other layers retained");return true;
    }
    error_reply(a,id,STATUS_PROTOCOL,"unknown_operation","unknown composition operation");return true;
}
static bool composition_output_command(App *a,AppOutput *d,uint8_t command,uint16_t id,uint32_t delta){
    Transition *t=d->transition;
    if(t){
        if(command==CMD_FRAME){const char *error=transition_frame(t,delta);if(error){reply(a,id,STATUS_EXECUTION,0,error);return true;}
            output_command_power(d,command);
            for(size_t offset=0;offset<(size_t)d->pixels.count*3;offset+=FRAME_LIMIT){size_t n=(size_t)d->pixels.count*3-offset;if(n>FRAME_LIMIT)n=FRAME_LIMIT;a->send(a->transport,RESP_PIXELS,id,d->pixels.rgb+offset,n);}reply(a,id,STATUS_OK,(int32_t)d->pixels.count,"transition frame rendered");return true;}
        if(command==CMD_PLAY||command==CMD_STOP){output_command_power(d,command);d->recall_owned=false;transition_play(t,command==CMD_PLAY);if(command==CMD_STOP){transition_invalidate(t);if(t->pending){d->pixels.type->cancel(d->pixels.driver);t->pending=false;}pixels_clear(&d->pixels);pixels_show(&d->pixels);}reply(a,id,STATUS_OK,0,command==CMD_PLAY?"A/B resumed":"A/B stopped");return true;}
        if(command==CMD_RESET){output_command_power(d,command);clear_output_composition(d);d->vm.output=&d->pixels;++d->composition_revision;pixels_clear(&d->pixels);pixels_show(&d->pixels);reply(a,id,STATUS_OK,0,"A/B unloaded");return true;}
        if(command==CMD_STATUS){char message[FRAME_LIMIT-8];snprintf(message,sizeof(message),"version=" LIGHTVM_VERSION " code_storage=flash module_limit=%u heap=%lu heap_peak=%lu heap_limit=%u pixels=%lu mode=transition frames=%lu animation_error=%.99s",MODULE_LIMIT,(unsigned long)vm_heap_used(),(unsigned long)vm_heap_peak(),VM_HEAP_LIMIT,(unsigned long)d->pixels.count,(unsigned long)t->frames,t->error[0]?t->error:"-");reply(a,id,STATUS_OK,0,message);return true;}
        return false;
    }
    Composition *s=d->composition;if(!s)return false;
    if(command==CMD_STATUS){
        char message[FRAME_LIMIT-8];const PlatformInfo *info=platform_info();
        snprintf(message,sizeof(message),"version=" LIGHTVM_VERSION " protocol=1 platform=%.16s board=%.24s output=ws2812 ready=1 uploading=%u module_limit=%u heap=%lu heap_peak=%lu heap_limit=%u pixels=%lu frame_interval_us=%u mode=composition frames=%lu layers=%u animation_error=%.99s",info->platform,info->board,d->vm.uploading,MODULE_LIMIT,(unsigned long)vm_heap_used(),(unsigned long)vm_heap_peak(),VM_HEAP_LIMIT,(unsigned long)s->count,FRAME_INTERVAL_US,(unsigned long)s->frames,s->layer_count,s->error[0]?s->error:"-");
        reply(a,id,STATUS_OK,0,message);return true;
    }
    if(command==CMD_FRAME){
        const char *error=composition_frame(s,delta);if(error){reply(a,id,STATUS_EXECUTION,0,error);return true;}
        output_command_power(d,command);
        for(size_t offset=0;offset<(size_t)s->count*3;offset+=FRAME_LIMIT){size_t n=(size_t)s->count*3-offset;if(n>FRAME_LIMIT)n=FRAME_LIMIT;a->send(a->transport,RESP_PIXELS,id,s->output->rgb+offset,n);}
        reply(a,id,STATUS_OK,(int32_t)s->count,"composition frame rendered");return true;
    }
    if(command==CMD_PLAY||command==CMD_STOP){output_command_power(d,command);
        if(command==CMD_STOP)composition_invalidate(s);
        composition_play(s,command==CMD_PLAY);
        if(command==CMD_STOP){if(s->pending){s->output->type->cancel(s->output->driver);s->pending=false;}pixels_clear(s->output);pixels_show(s->output);}
        reply(a,id,STATUS_OK,0,command==CMD_PLAY?"composition resumed":"composition stopped");return true;
    }
    if(command==CMD_RESET){output_command_power(d,command);clear_output_composition(d);d->vm.output=&d->pixels;++d->composition_revision;pixels_clear(&d->pixels);pixels_show(&d->pixels);reply(a,id,STATUS_OK,0,"composition unloaded");return true;}
    return false;
}
static void manage(App *a,uint16_t id,const uint8_t *p,size_t size) {
    Arguments args;
    if (!arguments(p,size,&args)) { error_reply(a,id,STATUS_PROTOCOL,"invalid_request","invalid management JSON or fields"); return; }
    if(!strcmp(args.op,"output_command")){
        AppOutput *d=app_output(a,args.output);
        if(!d){error_reply(a,id,STATUS_CONFIG,"unknown_output","output is not configured");return;}
        if(args.command!=CMD_STATUS&&args.command!=CMD_PLAY&&args.command!=CMD_STOP&&args.command!=CMD_FRAME&&args.command!=CMD_RESET){error_reply(a,id,STATUS_PROTOCOL,"invalid_command","unsupported output command");return;}
        if(args.delta_ms>1000){error_reply(a,id,STATUS_PROTOCOL,"invalid_delta","FRAME requires 0..1000 milliseconds");return;}
        if(composition_output_command(a,d,(uint8_t)args.command,id,args.delta_ms))return;
        if(args.command==CMD_RESET){
            d->active_program[0]=0;d->active_crc=0;
            if(a->upload_owner&&a->upload_output==d&&d->vm.uploading){store_upload_cancel(&a->upload);a->upload_owner=0;d->vm.uploading=false;}
        }
        uint8_t delta[4];write_u32(delta,args.delta_ms);
        vm_command(&d->vm,(uint8_t)args.command,id,args.command==CMD_FRAME?delta:NULL,args.command==CMD_FRAME?4:0);
        if(a->last_code==STATUS_OK)output_command_power(d,(uint8_t)args.command);
        return;
    }
    if (!strcmp(args.op,"session_close")) { app_disconnect(a,a->source); reply(a,id,STATUS_OK,0,"session closed; playback retained"); return; }
    if (!strcmp(args.op,"describe")) { describe(a,id); return; }
    if (!strcmp(args.op,"network_get")) {
        char data[4096]; size_t count=network_json(a->network_pending?&a->pending_network:&a->network.config,false,data,sizeof(data));
        Writer w={.app=a,.id=id}; bytes(&w,data,count); flush(&w);
        reply(a,id,STATUS_OK,(int32_t)a->store.network_revision,"network configuration; passwords omitted"); return;
    }
    if (!strcmp(args.op,"network_status")) {
        Network *n=&a->network; Writer w={.app=a,.id=id};
        literal(&w,"{\"state\":"); quoted(&w,a->network_pending?"reconfiguring":network_state_name(n->state));
        literal(&w,",\"mode\":"); quoted(&w,network_mode(n)==NET_MODE_AP?"ap":"station");
        literal(&w,",\"initial_setup\":"); literal(&w,n->initial_setup?"true":"false");
        literal(&w,",\"recovery\":"); literal(&w,n->recovery?"true":"false");
        literal(&w,",\"ssid\":"); quoted(&w,n->state==NET_AP_ACTIVE?n->ap_ssid:n->state==NET_CONNECTED && n->connected_index>=0?n->config.networks[n->connected_index].ssid:"");
        literal(&w,",\"ip\":"); quoted(&w,n->ip);
        literal(&w,",\"error\":"); quoted(&w,a->network_error[0]?a->network_error:n->error);
        literal(&w,",\"retry_ms\":"); uint64_t now=platform_time_us(); number(&w,n->state==NET_BACKOFF && n->retry_at>now?(uint32_t)((n->retry_at-now)/1000u):0);
        literal(&w,",\"revision\":"); number(&w,a->store.network_revision); literal(&w,",\"http_port\":"); number(&w,network_http_port(n));
        literal(&w,",\"mqtt_connected\":");literal(&w,a->mqtt_connected?"true":"false");
        literal(&w,",\"mqtt_error\":");quoted(&w,a->mqtt_error);
        literal(&w,"}"); flush(&w); reply(a,id,STATUS_OK,0,"network status"); return;
    }
    if (!strcmp(args.op,"network_validate") || !strcmp(args.op,"network_commit")) {
        if (!a->staged || a->object_kind!=3 || a->upload_owner!=a->source) { error_reply(a,id,STATUS_UPLOAD,"no_candidate","stage a network configuration in this session first"); return; }
        NetworkConfig config; char error[160];
        if (!network_parse(a->staging,a->expected,&a->network.config,&config,error,sizeof(error)) ||
            (config.wifi_enabled && !LIGHTVM_WIFI) || (config.http_enabled && !LIGHTVM_HTTP) || (config.mqtt.enabled && !LIGHTVM_MQTT) ||
            ((config.http_enabled || config.mqtt.enabled) && !config.wifi_enabled && strcmp(platform_info()->platform,"host"))) {
            error_reply(a,id,STATUS_CONFIG,"invalid_network",error[0]?error:"selected network feature is unavailable or HTTP/MQTT requires Wi-Fi"); return;
        }
        if (!strcmp(args.op,"network_validate")) { reply(a,id,STATUS_OK,0,"network configuration valid"); return; }
        if (!args.has_revision || args.revision!=a->store.network_revision || a->network_pending) { error_reply(a,id,STATUS_CONFIG,"stale_revision","network revision must match; wait for pending changes"); return; }
        char data[4096]; size_t count=network_json(&config,true,data,sizeof(data));
        bool ok=count && update_store(a,STORE_NETWORK_SETTINGS,STORE_NETWORK,data,(uint32_t)count,false); resume_clock(a);
        memset(data,0,sizeof(data));
        if (!ok) { error_reply(a,id,STATUS_STORAGE,"save_failed","network save failed; active settings retained"); return; }
        a->network.saved_valid=true; a->network_error[0]=0;
        if (a->network.recovery) a->network.config=config;
        else { a->pending_network=config; a->network_pending=true; a->network_due=platform_time_us()+1000000u; a->network_reply_source=a->source; }
        a->staged=false; a->upload_owner=0;
        reply(a,id,STATUS_OK,(int32_t)a->store.network_revision,a->network.recovery?"network saved for next boot; recovery AP retained":"network saved; reconnect after settings apply"); return;
    }
    if (!strcmp(args.op,"config_get")) {
        Writer w={.app=a,.id=id}; bytes(&w,a->config_json,a->config_size); flush(&w); reply(a,id,STATUS_OK,(int32_t)a->revision,"active configuration"); return;
    }
    if (!strcmp(args.op,"config_saved")) {
        const StoreEntry *entry=store_find(&a->store,STORE_CONFIGURATION,STORE_CONFIG);
        if (entry) read_object(a,id,entry);
        else { Writer w={.app=a,.id=id}; literal(&w,EMPTY_CONFIG); flush(&w); reply(a,id,STATUS_OK,0,"no saved configuration"); }
        return;
    }
    if (!strcmp(args.op,"program_list")) {
        Writer w={.app=a,.id=id};literal(&w,"[");unsigned found=0;uint32_t cursor=0;const StoreEntry *entry;
        while((entry=store_list(&a->store,args.type,&cursor))){
            if(entry->type!=STORE_ANIMATION&&entry->type!=STORE_TRANSITION)continue;
            if(found++)literal(&w,",");
            literal(&w,"{\"name\":");quoted(&w,entry->name);
            literal(&w,",\"size\":");number(&w,entry->size);literal(&w,",\"crc32\":");number(&w,entry->crc);
            literal(&w,",\"type\":");quoted(&w,store_type_name(entry->type));
            literal(&w,",\"profile\":");quoted(&w,store_type_name(entry->type));literal(&w,"}");
        }
        literal(&w,"]");flush(&w);reply(a,id,STATUS_OK,(int32_t)found,"stored programs");return;
    }
    if (!strcmp(args.op,"program_get")) {
        const StoreEntry *entry=find_program(&a->store,args.type,args.name);
        if(!entry&&args.type==STORE_ANY&&store_find(&a->store,STORE_ANIMATION,args.name)&&store_find(&a->store,STORE_TRANSITION,args.name))error_reply(a,id,STATUS_STORAGE,"ambiguous_name","specify animation or transition type");
        else if(!entry)error_reply(a,id,STATUS_STORAGE,"not_found","stored program does not exist");else read_object(a,id,entry);
        return;
    }
    if (!strcmp(args.op,"program_activate") && a->upload_output->vm.uploading) { error_reply(a,id,STATUS_BUSY,"upload_busy","finish or cancel the transient upload first"); return; }
    if (!strcmp(args.op,"program_activate")) { Json request;if(!json_parse(&request,p,size))return;(void)activate(a,args.name,id,args.action,args.output,&request,json_get(&request,0,"settings")); return; }
    if (!strcmp(args.op,"config_validate") || !strcmp(args.op,"config_apply")) {
        if (!a->staged || a->object_kind!=1 || a->upload_owner!=a->source) { error_reply(a,id,STATUS_CONFIG,"no_candidate","upload and finish a configuration first"); return; }
        HardwareConfig config; char error[160];
        if (!config_validate(a,a->staging,a->expected,&config,error,sizeof(error))) { error_reply(a,id,STATUS_CONFIG,"invalid_config",error); return; }
        if (!strcmp(args.op,"config_validate")) { hardware_destroy(&config); reply(a,id,STATUS_OK,0,"configuration valid"); return; }
        if (!args.has_revision || args.revision!=a->revision || a->revision==UINT32_MAX) { hardware_destroy(&config); error_reply(a,id,STATUS_CONFIG,"stale_revision","expected active revision is required and must match"); return; }
        if (!config_apply(a,&config,a->staging,a->expected)) { error_reply(a,id,STATUS_CONFIG,"initialization_failed","native output initialization failed; active configuration is empty"); return; }
        a->boot_error[0]=0; a->staged=false; a->upload_owner=0;
        reply(a,id,STATUS_OK,(int32_t)a->revision,"configuration applied in RAM; unchanged outputs retained"); return;
    }
    if (!strcmp(args.op,"config_save")) {
        if (!args.has_revision || args.revision!=a->revision || !args.has_saved_revision || args.saved_revision!=a->store.config_revision) {
            error_reply(a,id,STATUS_CONFIG,"stale_revision","expected active and saved revisions are required and must match"); return;
        }
        HardwareConfig config; char error[160];
        if (!config_validate(a,a->config_json,a->config_size,&config,error,sizeof(error))) { error_reply(a,id,STATUS_CONFIG,"invalid_config",error); return; }
        hardware_destroy(&config);
        /* Choosing restore-last while a startup preset is running captures that
         * target too. Recovery and temporary editor playback do not replace it. */
        for(unsigned i=0;!a->network.recovery&&i<a->output_count;i++){
            AppOutput *d=a->outputs[i];
            if(d->hardware.startup_mode!=STARTUP_RESTORE_LAST||!d->recall_owned)continue;
            const char *selected=app_output_selected_preset(a,d);
            if(!selected)continue;
            const char *failure=save_output_state(a,d,d->power_on,selected,&d->pixels.light);
            if(failure){error_reply(a,id,STATUS_STORAGE,"state_save_failed",failure);return;}
        }
        bool ok=store_put_config(&a->store,a->config_json,a->config_size); resume_clock(a);
        if (!ok) error_reply(a,id,STATUS_STORAGE,"save_failed","storage unavailable, full or write failed");
        else reply(a,id,STATUS_OK,(int32_t)a->store.config_revision,"configuration saved");
        return;
    }
    if (!strcmp(args.op,"program_save") || !strcmp(args.op,"program_delete")) {
        if (!args.has_revision || args.revision!=a->store.generation) { error_reply(a,id,STATUS_STORAGE,"stale_revision","expected catalog revision is required and must match"); return; }
        bool ok;
        if (!strcmp(args.op,"program_save")) {
            if (!a->staged || a->object_kind!=2 || a->upload_owner!=a->source || strcmp(args.name,a->object_name)) { error_reply(a,id,STATUS_UPLOAD,"no_candidate","upload and finish this named program first"); return; }
            if(args.type!=STORE_ANY&&args.type!=a->upload.entry.type){error_reply(a,id,STATUS_UPLOAD,"type_mismatch","type differs from uploaded program profile");return;}
            const StoreEntry *old=store_find(&a->store,a->upload.entry.type,args.name);
            if(old&&(old->crc!=a->upload.entry.crc||old->size!=a->upload.entry.size)&&preset_references(a,a->upload.entry.type,args.name)){error_reply(a,id,STATUS_STORAGE,"referenced","saved presets pin this program version; save a new program name");return;}
            ok=store_publish(&a->store,&a->upload.entry);
            if (ok) { a->staged=false; a->upload_owner=0; store_upload_cancel(&a->upload); }
        } else {
            const StoreEntry *entry=find_program(&a->store,args.type,args.name);
            if(!entry){
                bool ambiguous=args.type==STORE_ANY&&store_find(&a->store,STORE_ANIMATION,args.name)&&store_find(&a->store,STORE_TRANSITION,args.name);
                error_reply(a,id,STATUS_STORAGE,ambiguous?"ambiguous_name":"not_found",ambiguous?"specify animation or transition type":"stored program does not exist");return;
            }
            args.type=entry->type;
            if (referenced(a,args.type,args.name)) { error_reply(a,id,STATUS_STORAGE,"referenced","program is referenced by a saved preset, active/saved configuration or runtime"); return; }
            ok=update_store(a,args.type,args.name,NULL,0,true);
        }
        resume_clock(a);
        if (!ok) error_reply(a,id,STATUS_STORAGE,"save_failed","storage unavailable, full or write failed");
        else reply(a,id,STATUS_OK,(int32_t)a->store.generation,"program catalog updated");
        return;
    }
    error_reply(a,id,STATUS_PROTOCOL,"unknown_operation","unknown management operation");
}
static bool object_begin(App *a,const uint8_t *p,size_t size) {
    Json j; uint32_t expected,crc; char kind[16],name[33]="";
    static const char *const fields[]={"kind","name","size","crc32",NULL};
    if (!json_parse(&j,p,size) || !json_fields(&j,0,fields) ||
        !json_string(&j,json_get(&j,0,"kind"),kind,sizeof(kind)) ||
        !json_u32(&j,json_get(&j,0,"size"),&expected) || !json_u32(&j,json_get(&j,0,"crc32"),&crc)) return false;
    unsigned object_kind=!strcmp(kind,"config") ? 1u : !strcmp(kind,"program") ? 2u : !strcmp(kind,"network") ? 3u : !strcmp(kind,"composition") ? 4u : !strcmp(kind,"preset") ? 5u : 0u;
    if (!object_kind || expected<(object_kind==2 ? 8u : 1u) || expected>(object_kind==2 ? MODULE_LIMIT : CONFIG_LIMIT)) return false;
    int n=json_get(&j,0,"name");
    if ((object_kind==2||object_kind==5) && (!json_string(&j,n,name,sizeof(name)) || !object_name_valid(name))) return false;
    if (object_kind!=2 && object_kind!=5 && n>=0) return false;
    if(object_kind==2&&!store_upload_begin(&a->store,&a->upload,STORE_ANIMATION,name,expected,crc))return false;
    if(object_kind!=2)store_upload_cancel(&a->upload);
    a->object_kind=object_kind; a->expected=expected; a->expected_crc=crc; a->received=0;
    memcpy(a->object_name,name,sizeof(name)); a->uploading=true; a->staged=false;
    a->upload_deadline=platform_time_us()+30000000u; return true;
}
static void command_impl(void *context,uint8_t type,uint16_t id,const uint8_t *p,size_t size) {
    App *a=context;
    if (a->upload_owner && a->upload_owner!=a->source && (type==CMD_BEGIN || type==CMD_OBJECT_BEGIN || type==CMD_CHUNK || type==CMD_FINISH || type==CMD_OBJECT_CHUNK || type==CMD_OBJECT_FINISH || type==CMD_OBJECT_CANCEL)) {
        error_reply(a,id,STATUS_BUSY,"upload_busy","upload belongs to another session"); return;
    }
    if (type==CMD_OBJECT_BEGIN) {
        if (a->uploading || a->upload_output->vm.uploading) { error_reply(a,id,STATUS_BUSY,"upload_busy","another upload is in progress; cancel it first"); return; }
        if (!object_begin(a,p,size)) error_reply(a,id,STATUS_UPLOAD,"invalid_upload","invalid object upload metadata or size");
        else { a->upload_owner=a->source; reply(a,id,STATUS_OK,0,"object upload started"); }
        return;
    }
    if (type==CMD_OBJECT_CHUNK) {
        if (!a->uploading || size<=4) { error_reply(a,id,STATUS_UPLOAD,"no_upload","no upload or empty chunk"); return; }
        uint32_t offset=read_u32(p); size_t n=size-4;
        if(a->object_kind==2){
            if(!store_upload_chunk(&a->upload,offset,p+4,n)){error_reply(a,id,STATUS_UPLOAD,"invalid_chunk","unexpected chunk or flash write failure");return;}
            a->received=a->upload.received;
        }else{
            if(offset<=a->received&&n<=a->received-offset&&!memcmp(a->staging+offset,p+4,n)){reply(a,id,STATUS_OK,(int32_t)a->received,"duplicate chunk");return;}
            if(offset!=a->received||n>a->expected-a->received){error_reply(a,id,STATUS_UPLOAD,"invalid_offset","unexpected chunk offset or size");return;}
            memcpy(a->staging+offset,p+4,n);a->received+=(uint32_t)n;
        }
        a->upload_deadline=platform_time_us()+30000000u;
        reply(a,id,STATUS_OK,(int32_t)a->received,"chunk accepted"); return;
    }
    if (type==CMD_OBJECT_FINISH) {
        if (size || !a->uploading || a->received!=a->expected) { error_reply(a,id,STATUS_UPLOAD,"incomplete_upload","object upload incomplete"); return; }
        a->uploading=false;
        if (a->object_kind==2?!store_upload_finish(&a->upload):crc32_bytes(a->staging,a->expected)!=a->expected_crc) { error_reply(a,id,STATUS_UPLOAD,"checksum_mismatch","object checksum mismatch"); return; }
        if (a->object_kind==1) {
            HardwareConfig config; char error[160];
            if (!hardware_parse(a->staging,a->expected,&config,error,sizeof(error))) { error_reply(a,id,STATUS_CONFIG,"invalid_config",error); return; }
            hardware_destroy(&config);
        } else if (a->object_kind==3) {
            NetworkConfig config; char error[160];
            if (!network_parse(a->staging,a->expected,&a->network.config,&config,error,sizeof(error))) { a->upload_owner=0; error_reply(a,id,STATUS_CONFIG,"invalid_network",error); return; }
        } else if(a->object_kind==2) {
            const char * error=vm_validate_stored(&a->store,&a->upload.entry);
            if (error) { error_reply(a,id,STATUS_LOAD,"invalid_module",error); return; }
            uint8_t header[80];
            if(!store_read(&a->store,&a->upload.entry,0,header,sizeof(header))){error_reply(a,id,STATUS_STORAGE,"read_failed","program profile read failed");return;}
            a->upload.entry.type=read_u32(header+72)==4?STORE_TRANSITION:STORE_ANIMATION;
        }
        a->staged=true; a->upload_deadline=platform_time_us()+30000000u; reply(a,id,STATUS_OK,(int32_t)a->expected,"object staged; not saved or activated"); return;
    }
    if (type==CMD_OBJECT_CANCEL) {
        if (size) { error_reply(a,id,STATUS_PROTOCOL,"invalid_request","cancel takes no payload"); return; }
        a->uploading=false; a->staged=false; a->upload_output->vm.uploading=false; a->upload_owner=0; store_upload_cancel(&a->upload); reply(a,id,STATUS_OK,0,"object upload cancelled"); return;
    }
    if(type==CMD_BEGIN){
        if(a->uploading||a->staged||a->upload_output->vm.uploading){error_reply(a,id,STATUS_BUSY,"upload_busy","an upload is in progress");return;}
        if(size<8||size>8+OBJECT_NAME_LIMIT||read_u32(p)<8||read_u32(p)>MODULE_LIMIT){reply(a,id,STATUS_UPLOAD,0,"invalid module size or output identifier");return;}
        char output[OBJECT_NAME_LIMIT+1]={0};memcpy(output,p+8,size-8);
        if(size>8&&(!object_name_valid(output)||strlen(output)!=size-8)){reply(a,id,STATUS_CONFIG,0,"invalid output identifier");return;}
        AppOutput *destination=app_output(a,output);
        if(!destination){reply(a,id,STATUS_CONFIG,0,"unknown output");return;}
        if(destination->hardware.indicators){reply(a,id,STATUS_CONFIG,0,"indicator output cannot run animation programs");return;}
        if(!store_upload_begin(&a->store,&a->upload,STORE_ANIMATION,"",read_u32(p),read_u32(p+4))){reply(a,id,STATUS_STORAGE,0,"no flash extent available for upload");return;}
        a->upload_output=destination;
        a->upload_output->vm.uploading=true;a->upload_owner=a->source;a->upload_deadline=platform_time_us()+30000000u;
        reply(a,id,STATUS_OK,0,"flash upload started; active program retained");return;
    }
    if(type==CMD_CHUNK){
        if(!a->upload_output->vm.uploading||size<=4||!store_upload_chunk(&a->upload,read_u32(p),p+4,size-4)){reply(a,id,STATUS_UPLOAD,0,"invalid upload chunk or flash write failure");return;}
        a->upload_deadline=platform_time_us()+30000000u;reply(a,id,STATUS_OK,(int32_t)a->upload.received,"chunk accepted");return;
    }
    if(type==CMD_FINISH){
        if(!a->upload_output->vm.uploading||a->upload.received!=a->upload.entry.size){reply(a,id,STATUS_UPLOAD,0,"upload incomplete");return;}
        Json *request=NULL;int settings=-1;
        if(size){
            static const char *const fields[]={"settings","inputs",NULL};
            request=platform_calloc(1,sizeof(*request));
            if(!request||!json_parse(request,p,size)||!json_fields(request,0,fields)){
                platform_free(request);reply(a,id,STATUS_CONFIG,0,"invalid upload activation settings");return;
            }
            settings=json_get(request,0,"settings");
        }
        const char *error=NULL;bool checksum_ok=false;
        if(!store_upload_finish(&a->upload))error="module checksum or flash write failure";
        else {checksum_ok=true;error=install(a,a->upload_output,&a->upload.entry,NULL,false,request,settings);}
        platform_free(request);
        a->upload_output->vm.uploading=false;a->upload_owner=0;store_upload_cancel(&a->upload);
        reply(a,id,error?(checksum_ok?STATUS_LOAD:STATUS_UPLOAD):STATUS_OK,0,error?error:"module loaded from flash");return;
    }
    if(type==CMD_RESET&&!size){a->upload_output->vm.uploading=false;a->uploading=false;a->staged=false;app_output(a,NULL)->active_program[0]=0;app_output(a,NULL)->active_crc=0;a->upload_owner=0;store_upload_cancel(&a->upload);}
    AppOutput *d=app_output(a,NULL);
    if(d->hardware.indicators&&type!=CMD_STATUS){reply(a,id,STATUS_CONFIG,0,"indicator output is controlled through indicator_set");return;}
    if(d->composition||d->transition){
        if((type==CMD_FRAME&&size!=4)||(type!=CMD_FRAME&&size)){reply(a,id,STATUS_PROTOCOL,0,"invalid composition command payload");return;}
        if(type==CMD_FRAME&&read_u32(p)>1000){reply(a,id,STATUS_PROTOCOL,0,"FRAME requires 0..1000 milliseconds");return;}
        if(composition_output_command(a,d,type,id,type==CMD_FRAME?read_u32(p):0))return;
    }
    vm_command(&d->vm,type,id,p,size);
    if(a->last_code==STATUS_OK)output_command_power(d,type);

}
size_t app_indicator_json(const AppOutput *d,unsigned pixel,char *buffer,size_t capacity) {
    if(!d||!d->indicators||pixel>=d->pixels.count)return 0;
    const IndicatorLight *l=&d->indicators[pixel];
    int n=snprintf(buffer,capacity,"{\"state\":\"%s\",\"brightness\":%u,\"color_mode\":\"rgb\",\"color\":{\"r\":%u,\"g\":%u,\"b\":%u},\"effect\":\"%s\"}",l->on?"ON":"OFF",l->brightness,l->rgb[0],l->rgb[1],l->rgb[2],indicator_effects[l->effect]);
    return n>0&&(size_t)n<capacity?(size_t)n:0;
}
static bool indicator_manage(App *a,AppOutput *d,uint16_t id,const Json *j,const char *op) {
    if(strcmp(op,"indicator_set")&&strcmp(op,"indicator_get")&&strcmp(op,"indicator_snapshot"))return false;
    if(!strcmp(op,"indicator_snapshot")){
        static const char *const snapshot_fields[]={"op","output",NULL};
        if(!d->indicators||!json_fields(j,0,snapshot_fields))goto invalid;
        a->send(a->transport,RESP_PIXELS,id,d->pixels.rgb,d->pixels.count*3);reply(a,id,STATUS_OK,0,"indicator RGB frame");return true;
    }
    static const char *const fields[]={"op","output","pixel","value",NULL};
    uint32_t pixel;
    if(!d->indicators||!json_fields(j,0,fields)||!json_u32(j,json_get(j,0,"pixel"),&pixel)||pixel>=d->pixels.count)goto invalid;
    int value=json_get(j,0,"value");
    if(!strcmp(op,"indicator_set")){
        static const char *const light_fields[]={"state","brightness","color","effect",NULL};
        static const char *const color_fields[]={"r","g","b",NULL};
        if(!json_fields(j,value,light_fields)||!j->tokens[value].children)goto invalid;
        IndicatorLight next=d->indicators[pixel];
        int state=json_get(j,value,"state"),brightness=json_get(j,value,"brightness"),color=json_get(j,value,"color");
        if(state>=0){if(json_equal(j,state,"ON"))next.on=true;else if(json_equal(j,state,"OFF"))next.on=false;else goto invalid;}
        uint32_t n;
        if(brightness>=0){if(!json_u32(j,brightness,&n)||n>255)goto invalid;next.brightness=(uint8_t)n;}
        if(color>=0){
            if(!json_fields(j,color,color_fields))goto invalid;
            const char *channels[]={"r","g","b"};
            for(unsigned c=0;c<3;c++){if(!json_u32(j,json_get(j,color,channels[c]),&n)||n>255)goto invalid;next.rgb[c]=(uint8_t)n;}
        }
        int effect=json_get(j,value,"effect");
        if(effect>=0){
            unsigned e=0;while(e<sizeof(indicator_effects)/sizeof(indicator_effects[0])&&!json_equal(j,effect,indicator_effects[e]))e++;
            if(e==sizeof(indicator_effects)/sizeof(indicator_effects[0]))goto invalid;
            next.effect=(IndicatorEffect)e;
        }
        if(next.effect!=d->indicators[pixel].effect||(next.on&&!d->indicators[pixel].on))next.effect_started=platform_time_us();
        d->indicators[pixel]=next;d->indicator_dirty=true;
    }else if(value>=0)goto invalid;
    char buffer[160];size_t size=app_indicator_json(d,pixel,buffer,sizeof(buffer));
    Writer w={.app=a,.id=id};bytes(&w,buffer,size);flush(&w);reply(a,id,STATUS_OK,0,"indicator state");return true;
invalid:
    error_reply(a,id,STATUS_CONFIG,"invalid_indicator","expected indicator output, pixel and valid RGB light value");return true;
}
static bool key_feedback_manage(App *a,uint16_t id,const Json *j,const char *op) {
    if(strcmp(op,"key_feedback_set")&&strcmp(op,"key_feedback_get"))return false;
    static const char *const fields[]={"op","input","key","enabled",NULL};
    char input[OBJECT_NAME_LIMIT+1];uint32_t key;bool enabled;
    if(!json_fields(j,0,fields)||!json_string(j,json_get(j,0,"input"),input,sizeof(input))||
       !json_u32(j,json_get(j,0,"key"),&key)||key<1||key>DIGITAL_INPUT_CHANNELS)goto invalid;
    DigitalInput *d=NULL;
    for(unsigned n=0;n<a->digital_inputs.count;n++)if(!strcmp(a->digital_inputs.devices[n].config.id,input))d=&a->digital_inputs.devices[n];
    if(!d||!d->config.indicators[0])goto invalid;
    uint16_t bit=(uint16_t)(1u<<(key-1));
    if(!strcmp(op,"key_feedback_set")){
        if(!json_bool(j,json_get(j,0,"enabled"),&enabled))goto invalid;
        uint16_t next=enabled?(uint16_t)(d->press_feedback|bit):(uint16_t)(d->press_feedback&~bit);
        if(next!=d->press_feedback){
            uint8_t mask[2]={(uint8_t)next,(uint8_t)(next>>8)};
            bool ok=store_put(&a->store,STORE_KEY_FEEDBACK,input,mask,sizeof(mask));resume_clock(a);
            if(!ok){error_reply(a,id,STATUS_STORAGE,"feedback_not_saved","could not persist key press feedback");return true;}
            d->press_feedback=next;
        }
    }else if(json_get(j,0,"enabled")>=0)goto invalid;
    Writer w={.app=a,.id=id};literal(&w,"{\"enabled\":");literal(&w,d->press_feedback&bit?"true":"false");literal(&w,"}");flush(&w);
    reply(a,id,STATUS_OK,0,"key press feedback setting");return true;
invalid:
    error_reply(a,id,STATUS_CONFIG,"invalid_key_feedback","expected bound input, key (1..16) and boolean enabled");return true;
}
__attribute__((noinline)) static AppOutput *request_output(App *a,const uint8_t *p,size_t size){
    Json j;char output[OBJECT_NAME_LIMIT+1]="";if(!json_parse(&j,p,size))return NULL;
    int slot=json_get(&j,0,"slot");if(slot>=0&&!json_equal(&j,slot,"A")&&!json_equal(&j,slot,"B"))return NULL;
    int token=json_get(&j,0,"output");
    if(token>=0&&(!json_string(&j,token,output,sizeof(output))||!object_name_valid(output)))return NULL;
    return app_output(a,output);
}
static bool input_manage(App *a,uint16_t id,const Json *j,const char *op){
    if(strncmp(op,"input_",6))return false;
    const char *error=NULL;uint32_t generation=0;
    if(!strcmp(op,"input_claim")){
        static const char *const fields[]={"op","signals",NULL};
        error=json_fields(j,0,fields)?input_registry_claim(&a->inputs,j,json_get(j,0,"signals"),&generation):"invalid input claim fields";
        if(!error){Writer w={.app=a,.id=id};literal(&w,"{\"generation\":");number(&w,generation);literal(&w,",\"boot_id\":");quoted(&w,a->boot_id);literal(&w,"}");flush(&w);}
    }else if(!strcmp(op,"input_publish")){
        static const char *const fields[]={"op","boot_id","generation","sequence","values",NULL};uint32_t sequence;
        if(!json_fields(j,0,fields)||!json_equal(j,json_get(j,0,"boot_id"),a->boot_id)||
           !json_u32(j,json_get(j,0,"generation"),&generation)||!json_u32(j,json_get(j,0,"sequence"),&sequence))error="invalid input publication or previous boot";
        else error=input_registry_publish(&a->inputs,j,json_get(j,0,"values"),generation,sequence);
    }else if(!strcmp(op,"input_list")){
        static const char *const fields[]={"op",NULL};
        if(!json_fields(j,0,fields))error="invalid input list fields";
        else{
            Writer w={.app=a,.id=id};literal(&w,"[");
            static const char *const types[]={"","int","float","rgb8","bool"};
            for(unsigned n=0;n<a->inputs.count;n++){
                InputSignal *source=&a->inputs.signals[n];char value[48];if(n)literal(&w,",");
                literal(&w,"{\"name\":");quoted(&w,source->name);literal(&w,",\"type\":");quoted(&w,types[source->type]);
                literal(&w,",\"generation\":");number(&w,source->publisher);literal(&w,",\"sequence\":");number(&w,source->sequence);
                literal(&w,",\"valid\":");literal(&w,source->valid?"true":"false");literal(&w,",\"value\":");
                if(!source->valid)strcpy(value,"null");else if(source->type==T_FLOAT)snprintf(value,sizeof(value),"%.9g",(double)source->value.f);
                else if(source->type==T_BOOL)strcpy(value,source->value.u?"true":"false");else snprintf(value,sizeof(value),"%ld",(long)source->value.i);
                literal(&w,value);literal(&w,",\"age_ms\":");if(source->valid)wide(&w,(platform_time_us()-source->received)/1000);else literal(&w,"null");literal(&w,"}");
            }
            literal(&w,"]");flush(&w);
        }
    }else error="unknown input operation";
    if(error)error_reply(a,id,STATUS_CONFIG,"invalid_input",error);else reply(a,id,STATUS_OK,(int32_t)generation,"input operation completed");
    return true;
}
static void manage_dispatch(App *a,uint16_t id,const uint8_t *p,size_t size){
    AppOutput *d=request_output(a,p,size);
    if(!d){error_reply(a,id,STATUS_PROTOCOL,"invalid_output","invalid request, unknown output or invalid A/B slot");return;}
    Json *j=platform_calloc(1,sizeof(*j));char op[40],layer_id[OBJECT_NAME_LIMIT+1]="";
    if(!j||!json_parse(j,p,size)||!json_string(j,json_get(j,0,"op"),op,sizeof(op))){platform_free(j);error_reply(a,id,STATUS_PROTOCOL,"invalid_request","invalid management request");return;}
    if(key_feedback_manage(a,id,j,op)||indicator_manage(a,d,id,j,op)){platform_free(j);return;}
    if(d->hardware.indicators&&(!strncmp(op,"transition_",11)||!strncmp(op,"composition_",12)||
       !strcmp(op,"preset_activate")||!strcmp(op,"preset_recall")||!strcmp(op,"output_power")||
       !strcmp(op,"program_activate")||!strcmp(op,"program_reset")||!strcmp(op,"program_pause")||!strcmp(op,"program_resume")||!strcmp(op,"program_inputs")||!strcmp(op,"output_command"))){
        platform_free(j);error_reply(a,id,STATUS_CONFIG,"indicator_owned","indicator pixels are controlled through indicator_set");return;
    }
    if(input_manage(a,id,j,op)){platform_free(j);return;}
    if(light_manage(a,d,id,j,op)||transition_manage(a,d,id,j,op)||recall_manage(a,d,id,j,op)||preset_manage(a,d,id,j,op)){platform_free(j);return;}
    uint32_t before_revision=d->composition_revision;
    if(composition_manage(a,d,id,j,op)){if(d->composition_revision!=before_revision)d->recall_owned=false;platform_free(j);return;}
    unsigned slot=request_slot(j);Composition *s=output_composition(d,slot);
    if(!strcmp(op,"program_activate")&&(slot||d->transition)){activate_composition_slot(a,d,id,j,slot);platform_free(j);return;}
    VmInstance *vm=&d->vm;int layer_token=json_get(j,0,"layer");
    if((d->composition||d->transition)&&layer_token<0&&json_get(j,0,"target")<0&&!strcmp(op,"program_snapshot")){
        static const char *const fields[]={"op","slot","output",NULL};
        if(!json_fields(j,0,fields)){platform_free(j);error_reply(a,id,STATUS_PROTOCOL,"invalid_request","invalid snapshot fields");return;}
        platform_free(j);
        for(size_t offset=0;offset<(size_t)d->pixels.count*3;offset+=FRAME_LIMIT){size_t n=(size_t)d->pixels.count*3-offset;if(n>FRAME_LIMIT)n=FRAME_LIMIT;a->send(a->transport,RESP_PIXELS,id,d->pixels.rgb+offset,n);}
        Writer w={.app=a,.id=id};literal(&w,"{\"state\":");composition_state(&w,d);literal(&w,"}");flush(&w);
        reply(a,id,STATUS_OK,0,"last presented composition frame");return;
    }
    int target_token=json_get(j,0,"target");bool transition_target=target_token>=0;
    if(transition_target){
        if(!json_equal(j,target_token,"transition")||layer_token>=0||!d->transition||d->transition->effect!=TRANSITION_PROGRAM||!d->transition->programs[0]||
           (strcmp(op,"program_state")&&strcmp(op,"program_inputs")&&strcmp(op,"program_snapshot"))){platform_free(j);error_reply(a,id,STATUS_CONFIG,"unknown_target","select a transition program first");return;}
        vm=&d->transition->programs[0]->vm;
    }else if(layer_token>=0){
        if(!json_string(j,layer_token,layer_id,sizeof(layer_id))){platform_free(j);error_reply(a,id,STATUS_CONFIG,"unknown_layer","invalid layer id");return;}
        CompositionLayer *l=composition_layer(s,layer_id);
        if(!l){platform_free(j);error_reply(a,id,STATUS_CONFIG,"unknown_layer","composition layer is not configured");return;}
        vm=&l->vm;
    }else if(s&&s->layer_count)vm=&s->layers[0].vm;
    else if(slot&&!s){platform_free(j);error_reply(a,id,STATUS_CONFIG,"unknown_slot","slot B has no composition");return;}
    if(slot&&layer_token<0&&!strcmp(op,"output_command")){
        static const char *const fields[]={"op","output","slot","command","delta_ms",NULL};
        uint32_t command,delta=0;int dt=json_get(j,0,"delta_ms");
        bool valid=json_fields(j,0,fields)&&json_u32(j,json_get(j,0,"command"),&command)&&(dt<0||json_u32(j,dt,&delta))&&delta<=1000;
        platform_free(j);
        if(!valid){error_reply(a,id,STATUS_PROTOCOL,"invalid_command","invalid composition slot command");return;}
        const char *error=NULL;
        if(command==CMD_PLAY||command==CMD_STOP){composition_play(s,command==CMD_PLAY);if(command==CMD_STOP){composition_invalidate(s);memset(s->front,0,(size_t)s->count*3);s->frames++;}}
        else if(command==CMD_FRAME){error=composition_frame(s,delta);if(!error)for(size_t offset=0;offset<(size_t)s->count*3;offset+=FRAME_LIMIT){size_t n=(size_t)s->count*3-offset;if(n>FRAME_LIMIT)n=FRAME_LIMIT;a->send(a->transport,RESP_PIXELS,id,s->front+offset,n);}}
        else if(command==CMD_RESET){d->transition->compositions[1]=NULL;composition_destroy(s);++d->composition_revision;}
        else if(command!=CMD_STATUS)error="unsupported composition slot command";
        if(!error&&command!=CMD_STATUS&&command!=CMD_FRAME)d->recall_owned=false;
        transition_invalidate(d->transition);reply(a,id,error?STATUS_EXECUTION:STATUS_OK,0,error?error:"composition slot command complete");return;
    }
    bool read_only=!strcmp(op,"program_state")||!strcmp(op,"program_snapshot"),input_update=!strcmp(op,"program_inputs");
    if(read_only||!strcmp(op,"program_reset")||!strcmp(op,"program_pause")||!strcmp(op,"program_resume")||!strcmp(op,"program_inputs")){
        if(!read_only&&!input_update){transition_invalidate(d->transition);composition_invalidate(s);}
        platform_free(j);
        uint32_t generation=vm->generation,input_revision=vm->portable?vm->portable->input_revision:0;bool was_playing=vm->playing;
        if(!control_manage(vm,id,p,size))manage(a,id,p,size);
        if(a->last_code==STATUS_OK&&!s&&!transition_target&&(!strcmp(op,"program_resume")||!strcmp(op,"program_reset")))d->power_on=true;
        if(generation!=vm->generation||was_playing!=vm->playing||(vm->portable&&input_revision!=vm->portable->input_revision)){if(!transition_target)composition_modified(s);d->recall_owned=false;}
        if(vm->generation>d->layer_generation)d->layer_generation=vm->generation;
        if(input_update){if(d->transition)d->transition->dirty=true;}
        else if(!read_only){transition_invalidate(d->transition);composition_invalidate(s);}
        return;
    }
    if(layer_token>=0&&!strcmp(op,"output_command")){
        uint32_t command,delta=0;
        static const char *const fields[]={"op","slot","output","layer","command","delta_ms",NULL};
        bool valid=json_fields(j,0,fields)&&json_u32(j,json_get(j,0,"command"),&command);
        int dt=json_get(j,0,"delta_ms");if(dt>=0)valid=valid&&json_u32(j,dt,&delta);
        valid=valid&&delta<=1000&&(command==CMD_STATUS||command==CMD_PLAY||command==CMD_STOP||command==CMD_RESET||command==CMD_FRAME);
        platform_free(j);
        if(!valid){error_reply(a,id,STATUS_PROTOCOL,"invalid_command","invalid layer command");return;}
        if(command!=CMD_STATUS){transition_invalidate(d->transition);composition_invalidate(s);}
        uint32_t generation=vm->generation;bool was_playing=vm->playing;
        uint8_t bytes[4];write_u32(bytes,delta);vm_command(vm,(uint8_t)command,id,command==CMD_FRAME?bytes:NULL,command==CMD_FRAME?4:0);
        if(generation!=vm->generation||was_playing!=vm->playing){composition_modified(s);d->recall_owned=false;}
        if(command==CMD_STOP&&vm->control.contribution_valid){memset(vm->control.committed,0,control_frame_bytes(vm));vm->control.contribution_frame++;}
        if(vm->generation>d->layer_generation)d->layer_generation=vm->generation;
        if(command!=CMD_STATUS){transition_invalidate(d->transition);composition_invalidate(s);}
        return;
    }
    platform_free(j);manage(a,id,p,size);
}
void app_command(void *context,uint8_t type,uint16_t id,const uint8_t *p,size_t size) {
    App *a=context; uint64_t previous=a->source; if (!a->source) a->source=1;
    if(a->command_active){a->source=previous;return;}
    a->command_active=true;
    // Keep the large JSON-parser frames of these paths out of each other's
    // call chains; reset/activation also validate a separate program manifest.
    if(type==CMD_MANAGE)manage_dispatch(a,id,p,size);
    else command_impl(a,type,id,p,size);
    if (!a->uploading && !a->staged && !a->upload_output->vm.uploading) a->upload_owner=0;
    a->command_active=false;
    if(a->disconnect_pending){a->disconnect_pending=false;app_disconnect(a,a->upload_owner);}
    a->source=previous;
}
void app_dispatch(App *a,uint64_t source,SendMessage send,void *transport,uint8_t type,uint16_t id,const uint8_t *p,size_t size) {
    if(a->command_active){send_status(send,transport,id,STATUS_BUSY,0,"engine command in progress");return;}
    a->command_active=true;
    SendMessage previous_send=a->send; void *previous_transport=a->transport; uint64_t previous_source=a->source;
    a->send=send; a->transport=transport; a->source=source;
    if(type==CMD_MANAGE)manage_dispatch(a,id,p,size);
    else command_impl(a,type,id,p,size);
    if (!a->uploading && !a->staged && !a->upload_output->vm.uploading) a->upload_owner=0;
    a->command_active=false;
    if(a->disconnect_pending){a->disconnect_pending=false;app_disconnect(a,a->upload_owner);}
    a->send=previous_send; a->transport=previous_transport; a->source=previous_source;
}
void app_disconnect(App *a,uint64_t source) {
    if (!source || a->upload_owner!=source) return;
    if(a->command_active){a->disconnect_pending=true;return;}
    a->upload_owner=0; a->uploading=false; a->staged=false;
    a->upload_output->vm.uploading=false;store_upload_cancel(&a->upload);
    memset(a->staging,0,sizeof(a->staging));
}
void app_reply_delivered(App *a,uint64_t source) {
    if (a->network_pending && a->network_reply_source==source) { a->network_due=platform_time_us()+100000u; a->network_reply_source=0; }
}
