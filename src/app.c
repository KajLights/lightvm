#include "app.h"
#include "status.h"
#include "release.h"
#include "runtime.h"
#include "render_executor.h"
#include "device/json.h"
#include "device/description.h"
#include "vm_allocator.h"
#include "draw.h"
#include "preparation.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <inttypes.h>

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
static void number(Writer *w,uint32_t n){char s[21];size_t size=json_format_u64(s,n);bytes(w,s,size);}
static void wide(Writer *w,uint64_t n){char s[21];size_t size=json_format_u64(s,n);bytes(w,s,size);}
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
    uint32_t color=d->pixels.light.color;
    int n;
    if(mqtt)n=snprintf(buffer,capacity,
        "{\"state\":\"%s\",\"brightness\":%u,\"color_mode\":\"%s\",\"color\":{\"r\":%u,\"g\":%u,\"b\":%u},\"effect\":\"%s\"}",
        app_output_power(d)?"ON":"OFF",(unsigned)lroundf(d->pixels.light.brightness*255),
        app_output_color_control(d)?"rgb":"brightness",(unsigned)((color>>16)&255),(unsigned)((color>>8)&255),(unsigned)(color&255),preset?preset:"");
    else n=snprintf(buffer,capacity,
        "{\"revision\":%lu,\"composition_revision\":%lu,\"power\":%s,\"brightness\":%.9g,\"color\":%u,\"color_control\":%s,\"preset\":%s%s%s}",
        (unsigned long)d->light_revision,(unsigned long)d->composition_revision,app_output_power(d)?"true":"false",
        (double)d->pixels.light.brightness,(unsigned)color,app_output_color_control(d)?"true":"false",preset?"\"":"",preset?preset:"null",preset?"\"":"");
    return n>=0&&(size_t)n<capacity?(size_t)n:0;
}
static void composition_modified(Composition *s){if(s){s->preset[0]=0;s->preset_crc=0;}}
static void binding_text(void *,const char *);
static void composition_state_for(Writer *w,const AppOutput *d,const Composition *s){
    uint64_t sampled_at=platform_time_us();
    literal(w,"{\"revision\":");number(w,d->composition_revision);
    literal(w,",\"sampled_at_us\":");wide(w,sampled_at);
    literal(w,",\"active\":");literal(w,s?"true":"false");
    const char *preset=composition_preset(w->app,s);
    literal(w,",\"preset\":");if(preset)quoted(w,preset);else literal(w,"null");
    if(s){
        literal(w,",\"sources\":");input_scope_describe(s->sources,binding_text,w);
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
            literal(w,",\"render_reusable\":");literal(w,vm->portable&&vm->portable->render_reusable?"true":"false");
            literal(w,",\"dependency_current\":");
            if(vm->portable&&vm->portable->render_reusable){
                uint64_t time=vm->control.program_us;
                if(s->playing&&vm->playing&&!vm->control.in_step&&sampled_at>=vm->last_frame)time+=sampled_at-vm->last_frame;
                literal(w,vm->control.contribution_valid&&vm->control.contribution.frame&&!vm->control.in_step&&!inputs_changed(vm->portable,time)?"true":"false");
            }else literal(w,"null");
            literal(w,",\"displayed_frame\":");number(w,vm->control.displayed.frame);
            literal(w,",\"distinct_presentations\":");wide(w,vm->control.distinct_presentations);
            literal(w,",\"displayed_sample_age_us\":");
            if(vm->control.displayed.frame)wide(w,sampled_at-vm->control.displayed.captured_us);else literal(w,"null");
            literal(w,",\"completion_to_display_us\":");
            if(vm->control.displayed.frame)wide(w,vm->control.presented_us-vm->control.displayed.completed_us);else literal(w,"null");

            literal(w,",\"dependency_revision\":");number(w,vm->control.dependency_revision);
            literal(w,",\"dirty_to_display_us\":");wide(w,vm->control.dirty_to_display_us);
            literal(w,",\"dirty_age_us\":");if(vm->control.dirty)wide(w,sampled_at-vm->control.dirty_since_us);else literal(w,"null");
            literal(w,",\"runnable_wait_us\":");wide(w,vm->control.wait_us[1]);
            literal(w,",\"composite_wait_us\":");wide(w,vm->control.wait_us[2]);
            literal(w,",\"output_wait_us\":");wide(w,vm->control.wait_us[3]);
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

static void start_output(App *,AppOutput *);
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
        const StoreEntry *entry=store_find(&a->store,STORE_KEY_FEEDBACK,input->config->id);
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
static bool submit_scene(App *a,AppOutput *d,uint16_t id,PreparationRequest request,const void *data,size_t size){
    request.revision=d->composition_revision;request.light_revision=d->light_revision;
    if(!request.light_attached)request.light=d->pixels.light;
    uint32_t operation;const char *error=preparation_submit(a,d,&request,data,size,&operation);
    if(error){error_reply(a,id,STATUS_LOAD,"preparation_failed",error);return false;}
    if(!a->booting)preparation_reply(a,id,operation);
    return true;
}
static bool activate(App *a,const char *name,uint16_t id,const char *action,const char *output,const Json *request,int settings) {
    AppOutput *d=app_output(a,output);
    if(!d){error_reply(a,id,STATUS_CONFIG,"unknown_output","output is not configured");return false;}
    const StoreEntry *entry=store_find(&a->store,STORE_ANIMATION,name);
    if(!entry||!strcmp(name,STORE_CONFIG)||!strcmp(name,STORE_NETWORK)){error_reply(a,id,STATUS_STORAGE,"not_found","stored program does not exist");return false;}
    if(strcmp(action,"play")){error_reply(a,id,STATUS_EXECUTION,"invalid_action","animation profiles execute through PLAY/FRAME");return false;}
    PreparationRequest pending={.kind=PREPARE_DIRECT,.entry=*entry,.play=true};strcpy(pending.name,name);
    (void)settings;
    return submit_scene(a,d,id,pending,request?request->text:NULL,request?request->size:0);
}
static void indicator_tick(App *,AppOutput *,uint64_t);
static const char *save_output_state(App *,const AppOutput *,bool,const char *,const LightControls *);
static void collect_composition(Composition *s,unsigned budget){
    if(!s)return;
    composition_collect_ready(s);
    if(!s->playing||s->command_active||s->composing){
        for(unsigned n=0;n<s->layer_count;n++){
            VmInstance *vm=&s->layers[n].vm;control_observe(vm,vm->control.program_us);
            control_wait(vm,s->playing&&s->composing?2:0);
        }
        return;
    }
    unsigned first=s->layer_count?s->cursor%s->layer_count:0;
    bool admitted=false;
    /* Continue after the last admitted VM, not merely the last visited one.
       This bounds fairness by admissions even when an epoch fits only one pair,
       without an O(layers squared) age/cost division on the coordinator. */
    for(unsigned n=0;n<s->layer_count;n++){
        unsigned chosen=(first+n)%s->layer_count;
        VmInstance *vm=&s->layers[chosen].vm;
        bool changed=false;
        if(s->prepared&&!vm->control.in_step&&vm->portable&&vm->portable->render_reusable){
            uint64_t now=platform_time_us(),elapsed=now>=vm->last_frame?now-vm->last_frame:0;
            changed=inputs_changed(vm->portable,vm->control.program_us+elapsed);
        }
        /* A completed composite owns its own pixels. A changed reusable layer
           can render ahead while it waits for the physical front buffer. */
        if(!s->prepared||vm->control.in_step||changed){
            if(vm_tick_collect(vm,budget)){s->cursor=(chosen+1)%s->layer_count;admitted=true;}
        }else{control_observe(vm,vm->control.program_us);control_wait(vm,3);}
    }
    /* A complete round must also exchange core assignment. */
    if(admitted&&s->cursor==first)s->cursor=(first+1)%s->layer_count;
}
static void collect_render_jobs(App *a,unsigned first,unsigned budget){
    vm_tick_begin();
    for(unsigned n=0;n<a->output_count;n++){
        AppOutput *d=a->outputs[(first+n)%a->output_count];
        if(!d||d->hardware.indicators)continue;
        if(d->transition){
            Transition *t=d->transition;
            transition_update(t,platform_time_us());
            t->destination_only=d->recall_owned&&t->driver==TRANSITION_MANUAL&&t->progress==100;
            if(t->start_pending||t->command_active||!t->playing)continue;
            /* Finish/publish the transition before admitting another full set
               of layer slices. Otherwise four busy VMs can consume every
               epoch and leave one completed child waiting for its front. */
            transition_tick(t,64);
            if(t->destination_only)collect_composition(t->compositions[1],budget);
            else{
                unsigned slot=t->schedule_slot%2;
                if(runtime_epoch_can_collect())t->schedule_slot++;
                for(unsigned k=0;k<2;k++)collect_composition(t->compositions[(slot+k)%2],budget);
            }
        }else if(d->composition)collect_composition(d->composition,budget);
        else vm_tick_collect(&d->vm,budget);
    }
    vm_tick_flush();
}
static void service(void *context) {
    App *a=context;
    if(!a->hardware_changing)digital_inputs_tick(&a->digital_inputs,platform_time_us());
    bool previous=a->command_active;a->command_active=true;
    unsigned first=a->output_count?a->schedule_cursor++%a->output_count:0;
    collect_render_jobs(a,first,64);
    for(unsigned i=0;i<a->output_count;i++){
        AppOutput *d=a->outputs[(first+i)%a->output_count];
        if(!d)continue;
        if(d->hardware.indicators){indicator_tick(a,d,platform_time_us());continue;}
        if(d->transition){
            if(!d->transition->start_pending)transition_tick(d->transition,64);
        }
        else if(d->composition)composition_tick(d->composition,64);
        else if(!runtime_owns(&d->vm))vm_tick_budget(&d->vm,64);
    }
    vm_tick_end();
    if(a->service_io)a->service_io(a->service_context);
    a->command_active=previous;
    if(!previous&&a->disconnect_pending){a->disconnect_pending=false;app_disconnect(a,a->upload_owner);}
}
void app_init(App *a, SendMessage send, void *transport) {
    runtime_defer_begin();
    render_executor_init();
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
    device_metadata_init(&a->metadata,&a->store);
    const StoreEntry *config=store_find(&a->store,STORE_CONFIGURATION,STORE_CONFIG);
    if (config) {
        HardwareConfig hardware;
        char error[160];
        if (!store_read(&a->store,config,0,a->config_json,config->size)) { snprintf(a->boot_error,sizeof(a->boot_error),"configuration read failed"); empty_config(a); }
        else if (!hardware_parse(a->config_json,config->size,&hardware,error,sizeof(error))) { snprintf(a->boot_error,sizeof(a->boot_error),"%s",error); empty_config(a); }
        else if (!config_apply(a,&hardware,a->config_json,config->size)) snprintf(a->boot_error,sizeof(a->boot_error),"native output initialization failed");
        else if(!a->network.recovery) {
            for(unsigned i=0;i<a->output_count;i++)a->outputs[i]->startup_pending=true;
        }
    }
    network_settings_init(&a->network_settings,&a->network,&a->store);
    network_settings_load(&a->network_settings);
    board_status_init(&a->board_status,&a->inputs);
    board_status_tick(&a->board_status,&a->network,platform_time_us());
    a->device=app_device_host(a);
    a->booting=false;runtime_defer_end();
}
void app_destroy(App *a) {
    runtime_services(NULL,NULL);
    preparation_destroy(a);
    store_upload_cancel(&a->upload);
    empty_config(a);vm_destroy(&a->fallback.vm);store_destroy(&a->store);
    audio_sources_destroy(&a->audio);
    input_registry_destroy(&a->inputs);
    render_executor_shutdown();
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
        if(input->available&&!strcmp(input->config->indicators,d->hardware.id))pressed=input->stable&input->press_feedback;
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
static void source_demand(VmInstance *vm,bool active){
    if(!vm||!vm->portable)return;
    const char *error=inputs_demand(vm->portable,active);
    if(error)snprintf(vm->animation_error,sizeof(vm->animation_error),"%s",error);
}
static void composition_demand(Composition *c,bool active){
    if(c)for(unsigned n=0;n<c->layer_count;n++)source_demand(&c->layers[n].vm,active&&c->playing&&c->layers[n].vm.playing);
}
static void sync_output_audio_sources(AppOutput *d){
    Transition *t=d->transition;
    if(t){
        bool completed=d->recall_owned&&t->driver==TRANSITION_MANUAL&&t->progress==100;
        composition_demand(t->compositions[0],t->playing&&!completed);
        composition_demand(t->compositions[1],t->playing&&(!completed||d->power_on));
        for(unsigned n=0;n<2;n++)if(t->programs[n])source_demand(&t->programs[n]->vm,t->playing&&!completed);
    }else if(d->composition)composition_demand(d->composition,d->power_on);
    else source_demand(&d->vm,d->vm.playing&&d->power_on);
}
static void sync_audio_sources(App *a){
    for(unsigned i=0;i<a->output_count;i++)sync_output_audio_sources(a->outputs[i]);
}
typedef struct {
    bool manual,power,play,presenter,replace_transition;
    Composition *resume,*exclude;
    VmInstance *resume_vm,*exclude_vm;
} SourceDemandChange;
static const char *plan_composition_sources(AudioSourcePlan *plan,Composition *c,bool active,SourceDemandChange change){
    if(!c||c==change.exclude||!active||(!c->playing&&c!=change.resume))return NULL;
    for(unsigned n=0;n<c->layer_count;n++){
        VmInstance *vm=&c->layers[n].vm;
        if(vm!=change.exclude_vm&&vm->portable&&(vm->playing||vm==change.resume_vm)){
            const char *error=audio_source_plan_add(plan,vm->portable->source_scope);if(error)return error;
        }
    }
    return NULL;
}
/* Reserve all scopes that the command will make active, including retained
 * scenes. A failed reservation changes no playback flag or canonical policy. */
static const char *reserve_output_sources(AppOutput *d,SourceDemandChange change,AudioSourcePlan *plan){
    Transition *t=d->transition;const char *error=NULL;bool power=d->power_on||change.power;
    if(t){
        bool playing=t->playing||change.play;
        bool completed=d->recall_owned&&!change.manual&&t->driver==TRANSITION_MANUAL&&t->progress==100;
        error=plan_composition_sources(plan,t->compositions[0],playing&&!completed,change);
        if(!error)error=plan_composition_sources(plan,t->compositions[1],playing&&(!completed||power),change);
        for(unsigned n=0;!error&&n<2;n++)if(!change.replace_transition&&t->programs[n]&& &t->programs[n]->vm!=change.exclude_vm&&playing&&!completed)
            error=audio_source_plan_add(plan,t->programs[n]->vm.portable->source_scope);
    }else if(d->composition)error=plan_composition_sources(plan,d->composition,power||change.presenter,change);
    else if(&d->vm!=change.exclude_vm&&d->vm.portable&&(power||change.presenter)&&(d->vm.playing||&d->vm==change.resume_vm))error=audio_source_plan_add(plan,d->vm.portable->source_scope);
    if(!error)error=audio_source_plan_acquire(plan);
    return error;
}
static void finish_output_sources(AppOutput *d,AudioSourcePlan *plan){
    sync_output_audio_sources(d);audio_source_plan_release(plan);
}
void app_tick(App *a) {
    uint64_t now=platform_time_us();
    runtime_foreground(now);
    board_status_tick(&a->board_status,&a->network,now);
    sync_audio_sources(a);
    audio_sources_tick(&a->audio,now);
    digital_inputs_tick(&a->digital_inputs,now);
#if LIGHTVM_BOOT_SEQUENCE
    boot_sequence_tick(&a->boot_sequence,now);
#endif
    if (a->upload_owner && now>=a->upload_deadline) app_disconnect(a,a->upload_owner);
    network_settings_tick(&a->network_settings,now);
    /* Rendering and candidate preparation each have a bounded quantum. Do not
       concatenate both before the port can service another command. Five
       short preparation turns per render turn keep the old scene progressing
       while avoiding a long preparation quantum under command/HTTP load. */
    bool prepare=preparation_pending(a)&&a->preparation_turn<5;
    a->preparation_turn=prepare?a->preparation_turn+1:0;
    if(!prepare){
        unsigned first=a->output_count?a->schedule_cursor++%a->output_count:0;
        collect_render_jobs(a,first,4096);
        for(unsigned i=0;i<a->output_count;i++){
            AppOutput *d=a->outputs[(first+i)%a->output_count];
            if(d->hardware.indicators){indicator_tick(a,d,platform_time_us());continue;}
            if(d->transition)transition_tick(d->transition,4096);else if(d->composition)composition_tick(d->composition,4096);else vm_tick(&d->vm);
        }
        vm_tick_end();
    }else for(unsigned i=0;i<a->output_count;i++){
        AppOutput *d=a->outputs[i];
        if(d->hardware.indicators)indicator_tick(a,d,platform_time_us());
    }
    if(preparation_has_room(a))for(unsigned i=0;i<a->output_count;i++)if(a->outputs[i]->startup_pending){
        a->outputs[i]->startup_pending=false;start_output(a,a->outputs[i]);break;
    }
    if(prepare||!preparation_pending(a))preparation_tick(a);
    for(unsigned i=0;i<a->output_count;i++){
        AppOutput *d=a->outputs[i];
        if(d->light_save_due&&now>=d->light_save_due&&!a->command_active&&!a->store.writer){
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
enum Status app_save_network(App *a,const NetworkConfig *config,uint32_t revision,uint64_t source,char *error,size_t capacity) {
    if(a->upload_owner && a->upload_owner!=source){snprintf(error,capacity,"another upload is in progress; try again after it finishes");return STATUS_BUSY;}
    bool previous=a->command_active;a->command_active=true;
    enum Status status=network_settings_save(&a->network_settings,config,revision,source,error,capacity);
    resume_clock(a);a->command_active=previous;
    if(!previous && a->disconnect_pending){a->disconnect_pending=false;app_disconnect(a,a->upload_owner);}
    return status;
}
static void transition_state(Writer *,AppOutput *);
static void describe(App *a, uint16_t id) {
    Writer w={.app=a,.id=id};
    const PlatformInfo *info=platform_info();
    char limits[192];snprintf(limits,sizeof(limits),"{\"config_bytes\":%u,\"module_bytes\":%u,\"vm_heap_bytes\":%u,\"pixel_capacity\":%u}",(unsigned)CONFIG_LIMIT,(unsigned)MODULE_LIMIT,(unsigned)vm_heap_limit(),(unsigned)PIXEL_CAPACITY);
    literal(&w,"{");device_description(&a->device,limits,binding_text,&w);
    literal(&w,",\"connections\":true,\"light_controls\":true,\"schema\":\"lightvm.config\",\"platform\":");quoted(&w,info->platform);
    literal(&w,",\"build_compiler\":");quoted(&w,__VERSION__);
    literal(&w,",\"cpu_clock_hz\":");unsigned cpu=platform_cpu_hz();if(cpu)number(&w,cpu);else literal(&w,"null");
    literal(&w,",\"reset_reason\":");const char *reset=platform_reset_reason();if(reset)quoted(&w,reset);else literal(&w,"null");
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
    literal(&w,",\"vm_heap_limit\":"); number(&w,vm_heap_limit());
    literal(&w,",\"vm_heap_automatic\":");literal(&w,LIGHTVM_AUTOMATIC_HEAP?"true":"false");
    literal(&w,",\"preparation_memory_bytes\":");number(&w,(uint32_t)preparation_retained_bytes(a));
    literal(&w,",\"preparation_pending\":");literal(&w,preparation_pending(a)?"true":"false");
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
        literal(&w,"{\"id\":");quoted(&w,d->config->id);literal(&w,",\"type\":\"tca9555\",\"available\":");literal(&w,d->available?"true":"false");
        literal(&w,",\"pressed\":");if(d->available)number(&w,d->stable);else literal(&w,"null");literal(&w,"}");
    }
    literal(&w,"],\"input_event_overflow\":");number(&w,a->digital_inputs.overflow);
    literal(&w,",\"config_limit\":"); number(&w,CONFIG_LIMIT);
    literal(&w,",\"module_limit\":"); number(&w,MODULE_LIMIT);
    literal(&w,",\"render_workers\":");number(&w,render_executor_workers());
    literal(&w,",\"pico2_dcp_enabled\":");literal(&w,LIGHTVM_PICO2_DCP_ENABLED?"true":"false");
    literal(&w,",\"max_refresh_hz\":");number(&w,LIGHTVM_MAX_REFRESH_HZ);
    literal(&w,",\"render_slice_us\":");number(&w,LIGHTVM_RENDER_SLICE_US);
    literal(&w,",\"invocation_limit\":");number(&w,LIGHTVM_INVOCATION_LIMIT);
    literal(&w,",\"bytecode_cache_bytes\":");number(&w,LIGHTVM_BYTECODE_CACHE_BYTES);
    literal(&w,",\"http_clients\":");number(&w,LIGHTVM_HTTP_CLIENTS);
    literal(&w,",\"native_headroom_bytes\":");number(&w,LIGHTVM_NATIVE_HEADROOM_BYTES);
    RenderStats render=render_executor_stats();
    literal(&w,",\"render_offloads\":");wide(&w,render.offloads);
    literal(&w,",\"render_worker_us\":");wide(&w,render.worker_us);
    literal(&w,",\"render_pair_imbalance_us\":");wide(&w,render.pair_imbalance_us);
    literal(&w,",\"render_join_wait_us\":");wide(&w,render.join_wait_us);
    literal(&w,",\"render_max_job_us\":");wide(&w,render.max_job_us);

    literal(&w,",\"worker_stack_peak_bytes\":");number(&w,render.worker_stack_peak);
    literal(&w,",\"main_stack_peak_bytes\":");unsigned main_stack=platform_main_stack_peak();if(main_stack)number(&w,main_stack);else literal(&w,"null");
    RuntimeEpochStats epochs=runtime_epoch_stats();
    literal(&w,",\"render_epoch_target_us\":");number(&w,LIGHTVM_RENDER_EPOCH_US);
    literal(&w,",\"render_epochs\":");wide(&w,epochs.epochs);
    literal(&w,",\"render_epoch_total_us\":");wide(&w,epochs.total_us);
    literal(&w,",\"render_epoch_max_us\":");wide(&w,epochs.max_us);
    literal(&w,",\"render_epoch_overruns\":");wide(&w,epochs.overruns);
    literal(&w,",\"foreground_calls\":");wide(&w,epochs.foreground_calls);
    literal(&w,",\"foreground_max_gap_us\":");wide(&w,epochs.max_foreground_gap_us);
    literal(&w,",\"foreground_over_10ms\":");wide(&w,epochs.foreground_over_10ms);

    literal(&w,",\"ingress_calls\":");wide(&w,epochs.ingress_calls);
    literal(&w,",\"ingress_max_gap_us\":");wide(&w,epochs.max_ingress_gap_us);
    literal(&w,",\"ingress_over_10ms\":");wide(&w,epochs.ingress_over_10ms);
    literal(&w,",\"instance_context_bytes\":"); number(&w,sizeof(VmInstance));
    literal(&w,",\"app_context_bytes\":"); number(&w,sizeof(App));
    literal(&w,",\"upload_workspace_bytes\":"); number(&w,sizeof(StoreUpload));
    literal(&w,",\"upload_page_bytes\":"); number(&w,STORE_PAGE_BYTES);
    literal(&w,",\"animation_reset\":true,\"activation_settings\":true,\"slots\":[\"A\",\"B\"],\"slot_binding\":\"output\",\"pixel_format\":\"rgb8\",\"frame_policy\":\"transparent_before_render\",\"alpha\":true,\"compositions\":true,\"composition_layer_limit\":8,\"layer_pixel_format\":\"rgba8-premul\",\"composition\":true,\"program_generation\":"); number(&w,app_output(a,NULL)->vm.generation);
    literal(&w,",\"ab_transitions\":true,\"transition_sources\":true,\"transition_programs\":true,\"custom_transitions\":true,\"transition_effects\":[");
    for(unsigned n=0;n<TRANSITION_PROGRAM;n++){if(n)literal(&w,",");quoted(&w,transition_effect_name((TransitionEffect)n));}literal(&w,"]");
    literal(&w,",\"transient_settings\":true,\"preset_transitions\":true,\"presets\":true,\"typed_storage\":true");
    literal(&w,",\"live_inputs\":true,\"input_limit\":8,\"input_source_limit\":");number(&w,INPUT_SOURCE_LIMIT);
    literal(&w,",\"board_status_supported\":true");
    literal(&w,",\"audio_subscriptions_supported\":");literal(&w,LIGHTVM_AUDIO_SUBSCRIPTIONS?"true":"false");
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
static bool arguments(const Json *j,Arguments *args) {
    memset(args,0,sizeof(*args)); memcpy(args->action,"play",5); memcpy(args->slot,"A",2);
    static const char *const fields[]={"op","name","type","action","slot","output","expected_revision","expected_saved_revision","settings","inputs","sources","command","delta_ms",NULL};
    if (!json_fields(j,0,fields) || !json_string(j,json_get(j,0,"op"),args->op,sizeof(args->op))) return false;
    if(json_get(j,0,"settings")>=0&&strcmp(args->op,"program_activate"))return false;
    if(json_get(j,0,"inputs")>=0&&strcmp(args->op,"program_activate"))return false;
    int type=json_get(j,0,"type");char type_name[16];
    if(type>=0&&(!json_string(j,type,type_name,sizeof(type_name))||
       ((args->type=store_type(type_name))!=STORE_ANIMATION&&args->type!=STORE_TRANSITION)))return false;
    if(type>=0&&strcmp(args->op,"program_list")&&strcmp(args->op,"program_get")&&strcmp(args->op,"program_save")&&strcmp(args->op,"program_delete"))return false;
    int name=json_get(j,0,"name"),action=json_get(j,0,"action");
    if (name>=0 && (!json_string(j,name,args->name,sizeof(args->name)) || !object_name_valid(args->name))) return false;
    if (action>=0 && !json_string(j,action,args->action,sizeof(args->action))) return false;
    int slot=json_get(j,0,"slot");
    if (slot>=0 && !json_string(j,slot,args->slot,sizeof(args->slot))) return false;
    int command=json_get(j,0,"command"),delta=json_get(j,0,"delta_ms");
    if(command>=0&&!json_u32(j,command,&args->command))return false;
    if(delta>=0&&!json_u32(j,delta,&args->delta_ms))return false;
    if(strcmp(args->slot,"A"))return false;
    int output=json_get(j,0,"output");
    if(output>=0&&(!json_string(j,output,args->output,sizeof(args->output))||!object_name_valid(args->output)))return false;
    int revision=json_get(j,0,"expected_revision"),saved=json_get(j,0,"expected_saved_revision");
    args->has_revision=revision>=0; args->has_saved_revision=saved>=0;
    return (!args->has_revision || json_u32(j,revision,&args->revision)) && (!args->has_saved_revision || json_u32(j,saved,&args->saved_revision));
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


static void apply_composition(AppOutput *d,Composition *candidate){
    clear_output_composition(d);vm_output_cancel(&d->vm);d->vm.output=NULL;control_destroy(&d->vm);d->vm.playing=false;d->vm.ready=false;
    d->composition=candidate;candidate->revision=++d->composition_revision;d->active_program[0]=0;d->active_crc=0;
    d->power_on=true;
}
/* Admit the presenter and any direct-to-composition wrapper before moving ownership. */
static const char *prepare_presenter(AppOutput *d,Transition **out,Composition **wrapper){
    *out=NULL;*wrapper=NULL;if(d->transition)return 0;
    Transition *t=transition_create(&d->pixels);if(!t)return "transition exceeds aggregate memory budget";
    if(!d->composition&&d->vm.ready){*wrapper=composition_create(&t->target,1);if(!*wrapper){transition_destroy(t);return "composition wrapper exceeds memory budget";}}
    *out=t;return 0;
}
static void attach_presenter(App *a,AppOutput *d,Transition *t,Composition *wrapper){
    vm_output_cancel(&d->vm);
    if(d->composition){if(d->composition->prepared)composition_publish(d->composition);composition_offscreen(d->composition,&t->target);}
    else if(wrapper){
        wrapper->sources=d->vm.portable->source_scope;input_scope_retain(wrapper->sources);
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
static void apply_composition_slot(AppOutput *d,Composition *candidate,unsigned slot){
    if(!slot&&!d->transition){apply_composition(d,candidate);return;}
    Transition *t=d->transition;transition_invalidate(t);
    composition_install_prepared(candidate,&t->target);
    for(unsigned n=0;n<candidate->layer_count;n++)strcpy(candidate->layers[n].vm.slot,slot?"B":"A");
    Composition *old=t->compositions[slot];t->compositions[slot]=candidate;if(!slot)d->composition=candidate;
    composition_destroy(old);transition_invalidate(t);candidate->revision=++d->composition_revision;d->recall_owned=false;d->power_on=true;
}
static void activate_composition_slot(App *a,AppOutput *d,uint16_t id,const Json *j,unsigned slot){
    static const char *const fields[]={"op","name","output","slot","action","settings","inputs","sources",NULL};
    char name[OBJECT_NAME_LIMIT+1];
    int action=json_get(j,0,"action");
    if(!json_fields(j,0,fields)||!json_string(j,json_get(j,0,"name"),name,sizeof(name))||!object_name_valid(name)||(action>=0&&!json_equal(j,action,"play"))){error_reply(a,id,STATUS_CONFIG,"invalid_activation","expected a program name and play action");return;}
    const StoreEntry *entry=store_find(&a->store,STORE_ANIMATION,name);
    if(d->layer_generation<d->vm.generation)d->layer_generation=d->vm.generation;
    if(!entry||d->layer_generation==UINT32_MAX||d->composition_revision==UINT32_MAX){error_reply(a,id,STATUS_CONFIG,"preparation_failed","program missing or generation exhausted");return;}
    PreparationRequest pending={.kind=PREPARE_SLOT,.entry=*entry,.slot=slot,.play=true};strcpy(pending.name,name);
    (void)submit_scene(a,d,id,pending,j->text,j->size);
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
static size_t output_state_document(App *a,const AppOutput *d,bool on,const char *name,const LightControls *light,uint8_t *state){
    if(a->booting||!store_info(&a->store).writable)return 0;
    uint8_t previous[OUTPUT_STATE_BYTES];memset(state,0,OUTPUT_STATE_BYTES);state[0]=on;
    const StoreEntry *entry=store_find(&a->store,STORE_OUTPUT_STATE,d->hardware.id);
    bool known=entry&&entry->size==sizeof(previous)&&store_read(&a->store,entry,0,previous,sizeof(previous));
    uint32_t brightness;memcpy(&brightness,&light->brightness,sizeof(brightness));
    write_u32(state+1,brightness);write_u32(state+5,light->color);
    if(name&&name[0])snprintf((char *)state+OUTPUT_STATE_NAME,OBJECT_NAME_LIMIT+1,"%s",name);
    else if(known&&memchr(previous+OUTPUT_STATE_NAME,0,OBJECT_NAME_LIMIT+1)){
        memcpy(state+OUTPUT_STATE_NAME,previous+OUTPUT_STATE_NAME,OBJECT_NAME_LIMIT+1);
        if(on)state[0]=previous[0];
    }else state[0]=0;
    if(known&&!memcmp(state,previous,OUTPUT_STATE_BYTES))return 0;
    return OUTPUT_STATE_BYTES;
}
static const char *save_output_state(App *a,const AppOutput *d,bool on,const char *name,const LightControls *light){
    uint8_t state[OUTPUT_STATE_BYTES];size_t size=output_state_document(a,d,on,name,light,state);if(!size)return NULL;
    bool ok=store_put(&a->store,STORE_OUTPUT_STATE,d->hardware.id,state,(uint32_t)size);resume_clock(a);
    return ok?NULL:"could not persist output light state";
}
/* Everything that can fail is admitted before changing either live slot/driver.
 * No intermediate frame is required: the next fade starts at the old destination. */
static const char *begin_recall(App *a,AppOutput *d,Composition *candidate,TransitionProgram *program,double duration,bool on,const char *name,uint32_t crc,const LightControls *light,PreparedScene *prepared){
    if(d->composition_revision==UINT32_MAX||a->transition_sequence==UINT32_MAX)return "recall revision or assignment counter exhausted";
    bool was_on=app_output_power(d),owned=d->recall_owned;
    Transition *presenter=NULL;Composition *wrapper=NULL;
    const char *error=NULL;
    if(prepared){presenter=prepared->presenter;wrapper=prepared->wrapper;prepared->presenter=NULL;prepared->wrapper=NULL;}
    else error=prepare_presenter(d,&presenter,&wrapper);
    if(error)return error;
    if(!prepared||!prepared->persisted)error=save_output_state(a,d,on,name,light?light:&d->pixels.light);
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
    if(candidate){composition_install_prepared(candidate,&t->target);bind_composition_slot(candidate,1);}
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
const char *app_preparation_admit(App *a,AppOutput *d,PreparedScene *s){
    PreparationRequest *r=&s->request;AudioSourcePlan *plan=&s->retained_sources;
    if(d->composition_revision!=r->revision)return "scene changed during preparation";
    if(r->kind==PREPARE_RECALL&&(d->composition_revision==UINT32_MAX||a->transition_sequence==UINT32_MAX))return "recall revision or assignment counter exhausted";
    bool replace_slot=r->kind==PREPARE_SLOT||r->kind==PREPARE_COMPOSITION||r->kind==PREPARE_PRESET;
    const char *error=NULL;
    if(r->kind==PREPARE_RECALL||r->kind==PREPARE_TRANSITION||(replace_slot&&r->slot)){
        error=prepare_presenter(d,&s->presenter,&s->wrapper);if(error)return error;
    }
    if(r->kind==PREPARE_RECALL){
        /* A cut needs only the candidate. A fade retains the old destination;
           manual A/B playback may advance before persistence completes. */
        if(s->transition_spec.duration_ms>0&&(d->recall_owned||app_output_power(d))){
            if(d->transition){
                if(!d->recall_owned)error=plan_composition_sources(plan,d->transition->compositions[0],true,(SourceDemandChange){0});
                if(!error)error=plan_composition_sources(plan,d->transition->compositions[1],true,(SourceDemandChange){0});
            }else if(d->composition)error=plan_composition_sources(plan,d->composition,true,(SourceDemandChange){0});
            else if(d->vm.portable&&d->vm.playing)error=audio_source_plan_add(plan,d->vm.portable->source_scope);
        }
        if(!error)error=audio_source_plan_acquire(plan);
    }else if(r->kind==PREPARE_LAYER||(r->kind==PREPARE_RESET&&r->layer[0])){
        CompositionLayer *layer=composition_layer(output_composition(d,r->slot),r->layer);
        if(!layer||layer->vm.generation!=r->generation)return "layer changed during preparation";
        error=reserve_output_sources(d,(SourceDemandChange){.manual=true,.exclude_vm=&layer->vm},plan);
    }else if(r->kind==PREPARE_TRANSITION)error=reserve_output_sources(d,(SourceDemandChange){.manual=true,.presenter=true,.replace_transition=true},plan);
    else if(replace_slot&&(r->slot||d->transition))error=reserve_output_sources(d,(SourceDemandChange){.manual=true,.power=true,.exclude=output_composition(d,r->slot)},plan);
    if(error)return error;
    /* Commit canonical provider policies while the scopes are still alive.
       Only provider references survive across persistence ticks; no borrowed
       scope/config pointer is read after this admission phase. */
    audio_source_plan_commit(plan);plan->count=0;
    return NULL;
}
int app_preparation_persist(App *a,AppOutput *d,PreparedScene *s,StoreWrite *write,unsigned *index,uint8_t *scratch,const char **error){
    PreparationRequest *r=&s->request;
    if(write->store){int status=store_write_step(write);if(status<0)*error="scene persistence failed";if(status>0){(*index)++;s->persisted=true;}return status;}
    if(*index){s->persisted=true;return 1;}
    StoreType type;const char *name;const void *data;size_t size;
    if(r->kind==PREPARE_PRESET_SAVE){
        if(a->store.generation!=r->catalog_revision){*error="preset catalogue changed during preparation";return -1;}
        type=STORE_PRESET;name=r->name;data=s->document;size=s->document_size;
    }else if(r->kind==PREPARE_RECALL&&!r->boot){
        type=STORE_OUTPUT_STATE;name=d->hardware.id;data=scratch;size=output_state_document(a,d,r->on,r->name,&r->light,scratch);
        if(!size){s->persisted=true;return 1;}
    }else{s->persisted=true;return 1;}
    if(!store_write_begin(&a->store,write,type,name,data,(uint32_t)size)){*error="scene storage unavailable, busy or full";return -1;}
    return 0;
}
static const char *commit_prepared_scene(App *a,AppOutput *d,PreparedScene *s){
    PreparationRequest *r=&s->request;
    if(d->composition_revision!=r->revision)return "scene changed during preparation";
    if(r->kind==PREPARE_PRESET_SAVE){
        if(!s->persisted)return "preset persistence incomplete";
        bool ok=s->persisted;resume_clock(a);
        return ok?NULL:"preset storage unavailable, full or write failed";
    }
    const LightControls *light=r->light_attached&&d->light_revision==r->light_revision?&r->light:NULL;
    if(s->direct)inputs_attach(s->direct->portable,&a->inputs,&d->pixels.light);
    if(s->composition)for(unsigned n=0;n<s->composition->layer_count;n++){
        VmInstance *vm=&s->composition->layers[n].vm;inputs_attach(vm->portable,&a->inputs,&d->pixels.light);vm->send=vm_send;vm->transport=a;
    }
    if(s->transition){inputs_attach(s->transition->vm.portable,&a->inputs,&d->pixels.light);s->transition->vm.send=vm_send;s->transition->vm.transport=a;}
    if(s->paint){inputs_attach(s->paint->vm.portable,&a->inputs,&d->pixels.light);s->paint->vm.send=vm_send;s->paint->vm.transport=a;}
    if(s->presenter&&r->kind!=PREPARE_RECALL){attach_presenter(a,d,s->presenter,s->wrapper);s->presenter=NULL;s->wrapper=NULL;}
    if(r->kind==PREPARE_TRANSITION){
        Transition *t=d->transition;transition_invalidate(t);
        for(unsigned n=0;n<2;n++)transition_program_destroy(t->programs[n]);
        t->programs[0]=s->transition;t->programs[1]=s->paint;s->transition=s->paint=NULL;
        t->effect=r->effect;t->seed=r->seed;d->recall_owned=false;++d->composition_revision;return NULL;
    }
    if(r->kind==PREPARE_RECALL){
        bool booting=a->booting;a->booting=r->boot;
        const char *error=begin_recall(a,d,s->composition,s->transition,s->transition_spec.duration_ms,r->on,r->name,r->entry.crc,light,s);
        a->booting=booting;
        if(error)return error;
        s->composition=NULL;s->transition=NULL;return NULL;
    }
    if(r->kind==PREPARE_LAYER||(r->kind==PREPARE_RESET&&r->layer[0])){
        Composition *composition=output_composition(d,r->slot);CompositionLayer *layer=composition_layer(composition,r->layer);
        if(!layer||layer->vm.generation!=r->generation)return "layer changed during preparation";
        transition_invalidate(d->transition);composition_invalidate(composition);control_destroy(&layer->vm);
        layer->vm=*s->direct;demo_free(s->direct);s->direct=NULL;
        layer->vm.target=&layer->target;layer->vm.send=vm_send;layer->vm.transport=a;
        strcpy(layer->vm.layer,layer->id);strcpy(layer->vm.slot,r->slot?"B":"A");vm_rebind(&layer->vm);
        layer->vm.playing=r->play;layer->vm.last_frame=platform_time_us();layer->vm.next_frame=layer->vm.last_frame;
        strcpy(layer->program,r->name);layer->crc=r->entry.crc;composition_modified(composition);d->recall_owned=false;
        if(r->kind==PREPARE_LAYER)composition->revision=++d->composition_revision;
        return NULL;
    }
    if(r->kind==PREPARE_RESET&&d->vm.generation!=r->generation)return "program changed during preparation";
    if(r->kind==PREPARE_DIRECT||r->kind==PREPARE_RESET){
        bool uploading=d->vm.uploading;clear_output_composition(d);control_destroy(&d->vm);d->vm=*s->direct;d->vm.uploading=uploading;demo_free(s->direct);s->direct=NULL;
        vm_rebind(&d->vm);d->vm.send=vm_send;d->vm.transport=a;d->vm.playing=r->play;
        d->vm.last_frame=platform_time_us();d->vm.next_frame=d->vm.last_frame;
        d->vm.frame_ready=d->vm.control.initialized;
        if(r->play)d->power_on=true;
        strcpy(d->active_program,r->name);d->active_crc=r->name[0]?r->entry.crc:0;
        return NULL;
    }
    apply_composition_slot(d,s->composition,r->slot);s->composition=NULL;
    return NULL;
}
const char *app_preparation_commit(App *a,AppOutput *d,PreparedScene *s){
    const char *error=commit_prepared_scene(a,d,s);
    if(!error)sync_output_audio_sources(d);
    audio_source_plan_release(&s->retained_sources);
    return error;
}
static bool transition_manage(App *a,AppOutput *d,uint16_t id,const Json *j,const char *op){
    if(strncmp(op,"transition_",11))return false;
    static const char *const fields[]={"op","output","expected_revision","expected_assignment","binding","progress","target","duration_ms","interval_ms","source","assignment","effect","seed","mask","paint","program","settings","inputs","sources",NULL};
    if(!json_fields(j,0,fields)||!d->pixels.count){error_reply(a,id,STATUS_CONFIG,"invalid_transition","invalid transition request or output");return true;}
    if(!strcmp(op,"transition_get")){Writer w={.app=a,.id=id};transition_state(&w,d);flush(&w);reply(a,id,STATUS_OK,0,"A/B transition state");return true;}
    const char *error=NULL;double value=0,duration=0,interval=0;char source[OBJECT_NAME_LIMIT+1]="";uint32_t assignment=0,seed=0;
    bool swap=!strcmp(op,"transition_swap"),select=!strcmp(op,"transition_select"),set=!strcmp(op,"transition_set"),run=!strcmp(op,"transition_run"),assign=!strcmp(op,"transition_assign"),update=!strcmp(op,"transition_update"),hold=!strcmp(op,"transition_hold"),bind=!strcmp(op,"transition_bind");
    TransitionEffect effect=TRANSITION_CROSSFADE;InputPort *input=NULL;
    if(!swap&&!select&&!set&&!run&&!assign&&!update&&!hold&&!bind)error="unknown transition operation";
    if(bind){
        uint32_t expected;int binding=json_get(j,0,"binding");
        if(!json_u32(j,json_get(j,0,"expected_assignment"),&expected)||expected!=(d->transition?d->transition->assignment:0))error="stale driver assignment";
        else{
            input=demo_calloc(1,sizeof(*input));
            if(!input)error="transition input exceeds peak memory budget";
            else{
                strcpy(input->name,"progress");input->type=T_FLOAT;input->bounds=3;input->maximum.f=1;input->light=&d->pixels.light;
                if(d->transition)transition_update(d->transition,platform_time_us());
                input->initial.f=input->snapshot.f=input->driver.value.f=d->transition?(float)(d->transition->progress/100):0;
                error=input_port_bind(&a->inputs,input,j,binding,platform_time_us());
                if(!error&&input->driver.kind==INPUT_SOURCE&&json_get(j,binding,"fallback")<0)input->driver.hold=true;
            }
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
        if(effect!=TRANSITION_CUSTOM&&(json_get(j,0,"mask")>=0||json_get(j,0,"paint")>=0))error="mask and paint require the custom effect";
        if(!error){PreparationRequest pending={.kind=PREPARE_TRANSITION,.effect=effect,.seed=seed};(void)submit_scene(a,d,id,pending,j->text,j->size);return true;}
    }
    if(!error&&(swap||set||run||assign||hold||bind)&&a->transition_sequence==UINT32_MAX)error="driver assignment counter exhausted";
    AudioSourcePlan plan={0};Transition *presenter=NULL;Composition *wrapper=NULL;
    if(!error)error=prepare_presenter(d,&presenter,&wrapper);
    if(!error)error=reserve_output_sources(d,(SourceDemandChange){.manual=true,.presenter=true,.power=set||run||assign||bind},&plan);
    if(!error){
        audio_source_plan_commit(&plan);preparation_cancel_output(a,d->hardware.id);
        if(presenter){attach_presenter(a,d,presenter,wrapper);presenter=NULL;wrapper=NULL;}
        Transition *t=d->transition;
        if(swap||set||run||assign||hold||bind)t->assignment=a->transition_sequence;
        d->recall_owned=false;
        if(set||run||assign||update||bind)d->power_on=true;
        if(swap){transition_invalidate(t);swap_compositions(d);(void)transition_set(t,t->progress);++d->composition_revision;}
        else if(set&&!transition_set(t,value))error="driver assignment counter exhausted";
        else if(run&&!transition_run_interval(t,value,duration,interval))error="driver assignment counter exhausted";
        else if(assign&&!transition_assign(t,source))error="driver assignment counter exhausted";
        else if(hold)transition_hold(t);
        else if(bind){transition_bind_port(t,&a->inputs,input);input=NULL;}
        if(!error&&(swap||set||run||assign||hold||bind))a->transition_sequence=t->assignment;
        finish_output_sources(d,&plan);
    }
    audio_source_plan_release(&plan);demo_free(input);composition_destroy(wrapper);transition_destroy(presenter);
    if(error)error_reply(a,id,STATUS_CONFIG,"invalid_transition",error);
    else{Writer w={.app=a,.id=id};transition_state(&w,d);flush(&w);reply(a,id,STATUS_OK,0,"transition updated");}
    return true;
}
/* One saved preset invocation supplies selection, power and startup transitions.
 * OFF needs only that recipe's transition, without allocating its animation layers. */


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
    if(!error&&on){
        const StoreEntry *entry=store_find(&a->store,STORE_PRESET,name);
        if(!entry)error="stored preset does not exist";
        else{
            PreparationRequest pending={.kind=PREPARE_RECALL,.play=true,.on=true,.boot=true,.entry=*entry,.light=d->pixels.light,.revision=d->composition_revision,.light_revision=d->light_revision};strcpy(pending.name,name);uint32_t operation;
            error=preparation_submit(a,d,&pending,NULL,0,&operation);
        }
    }else if(!error){
        const StoreEntry *entry=store_find(&a->store,STORE_PRESET,name);
        if(name[0]&&!entry)error="stored preset does not exist";
        else{d->recall_owned=true;d->power_on=false;strcpy(d->selected_preset,name);d->selected_preset_crc=entry?entry->crc:0;}
    }
    if(error)startup_error(a->boot_error,sizeof(a->boot_error),&d->hardware,error);
}
static bool submit_recall(App *a,AppOutput *d,uint16_t id,const char *name,bool on,const LightControls *light){
    PreparationRequest pending={.kind=PREPARE_RECALL,.play=true,.on=on,.light_attached=light!=NULL};
    if(light)pending.light=*light;
    strcpy(pending.name,name);
    const StoreEntry *entry=store_find(&a->store,STORE_PRESET,name);
    if(entry)pending.entry=*entry;
    else if(on||name[0]){error_reply(a,id,STATUS_LOAD,"recall_failed","stored preset does not exist");return false;}
    return submit_scene(a,d,id,pending,NULL,0);
}
static bool recall_command(App *a,AppOutput *d,uint16_t id,const Command *command){
    bool power=command->kind==COMMAND_POWER;
    uint32_t revision=command->revision;bool on=command->on;char name[OBJECT_NAME_LIMIT+1]="";
    if(!d->pixels.count||revision!=d->composition_revision){
        error_reply(a,id,STATUS_CONFIG,"stale_revision","valid output and current composition revision required");return true;
    }
    if(power){
        if(!on)preparation_cancel_output(a,d->hardware.id);
        const char *selected=app_output_selected_preset(a,d);
        if(on==app_output_power(d)&&(!on||!(command->fields&COMMAND_PRESET)||(selected&&!strcmp(selected,command->preset)))){
            preparation_cancel_output(a,d->hardware.id);
            const char *error=NULL;
            if(a->store.writer)d->light_save_due=platform_time_us()+750000;
            else if(!on||selected)error=save_output_state(a,d,on,selected,&d->pixels.light);
            if(error)error_reply(a,id,STATUS_STORAGE,"state_save_failed",error);
            else reply(a,id,STATUS_OK,(int32_t)revision,"power target unchanged");
            return true;
        }
        if(!selected&&d->transition&&d->transition->progress>0)selected=composition_preset(a,d->transition->compositions[1]);
        if(selected)snprintf(name,sizeof(name),"%s",selected);
        if(on){
            if(command->fields&COMMAND_PRESET)strcpy(name,command->preset);
            else if(!name[0])snprintf(name,sizeof(name),"%s",d->hardware.ha_default_preset);
        }
    }else{
        strcpy(name,command->preset);
        const char *selected=app_output_selected_preset(a,d);
        if(app_output_power(d)&&selected&&!strcmp(selected,name)){
            preparation_cancel_output(a,d->hardware.id);
            const char *error=NULL;
            if(a->store.writer)d->light_save_due=platform_time_us()+750000;
            else error=save_output_state(a,d,true,name,&d->pixels.light);
            if(error)error_reply(a,id,STATUS_STORAGE,"state_save_failed",error);
            else reply(a,id,STATUS_OK,(int32_t)revision,"preset target unchanged");
            return true;
        }
    }
    (void)submit_recall(a,d,id,name,on,NULL);
    return true;
}
static void refresh_light_frame(AppOutput *d){
    /* Release the physical transfer, keeping every unscaled scene buffer and
       invocation intact. A paused scene can therefore be dimmed immediately. */
    const void *previous=runtime_enter(&d->vm);
    bool composition_active=d->composition&&d->composition->command_active;
    bool transition_active=d->transition&&d->transition->command_active;
    if(d->composition)d->composition->command_active=true;
    if(d->transition)d->transition->command_active=true;
    vm_output_cancel(&d->vm);
    if(d->composition&&d->composition->pending){
        d->pixels.type->cancel(d->pixels.driver);d->composition->pending=false;
    }
    if(d->transition&&d->transition->pending){
        d->pixels.type->cancel(d->pixels.driver);d->transition->pending=false;
    }
    snprintf(d->light_error,sizeof(d->light_error),"%s",pixels_show(&d->pixels)?"":"native light output failed");
    /* A synchronous driver can service other outputs while draining. This
       output must not submit a second frame from that nested checkpoint. */
    if(d->composition)d->composition->command_active=composition_active;
    if(d->transition)d->transition->command_active=transition_active;
    runtime_leave(previous);
}
static bool light_command_execute(App *a,AppOutput *d,uint16_t id,const Command *command){
    if(!d->pixels.count||d->hardware.indicators){
        error_reply(a,id,STATUS_CONFIG,"invalid_light","light controls require an animation output");return true;
    }
    if(command->kind==COMMAND_LIGHT_SET){
        if(command->revision!=d->light_revision||command->revision==UINT32_MAX){
            error_reply(a,id,STATUS_CONFIG,"stale_revision","expected light revision must match");return true;
        }
        LightControls next=d->pixels.light;bool on=app_output_power(d);
        if(command->fields&COMMAND_BRIGHTNESS)next.brightness=command->brightness;
        if(command->fields&COMMAND_COLOR)next.color=command->color;
        if(command->fields&COMMAND_POWER_VALUE)on=command->on;
        const char *selected=app_output_selected_preset(a,d);
        char name[OBJECT_NAME_LIMIT+1]="";
        if(selected)strcpy(name,selected);
        if(command->fields&COMMAND_PRESET){strcpy(name,command->preset);if(!(command->fields&COMMAND_POWER_VALUE))on=true;}
        bool scene=(command->fields&(COMMAND_POWER_VALUE|COMMAND_PRESET))!=0;
        if(scene&&command->composition_revision!=d->composition_revision){
            error_reply(a,id,STATUS_CONFIG,"invalid_light","expected composition revision must match");return true;
        }
        bool recall=scene&&(on!=app_output_power(d)||((command->fields&COMMAND_PRESET)&&(!selected||strcmp(selected,name))));
        if(recall&&on&&!name[0])strcpy(name,d->hardware.ha_default_preset);
        if(scene&&(!on||!recall))preparation_cancel_output(a,d->hardware.id);
        if(recall){(void)submit_recall(a,d,id,name,on,&next);return true;}
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
static bool indicator_command(App *,AppOutput *,uint16_t,const Command *);
static bool key_feedback_command(App *,uint16_t,const Command *);
static void execute_command(App *a,uint16_t id,const Command *command){
    if(command->kind==COMMAND_KEY_GET||command->kind==COMMAND_KEY_SET){key_feedback_command(a,id,command);return;}
    AppOutput *d=app_output(a,command->output);
    if(!d){error_reply(a,id,STATUS_PROTOCOL,"invalid_output","output is not configured");return;}
    if(command->kind>=COMMAND_INDICATOR_GET){indicator_command(a,d,id,command);return;}
    if(d->hardware.indicators){error_reply(a,id,STATUS_CONFIG,"indicator_owned","indicator pixels are controlled through indicator_set");return;}
    if(command->kind==COMMAND_RECALL||command->kind==COMMAND_POWER)recall_command(a,d,id,command);
    else light_command_execute(a,d,id,command);
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
    if(!saving){
        if(!entry){error_reply(a,id,STATUS_STORAGE,"not_found","stored preset does not exist");return true;}
        PreparationRequest pending={.kind=PREPARE_PRESET,.slot=request_slot(j),.play=true,.entry=*entry};strcpy(pending.name,name);
        (void)submit_scene(a,d,id,pending,NULL,0);return true;
    }
    PreparationRequest pending={.kind=PREPARE_PRESET_SAVE,.catalog_revision=revision};strcpy(pending.name,name);
    if(submit_scene(a,d,id,pending,a->staging,a->expected)){a->staged=false;a->upload_owner=0;}
    return true;
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
        PreparationRequest pending={.kind=PREPARE_COMPOSITION,.slot=slot,.play=true};
        if(submit_scene(a,d,id,pending,a->staging,a->expected)){a->staged=false;a->upload_owner=0;}
        return true;
    }
    if(!strcmp(op,"composition_clear")){
        AudioSourcePlan plan={0};const char *error=reserve_output_sources(d,(SourceDemandChange){.manual=true,.exclude=s},&plan);
        if(error){error_reply(a,id,STATUS_CONFIG,"sources_unavailable",error);return true;}
        audio_source_plan_commit(&plan);
        if(d->transition){transition_invalidate(d->transition);d->transition->compositions[slot]=NULL;if(!slot)d->composition=NULL;composition_destroy(s);}
        else if(!slot){clear_output_composition(d);d->vm.output=&d->pixels;pixels_clear(&d->pixels);pixels_show(&d->pixels);d->power_on=false;}
        d->recall_owned=false;finish_output_sources(d,&plan);
        ++d->composition_revision;reply(a,id,STATUS_OK,(int32_t)d->composition_revision,"composition removed");return true;
    }
    if(!strcmp(op,"composition_pause")||!strcmp(op,"composition_resume")){
        if(!s){error_reply(a,id,STATUS_CONFIG,"unknown_slot","composition is not configured");return true;}
        bool resume=!strcmp(op,"composition_resume");AudioSourcePlan plan={0};
        const char *error=reserve_output_sources(d,(SourceDemandChange){.manual=true,.power=resume,.resume=resume?s:NULL,.exclude=resume?NULL:s},&plan);
        if(error){error_reply(a,id,STATUS_CONFIG,"sources_unavailable",error);return true;}
        audio_source_plan_commit(&plan);
        composition_play(s,resume);s->revision=++d->composition_revision;
        if(resume)d->power_on=true;
        d->recall_owned=false;
        finish_output_sources(d,&plan);
        reply(a,id,STATUS_OK,(int32_t)d->composition_revision,"composition playback updated");return true;
    }
    char layer_id[OBJECT_NAME_LIMIT+1];
    if(!s||!json_string(j,json_get(j,0,"layer"),layer_id,sizeof(layer_id))){error_reply(a,id,STATUS_CONFIG,"unknown_layer","composition layer is required");return true;}
    CompositionLayer *layer=composition_layer(s,layer_id);
    if(!layer){error_reply(a,id,STATUS_CONFIG,"unknown_layer","composition layer is not configured");return true;}
    bool blend=!strcmp(op,"composition_layer_blend"),opacity=!strcmp(op,"composition_layer_opacity"),remove=!strcmp(op,"composition_layer_remove");
    if(blend||opacity||remove){
        uint8_t mode=0;uint32_t amount=0;
        if(blend&&!layer_blend(j,json_get(j,0,"blend"),&mode)){error_reply(a,id,STATUS_CONFIG,"invalid_blend","layer blend must be over, add, screen or multiply");return true;}
        if(opacity&&(!json_u32(j,json_get(j,0,"opacity"),&amount)||amount>255)){error_reply(a,id,STATUS_CONFIG,"invalid_opacity","layer opacity must be 0..255");return true;}
        AudioSourcePlan plan={0};const char *error=reserve_output_sources(d,(SourceDemandChange){.manual=true,.exclude_vm=remove?&layer->vm:NULL},&plan);
        if(error){error_reply(a,id,STATUS_CONFIG,"sources_unavailable",error);return true;}
        audio_source_plan_commit(&plan);transition_invalidate(d->transition);composition_invalidate(s);
        if(remove)composition_remove_layer(s,(unsigned)(layer-s->layers));
        else if(blend)layer->blend=mode;
        else layer->opacity=(uint8_t)amount;
        composition_modified(s);s->revision=++d->composition_revision;d->recall_owned=false;finish_output_sources(d,&plan);
        reply(a,id,STATUS_OK,(int32_t)d->composition_revision,remove?"layer removed":blend?"layer blend updated":"layer opacity updated");return true;
    }
    if(!strcmp(op,"composition_layer_activate")){
        uint32_t generation;char name[OBJECT_NAME_LIMIT+1];
        if(!json_u32(j,json_get(j,0,"expected_generation"),&generation)||generation!=layer->vm.generation){error_reply(a,id,STATUS_CONFIG,"stale_generation","expected layer generation must match");return true;}
        if(!json_string(j,json_get(j,0,"name"),name,sizeof(name))||!object_name_valid(name)){error_reply(a,id,STATUS_CONFIG,"invalid_name","program name required");return true;}
        const StoreEntry *entry=store_find(&a->store,STORE_ANIMATION,name);
        if(!entry){error_reply(a,id,STATUS_LOAD,"preparation_failed","program does not exist");return true;}
        PreparationRequest pending={.kind=PREPARE_LAYER,.entry=*entry,.slot=slot,.play=true,.generation=generation};strcpy(pending.layer,layer->id);strcpy(pending.name,name);
        (void)submit_scene(a,d,id,pending,j->text,j->size);return true;
    }
    error_reply(a,id,STATUS_PROTOCOL,"unknown_operation","unknown composition operation");return true;
}
static bool composition_output_command(App *a,AppOutput *d,uint8_t command,uint16_t id,uint32_t delta){
    Transition *t=d->transition;
    if(t){
        if(command==CMD_FRAME){const char *error=transition_frame(t,delta);if(error){reply(a,id,STATUS_EXECUTION,0,error);return true;}
            output_command_power(d,command);
            for(size_t offset=0;offset<(size_t)d->pixels.count*3;offset+=FRAME_LIMIT){size_t n=(size_t)d->pixels.count*3-offset;if(n>FRAME_LIMIT)n=FRAME_LIMIT;a->send(a->transport,RESP_PIXELS,id,d->pixels.rgb+offset,n);}reply(a,id,STATUS_OK,(int32_t)d->pixels.count,"transition frame rendered");return true;}
        if(command==CMD_PLAY||command==CMD_STOP){
            AudioSourcePlan plan={0};const char *error=command==CMD_PLAY?reserve_output_sources(d,(SourceDemandChange){.manual=true,.power=true,.play=true},&plan):NULL;
            if(error){reply(a,id,STATUS_CONFIG,0,error);return true;}
            preparation_cancel_output(a,d->hardware.id);
            audio_source_plan_commit(&plan);output_command_power(d,command);d->recall_owned=false;transition_play(t,command==CMD_PLAY);
            finish_output_sources(d,&plan);
            if(command==CMD_STOP){transition_invalidate(t);if(t->pending){d->pixels.type->cancel(d->pixels.driver);t->pending=false;}pixels_clear(&d->pixels);pixels_show(&d->pixels);}
            reply(a,id,STATUS_OK,0,command==CMD_PLAY?"A/B resumed":"A/B stopped");return true;
        }
        if(command==CMD_RESET){output_command_power(d,command);clear_output_composition(d);d->vm.output=&d->pixels;++d->composition_revision;pixels_clear(&d->pixels);pixels_show(&d->pixels);reply(a,id,STATUS_OK,0,"A/B unloaded");return true;}
        if(command==CMD_STATUS){char message[FRAME_LIMIT-8];
        StatusWriter w={message,sizeof(message),0};
        status_field(&w,"version",LIGHTVM_VERSION,SIZE_MAX);
        status_field(&w,"code_storage","flash",SIZE_MAX);
        status_integer(&w,"module_limit",MODULE_LIMIT);
        status_integer(&w,"heap",vm_heap_used());
        status_integer(&w,"heap_peak",vm_heap_peak());
        status_integer(&w,"heap_limit",vm_heap_limit());
        status_integer(&w,"pixels",d->pixels.count);
        status_field(&w,"mode","transition",SIZE_MAX);
        status_integer(&w,"frames",t->frames);
        status_integer(&w,"fg10",runtime_epoch_stats().foreground_over_10ms);
        status_integer(&w,"io10",runtime_epoch_stats().ingress_over_10ms);
        status_field(&w,"animation_error",t->error[0]?t->error:"-",99);
        reply(a,id,STATUS_OK,0,message);return true;}
        return false;
    }
    Composition *s=d->composition;if(!s)return false;
    if(command==CMD_STATUS){
        char message[FRAME_LIMIT-8];const PlatformInfo *info=platform_info();
        StatusWriter w={message,sizeof(message),0};
        status_field(&w,"version",LIGHTVM_VERSION,SIZE_MAX);
        status_integer(&w,"protocol",1);
        status_field(&w,"platform",info->platform,16);
        status_field(&w,"board",info->board,24);
        status_field(&w,"output","ws2812",SIZE_MAX);
        status_integer(&w,"ready",1);
        status_integer(&w,"uploading",d->vm.uploading);
        status_integer(&w,"module_limit",MODULE_LIMIT);
        status_integer(&w,"heap",vm_heap_used());
        status_integer(&w,"heap_peak",vm_heap_peak());
        status_integer(&w,"heap_limit",vm_heap_limit());
        status_integer(&w,"pixels",s->count);
        status_integer(&w,"frame_interval_us",FRAME_INTERVAL_US);
        status_field(&w,"mode","composition",SIZE_MAX);
        status_integer(&w,"frames",s->frames);
        status_integer(&w,"layers",s->layer_count);
        status_integer(&w,"fg10",runtime_epoch_stats().foreground_over_10ms);
        status_integer(&w,"io10",runtime_epoch_stats().ingress_over_10ms);
        status_field(&w,"animation_error",s->error[0]?s->error:"-",99);
        reply(a,id,STATUS_OK,0,message);return true;
    }
    if(command==CMD_FRAME){
        const char *error=composition_frame(s,delta);if(error){reply(a,id,STATUS_EXECUTION,0,error);return true;}
        output_command_power(d,command);
        for(size_t offset=0;offset<(size_t)s->count*3;offset+=FRAME_LIMIT){size_t n=(size_t)s->count*3-offset;if(n>FRAME_LIMIT)n=FRAME_LIMIT;a->send(a->transport,RESP_PIXELS,id,s->output->rgb+offset,n);}
        reply(a,id,STATUS_OK,(int32_t)s->count,"composition frame rendered");return true;
    }
    if(command==CMD_PLAY||command==CMD_STOP){
        AudioSourcePlan plan={0};const char *error=command==CMD_PLAY?reserve_output_sources(d,(SourceDemandChange){.power=true,.resume=s},&plan):NULL;
        if(error){reply(a,id,STATUS_CONFIG,0,error);return true;}
        preparation_cancel_output(a,d->hardware.id);
        audio_source_plan_commit(&plan);output_command_power(d,command);
        if(command==CMD_STOP)composition_invalidate(s);
        composition_play(s,command==CMD_PLAY);
        finish_output_sources(d,&plan);
        if(command==CMD_STOP){if(s->pending){s->output->type->cancel(s->output->driver);s->pending=false;}pixels_clear(s->output);pixels_show(s->output);}
        reply(a,id,STATUS_OK,0,command==CMD_PLAY?"composition resumed":"composition stopped");return true;
    }
    if(command==CMD_RESET){output_command_power(d,command);clear_output_composition(d);d->vm.output=&d->pixels;++d->composition_revision;pixels_clear(&d->pixels);pixels_show(&d->pixels);reply(a,id,STATUS_OK,0,"composition unloaded");return true;}
    return false;
}
/* These independent commands must not keep network/configuration scratch
 * live during an unrelated program activation or catalogue operation. */
static __attribute__((noinline)) void network_get(App *a,uint16_t id){
    /* This reply buffer must not remain on the network-validation call path. */
    char *data=platform_calloc(4096,1);if(!data){reply(a,id,STATUS_CONFIG,0,"network reply allocation failed");return;}
    size_t count=network_json(a->network_settings.pending?&a->network_settings.candidate:&a->network.config,false,data,4096);
    Writer w={.app=a,.id=id};bytes(&w,data,count);flush(&w);platform_free(data);
    reply(a,id,STATUS_OK,(int32_t)a->store.network_revision,"network configuration; passwords omitted");
}
static __attribute__((noinline)) bool manage_network(App *a,uint16_t id,const Arguments *args) {
    if (!strcmp(args->op,"network_get")) {network_get(a,id);return true;}
    if (!strcmp(args->op,"network_status")) {
        Network *n=&a->network; Writer w={.app=a,.id=id};
        literal(&w,"{\"state\":"); quoted(&w,a->network_settings.pending?"reconfiguring":network_state_name(n->state));
        literal(&w,",\"mode\":"); quoted(&w,network_mode(n)==NET_MODE_AP?"ap":"station");
        literal(&w,",\"initial_setup\":"); literal(&w,n->initial_setup?"true":"false");
        literal(&w,",\"recovery\":"); literal(&w,n->recovery?"true":"false");
        literal(&w,",\"ssid\":"); quoted(&w,n->state==NET_AP_ACTIVE?n->ap_ssid:n->state==NET_CONNECTED && n->connected_index>=0?n->config.networks[n->connected_index].ssid:"");
        literal(&w,",\"ip\":"); quoted(&w,n->ip);
        literal(&w,",\"error\":"); quoted(&w,a->network_settings.error[0]?a->network_settings.error:n->error);
        literal(&w,",\"retry_ms\":"); uint64_t now=platform_time_us(); number(&w,n->state==NET_BACKOFF && n->retry_at>now?(uint32_t)((n->retry_at-now)/1000u):0);
        literal(&w,",\"revision\":"); number(&w,a->store.network_revision); literal(&w,",\"http_port\":"); number(&w,network_http_port(n));
        literal(&w,",\"mqtt_connected\":");literal(&w,a->mqtt_connected?"true":"false");
        literal(&w,",\"mqtt_error\":");quoted(&w,a->mqtt_error);
        literal(&w,"}"); flush(&w); reply(a,id,STATUS_OK,0,"network status"); return true;
    }
    if (!strcmp(args->op,"network_validate") || !strcmp(args->op,"network_commit")) {
        if (!a->staged || a->object_kind!=3 || a->upload_owner!=a->source) { error_reply(a,id,STATUS_UPLOAD,"no_candidate","stage a network configuration in this session first"); return true; }
        NetworkConfig config; char error[160];
        if (!network_parse(a->staging,a->expected,&a->network.config,&config,error,sizeof(error)) ||
            (config.wifi_enabled && !LIGHTVM_WIFI) || (config.http_enabled && !LIGHTVM_HTTP) || (config.mqtt.enabled && !LIGHTVM_MQTT) ||
            ((config.http_enabled || config.mqtt.enabled) && !config.wifi_enabled && strcmp(platform_info()->platform,"host"))) {
            error_reply(a,id,STATUS_CONFIG,"invalid_network",error[0]?error:"selected network feature is unavailable or HTTP/MQTT requires Wi-Fi"); return true;
        }
        if (!strcmp(args->op,"network_validate")) { reply(a,id,STATUS_OK,0,"network configuration valid"); return true; }
        if (!args->has_revision) { error_reply(a,id,STATUS_CONFIG,"stale_revision","network revision must match; wait for pending changes"); return true; }
        enum Status status=app_save_network(a,&config,args->revision,a->source,error,sizeof(error));
        if (status!=STATUS_OK) { error_reply(a,id,status,status==STATUS_STORAGE?"save_failed":status==STATUS_BUSY?"busy":"stale_revision",error); return true; }
        a->staged=false; a->upload_owner=0;
        reply(a,id,STATUS_OK,(int32_t)a->store.network_revision,a->network.recovery?"network saved for next boot; recovery AP retained":"network saved; reconnect after settings apply"); return true;
    }
    return false;
}
int app_preparation_config_persist(App *a,const PreparationRequest *r,const void *data,size_t size,StoreWrite *write,unsigned *index,uint8_t *scratch,const char **error){
    if(r->kind!=PREPARE_CONFIG_SAVE)return 1;
    if(r->active_revision!=a->revision||r->saved_revision!=a->store.config_revision){*error="configuration changed during preparation";return -1;}
    if(write->store){
        int status=store_write_step(write);if(status<0){*error="configuration persistence failed";return -1;}
        if(!status)return 0;
        if(*index==a->output_count)return 1;
        (*index)++;return 0;
    }
    if(*index<a->output_count){
        AppOutput *d=a->outputs[*index];
        const char *selected=app_output_selected_preset(a,d);
        if(a->network.recovery||d->hardware.startup_mode!=STARTUP_RESTORE_LAST||!d->recall_owned||!selected){(*index)++;return 0;}
        size_t bytes=output_state_document(a,d,d->power_on,selected,&d->pixels.light,scratch);
        if(!bytes){(*index)++;return 0;}
        if(!store_write_begin(&a->store,write,STORE_OUTPUT_STATE,d->hardware.id,scratch,(uint32_t)bytes)){*error="output state storage unavailable or busy";return -1;}
    }else if(!store_write_begin(&a->store,write,STORE_CONFIGURATION,STORE_CONFIG,data,(uint32_t)size)){*error="configuration storage unavailable or busy";return -1;}
    return 0;
}
const char *app_preparation_config_commit(App *a,const PreparationRequest *r,HardwareConfig *config,const void *data,size_t size){
    if(r->active_revision!=a->revision)return "active configuration changed during preparation";
    if(r->kind==PREPARE_CONFIG_VALIDATE)return NULL;
    if(r->kind==PREPARE_CONFIG_APPLY){
        if(!config_apply(a,config,data,(uint32_t)size))return "native output initialization failed; active configuration is empty";
        a->boot_error[0]=0;return NULL;
    }
    return NULL; /* Durable publication finished in the preceding bounded step. */
}
static __attribute__((noinline)) bool manage_configuration(App *a,uint16_t id,const Arguments *args) {
    bool validate=!strcmp(args->op,"config_validate"),apply=!strcmp(args->op,"config_apply"),save=!strcmp(args->op,"config_save");
    if(!validate&&!apply&&!save)return false;
    if(!save&&(!a->staged||a->object_kind!=1||a->upload_owner!=a->source)){error_reply(a,id,STATUS_CONFIG,"no_candidate","upload and finish a configuration first");return true;}
    if(!validate&&(!args->has_revision||!settings_revision_matches(a->revision,args->revision,NULL,0)||(save&&(!args->has_saved_revision||!settings_revision_matches(a->store.config_revision,args->saved_revision,NULL,0))))){
        error_reply(a,id,STATUS_CONFIG,"stale_revision","expected active and saved revisions must match");return true;
    }
    PreparationRequest pending={.kind=validate?PREPARE_CONFIG_VALIDATE:apply?PREPARE_CONFIG_APPLY:PREPARE_CONFIG_SAVE,.active_revision=a->revision,.saved_revision=a->store.config_revision};
    if(submit_scene(a,app_output(a,NULL),id,pending,save?(const void *)a->config_json:a->staging,save?a->config_size:a->expected)&&!save&&!validate){a->staged=false;a->upload_owner=0;}
    return true;
}
static void manage(App *a,uint16_t id,Arguments args) {
    if(manage_network(a,id,&args)||manage_configuration(a,id,&args))return;
    if(!strcmp(args.op,"output_command")){
        AppOutput *d=app_output(a,args.output);
        if(!d){error_reply(a,id,STATUS_CONFIG,"unknown_output","output is not configured");return;}
        if(args.command!=CMD_STATUS&&args.command!=CMD_PLAY&&args.command!=CMD_STOP&&args.command!=CMD_FRAME&&args.command!=CMD_RESET){error_reply(a,id,STATUS_PROTOCOL,"invalid_command","unsupported output command");return;}
        if(args.delta_ms>1000){error_reply(a,id,STATUS_PROTOCOL,"invalid_delta","FRAME requires 0..1000 milliseconds");return;}
        if(args.command==CMD_STOP||args.command==CMD_RESET)preparation_cancel_output(a,d->hardware.id);
        if(composition_output_command(a,d,(uint8_t)args.command,id,args.delta_ms))return;
        if(args.command==CMD_RESET){
            d->active_program[0]=0;d->active_crc=0;
            if(a->upload_owner&&a->upload_output==d&&d->vm.uploading){store_upload_cancel(&a->upload);a->upload_owner=0;d->vm.uploading=false;}
        }
        uint8_t delta[4];write_u32(delta,args.delta_ms);
        vm_command(&d->vm,(uint8_t)args.command,id,args.command==CMD_FRAME?delta:NULL,args.command==CMD_FRAME?4:0);
        if(a->last_code==STATUS_OK){if(args.command==CMD_PLAY)preparation_cancel_output(a,d->hardware.id);output_command_power(d,(uint8_t)args.command);}
        return;
    }
    if (!strcmp(args.op,"session_close")) { app_disconnect(a,a->source); reply(a,id,STATUS_OK,0,"session closed; playback retained"); return; }
    if (!strcmp(args.op,"describe")) { describe(a,id); return; }
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
static bool object_decode(App *a,const Json *j) {
    uint32_t expected,crc; char kind[16],name[33]="";
    static const char *const fields[]={"kind","name","size","crc32",NULL};
    if (!json_fields(j,0,fields) ||
        !json_string(j,json_get(j,0,"kind"),kind,sizeof(kind)) ||
        !json_u32(j,json_get(j,0,"size"),&expected) || !json_u32(j,json_get(j,0,"crc32"),&crc)) return false;
    unsigned object_kind=!strcmp(kind,"config") ? 1u : !strcmp(kind,"program") ? 2u : !strcmp(kind,"network") ? 3u : !strcmp(kind,"composition") ? 4u : !strcmp(kind,"preset") ? 5u : 0u;
    if (!object_kind || expected<(object_kind==2 ? 8u : 1u) || expected>(object_kind==2 ? MODULE_LIMIT : CONFIG_LIMIT)) return false;
    int n=json_get(j,0,"name");
    if ((object_kind==2||object_kind==5) && (!json_string(j,n,name,sizeof(name)) || !object_name_valid(name))) return false;
    if (object_kind!=2 && object_kind!=5 && n>=0) return false;
    if(object_kind==2&&!store_upload_begin(&a->store,&a->upload,STORE_ANIMATION,name,expected,crc))return false;
    if(object_kind!=2)store_upload_cancel(&a->upload);
    a->object_kind=object_kind; a->expected=expected; a->expected_crc=crc; a->received=0;
    memcpy(a->object_name,name,sizeof(name)); a->uploading=true; a->staged=false;
    a->upload_deadline=platform_time_us()+30000000u; return true;
}
static __attribute__((noinline)) bool object_begin(App *a,const uint8_t *p,size_t size){
    Json *j=platform_calloc(1,sizeof(*j));
    bool ok=j&&json_parse(j,p,size)&&object_decode(a,j);
    platform_free(j);return ok;
}
static __attribute__((noinline)) bool validate_object_document(App *a,uint16_t id){
    char error[160];
    if(a->object_kind==1){
        HardwareConfig config;
        if(!hardware_parse(a->staging,a->expected,&config,error,sizeof(error))){error_reply(a,id,STATUS_CONFIG,"invalid_config",error);return false;}
        hardware_destroy(&config);
    }else{
        NetworkConfig config;
        if(!network_parse(a->staging,a->expected,&a->network.config,&config,error,sizeof(error))){a->upload_owner=0;error_reply(a,id,STATUS_CONFIG,"invalid_network",error);return false;}
    }
    return true;
}
static void command_impl(void *context,uint8_t type,uint16_t id,const uint8_t *p,size_t size) {
    App *a=context;
    if(type==CMD_AUDIO_FRAME){
        const char *error=audio_gateway_fragment(&a->audio,a->source,p,size,platform_time_us());
        if(error)error_reply(a,id,STATUS_CONFIG,"invalid_audio_frame",error);
        else reply(a,id,STATUS_OK,0,"audio fragment accepted");
        return;
    }
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
        if (a->object_kind==1||a->object_kind==3) {
            if(!validate_object_document(a,id))return;
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
        Json *request=NULL;
        if(size){
            static const char *const fields[]={"settings","inputs",NULL};
            request=platform_calloc(1,sizeof(*request));
            if(!request||!json_parse(request,p,size)||!json_fields(request,0,fields)){
                platform_free(request);reply(a,id,STATUS_CONFIG,0,"invalid upload activation settings");return;
            }
        }
        platform_free(request);
        if(!store_upload_finish(&a->upload)){reply(a,id,STATUS_UPLOAD,0,"module checksum or flash write failure");return;}
        PreparationRequest pending={.kind=PREPARE_DIRECT,.entry=a->upload.entry};
        (void)submit_scene(a,a->upload_output,id,pending,p,size);
        a->upload_output->vm.uploading=false;a->upload_owner=0;store_upload_cancel(&a->upload);return;
    }
    if(type==CMD_RESET&&!size){preparation_cancel_output(a,app_output(a,NULL)->hardware.id);a->upload_output->vm.uploading=false;a->uploading=false;a->staged=false;app_output(a,NULL)->active_program[0]=0;app_output(a,NULL)->active_crc=0;a->upload_owner=0;store_upload_cancel(&a->upload);}
    AppOutput *d=app_output(a,NULL);
    if(type==CMD_STOP||type==CMD_RESET)preparation_cancel_output(a,d->hardware.id);
    if(d->hardware.indicators&&type!=CMD_STATUS){reply(a,id,STATUS_CONFIG,0,"indicator output is controlled through indicator_set");return;}
    if(d->composition||d->transition){
        if((type==CMD_FRAME&&size!=4)||(type!=CMD_FRAME&&size)){reply(a,id,STATUS_PROTOCOL,0,"invalid composition command payload");return;}
        if(type==CMD_FRAME&&read_u32(p)>1000){reply(a,id,STATUS_PROTOCOL,0,"FRAME requires 0..1000 milliseconds");return;}
        if(composition_output_command(a,d,type,id,type==CMD_FRAME?read_u32(p):0))return;
    }
    vm_command(&d->vm,type,id,p,size);
    if(a->last_code==STATUS_OK){if(type==CMD_PLAY)preparation_cancel_output(a,d->hardware.id);output_command_power(d,type);}

}
size_t app_indicator_json(const AppOutput *d,unsigned pixel,char *buffer,size_t capacity) {
    if(!d||!d->indicators||pixel>=d->pixels.count)return 0;
    const IndicatorLight *l=&d->indicators[pixel];
    int n=snprintf(buffer,capacity,"{\"state\":\"%s\",\"brightness\":%u,\"color_mode\":\"rgb\",\"color\":{\"r\":%u,\"g\":%u,\"b\":%u},\"effect\":\"%s\"}",l->on?"ON":"OFF",l->brightness,l->rgb[0],l->rgb[1],l->rgb[2],indicator_effects[l->effect]);
    return n>0&&(size_t)n<capacity?(size_t)n:0;
}
static bool indicator_command(App *a,AppOutput *d,uint16_t id,const Command *command) {
    if(command->kind==COMMAND_INDICATOR_SNAPSHOT){
        if(!d->indicators)goto invalid;
        a->send(a->transport,RESP_PIXELS,id,d->pixels.rgb,d->pixels.count*3);reply(a,id,STATUS_OK,0,"indicator RGB frame");return true;
    }
    unsigned pixel=command->pixel;
    if(!d->indicators||pixel>=d->pixels.count)goto invalid;
    if(command->kind==COMMAND_INDICATOR_SET){
        IndicatorLight next=d->indicators[pixel];
        if(command->fields&COMMAND_POWER_VALUE)next.on=command->on;
        if(command->fields&COMMAND_BRIGHTNESS)next.brightness=(uint8_t)command->brightness;
        if(command->fields&COMMAND_COLOR)for(unsigned c=0;c<3;c++)next.rgb[c]=(uint8_t)(command->color>>(16-c*8));
        if(command->fields&COMMAND_EFFECT)next.effect=(IndicatorEffect)command->effect;
        if(next.effect!=d->indicators[pixel].effect||(next.on&&!d->indicators[pixel].on))next.effect_started=platform_time_us();
        d->indicators[pixel]=next;d->indicator_dirty=true;
    }
    char buffer[160];size_t size=app_indicator_json(d,pixel,buffer,sizeof(buffer));
    Writer w={.app=a,.id=id};bytes(&w,buffer,size);flush(&w);reply(a,id,STATUS_OK,0,"indicator state");return true;
invalid:
    error_reply(a,id,STATUS_CONFIG,"invalid_indicator","expected indicator output, pixel and valid RGB light value");return true;
}
static bool key_feedback_command(App *a,uint16_t id,const Command *command) {
    const char *input=command->output;unsigned key=command->pixel;bool enabled=command->on;
    if(key<1||key>DIGITAL_INPUT_CHANNELS)goto invalid;
    DigitalInput *d=NULL;
    for(unsigned n=0;n<a->digital_inputs.count;n++)if(!strcmp(a->digital_inputs.devices[n].config->id,input))d=&a->digital_inputs.devices[n];
    if(!d||!d->config->indicators[0])goto invalid;
    uint16_t bit=(uint16_t)(1u<<(key-1));
    if(command->kind==COMMAND_KEY_SET){
        uint16_t next=enabled?(uint16_t)(d->press_feedback|bit):(uint16_t)(d->press_feedback&~bit);
        if(next!=d->press_feedback){
            uint8_t mask[2]={(uint8_t)next,(uint8_t)(next>>8)};
            bool ok=store_put(&a->store,STORE_KEY_FEEDBACK,input,mask,sizeof(mask));resume_clock(a);
            if(!ok){error_reply(a,id,STATUS_STORAGE,"feedback_not_saved","could not persist key press feedback");return true;}
            d->press_feedback=next;
        }
    }
    Writer w={.app=a,.id=id};literal(&w,"{\"enabled\":");literal(&w,d->press_feedback&bit?"true":"false");literal(&w,"}");flush(&w);
    reply(a,id,STATUS_OK,0,"key press feedback setting");return true;
invalid:
    error_reply(a,id,STATUS_CONFIG,"invalid_key_feedback","expected bound input, key (1..16) and boolean enabled");return true;
}
static AppOutput *request_output(App *a,const Json *j){
    char output[OBJECT_NAME_LIMIT+1]="";
    int slot=json_get(j,0,"slot");if(slot>=0&&!json_equal(j,slot,"A")&&!json_equal(j,slot,"B"))return NULL;
    int token=json_get(j,0,"output");
    if(token>=0&&(!json_string(j,token,output,sizeof(output))||!object_name_valid(output)))return NULL;
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
        static const char *const fields[]={"op","name",NULL};
        char filter[sizeof(((InputSignal *)0)->name)]="";
        int name=json_get(j,0,"name");
        if(!json_fields(j,0,fields)||(name>=0&&(!json_string(j,name,filter,sizeof(filter))||!filter[0])))error="invalid input list fields";
        else{
            Writer w={.app=a,.id=id};literal(&w,"[");
            static const char *const types[]={"","int","float","rgb8","bool"};
            unsigned emitted=0;
            for(unsigned n=0;n<a->inputs.native_count+a->inputs.count;n++){
                bool native=n<a->inputs.native_count;
                InputSignal *source=native?&a->inputs.native[n]:&a->inputs.signals[n-a->inputs.native_count];
                if(name>=0&&strcmp(filter,source->name))continue;
                char value[48];if(emitted++)literal(&w,",");
                literal(&w,"{\"name\":");quoted(&w,source->name);literal(&w,",\"type\":");quoted(&w,types[source->type]);
                literal(&w,",\"origin\":");quoted(&w,native?"board":"publisher");
                if(native){literal(&w,",\"label\":");quoted(&w,source->label);literal(&w,",\"unit\":");quoted(&w,source->unit);
                    char range[100];snprintf(range,sizeof(range),",\"suggested_min\":%.9g,\"suggested_max\":%.9g",(double)source->suggested_min,(double)source->suggested_max);literal(&w,range);
                }
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
static bool audio_manage(App *a,uint16_t id,const Json *j,const char *op){
    if(strcmp(op,"audio_claim")&&strcmp(op,"audio_keep")&&strcmp(op,"audio_clock")&&strcmp(op,"audio_release"))return false;
    const char *error=NULL;AudioChallenge result={0};uint32_t generation=0;uint64_t now=platform_time_us();
    if(!strcmp(op,"audio_claim"))error=audio_gateway_claim(&a->audio,j,a->source,now,&result);
    else{
        static const char *const base[]={"op","boot_id","generation",NULL};
        static const char *const clock[]={"op","boot_id","generation","challenge","publisher_us",NULL};
        bool echo=!strcmp(op,"audio_clock");uint64_t challenge=0,remote=0;
        if(!json_fields(j,0,echo?clock:base)||!json_equal(j,json_get(j,0,"boot_id"),a->boot_id)||
           !json_u32(j,json_get(j,0,"generation"),&generation)||
           (echo&&(!audio_source_id(j,json_get(j,0,"challenge"),&challenge)||!audio_source_id(j,json_get(j,0,"publisher_us"),&remote))))
            error="invalid audio ownership or clock fields";
        else if(echo)error=audio_gateway_clock(&a->audio,a->source,generation,challenge,remote,now);
        else if(!strcmp(op,"audio_keep"))error=audio_gateway_keep(&a->audio,a->source,generation,now,&result);
        else error=audio_gateway_release(&a->audio,a->source,generation);
    }
    if(error)error_reply(a,id,STATUS_CONFIG,"invalid_audio",error);
    else{
        if(result.generation){
            Writer w={.app=a,.id=id};char fields[128];literal(&w,"{\"boot_id\":");quoted(&w,a->boot_id);
            snprintf(fields,sizeof(fields),",\"generation\":%u,\"challenge\":\"%016llx\",\"controller_us\":\"%016llx\"}",
                (unsigned)result.generation,(unsigned long long)result.challenge,(unsigned long long)result.sent);
            literal(&w,fields);flush(&w);
        }
        reply(a,id,STATUS_OK,0,"audio operation completed");
    }
    return true;
}
static void manage_dispatch(App *a,uint16_t id,const uint8_t *p,size_t size){
    Json *j=platform_calloc(1,sizeof(*j));char op[40],layer_id[OBJECT_NAME_LIMIT+1]="";
    if(!j||!json_parse(j,p,size)||!json_string(j,json_get(j,0,"op"),op,sizeof(op))){platform_free(j);error_reply(a,id,STATUS_PROTOCOL,"invalid_request","invalid management request");return;}
    if(!strcmp(op,"device_metadata_get")||!strcmp(op,"device_metadata_set")){
        char error[160]="",document[256];enum Status status=STATUS_OK;
        if(!strcmp(op,"device_metadata_get")){
            static const char *const fields[]={"op",NULL};if(!json_fields(j,0,fields))status=STATUS_PROTOCOL;
        }else if(a->upload_owner||preparation_pending(a)) {status=STATUS_BUSY;snprintf(error,sizeof(error),"finish pending writes before renaming");}
        else {status=device_metadata_validate(&a->metadata,j,a->boot_id,error,sizeof(error));if(status==STATUS_OK){status=device_metadata_set(&a->metadata,j,a->boot_id,error,sizeof(error));resume_clock(a);}}
        platform_free(j);
        if(status!=STATUS_OK)error_reply(a,id,status,"device_metadata",error[0]?error:"invalid metadata request");
        else {Writer w={.app=a,.id=id};size_t n=device_metadata_json(&a->metadata,document,sizeof(document));bytes(&w,document,n);flush(&w);reply(a,id,STATUS_OK,0,"device metadata");}
        return;
    }
    if(!strcmp(op,"audio_status")){
        static const char *const fields[]={"op",NULL};
        if(!json_fields(j,0,fields)){platform_free(j);error_reply(a,id,STATUS_PROTOCOL,"invalid_request","unexpected audio status fields");return;}
        platform_free(j);Writer w={.app=a,.id=id};
        literal(&w,"{\"providers\":");audio_sources_status(&a->audio,binding_text,&w);
        literal(&w,"}");flush(&w);reply(a,id,STATUS_OK,0,"structured audio source status");return;
    }

    if(audio_manage(a,id,j,op)){platform_free(j);return;}
    if(preparation_manage(a,id,j,op)){platform_free(j);return;}
    AppOutput *d=request_output(a,j);
    if(!d){platform_free(j);error_reply(a,id,STATUS_PROTOCOL,"invalid_output","invalid request, unknown output or invalid A/B slot");return;}
    Command command;bool recognized;
    const char *decode_error=command_decode(j,op,&command,&recognized);
    if(recognized){
        platform_free(j);
        if(decode_error)error_reply(a,id,STATUS_CONFIG,
            command.kind>=COMMAND_KEY_GET?"invalid_key_feedback":
            command.kind>=COMMAND_INDICATOR_GET?"invalid_indicator":
            command.kind==COMMAND_RECALL||command.kind==COMMAND_POWER?"invalid_recall":"invalid_light",decode_error);
        else execute_command(a,id,&command);
        return;
    }
    if(d->hardware.indicators&&(!strncmp(op,"transition_",11)||!strncmp(op,"composition_",12)||
       !strcmp(op,"preset_activate")||!strcmp(op,"preset_recall")||!strcmp(op,"output_power")||
       !strcmp(op,"program_activate")||!strcmp(op,"program_reset")||!strcmp(op,"program_pause")||!strcmp(op,"program_resume")||!strcmp(op,"program_inputs")||!strcmp(op,"output_command"))){
        platform_free(j);error_reply(a,id,STATUS_CONFIG,"indicator_owned","indicator pixels are controlled through indicator_set");return;
    }
    if(input_manage(a,id,j,op)){platform_free(j);return;}
    if(transition_manage(a,d,id,j,op)||preset_manage(a,d,id,j,op)){platform_free(j);return;}
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
        AudioSourcePlan plan={0};
        bool lifecycle=command==CMD_PLAY||command==CMD_STOP||command==CMD_RESET;
        const char *admission=lifecycle?reserve_output_sources(d,(SourceDemandChange){.manual=true,.resume=command==CMD_PLAY?s:NULL,.exclude=command==CMD_PLAY?NULL:s},&plan):NULL;
        if(admission){error_reply(a,id,STATUS_CONFIG,"sources_unavailable",admission);return;}
        audio_source_plan_commit(&plan);
        if(command==CMD_PLAY||command==CMD_STOP||command==CMD_RESET)preparation_cancel_output(a,d->hardware.id);
        const char *error=NULL;
        if(command==CMD_PLAY||command==CMD_STOP){composition_play(s,command==CMD_PLAY);if(command==CMD_STOP){composition_invalidate(s);memset(s->front,0,(size_t)s->count*3);for(unsigned n=0;n<s->layer_count;n++)s->layers[n].front_stamp=(ContributionStamp){0};s->frames++;}}
        else if(command==CMD_FRAME){error=composition_frame(s,delta);if(!error)for(size_t offset=0;offset<(size_t)s->count*3;offset+=FRAME_LIMIT){size_t n=(size_t)s->count*3-offset;if(n>FRAME_LIMIT)n=FRAME_LIMIT;a->send(a->transport,RESP_PIXELS,id,s->front+offset,n);}}
        else if(command==CMD_RESET){d->transition->compositions[1]=NULL;composition_destroy(s);++d->composition_revision;}
        else if(command!=CMD_STATUS)error="unsupported composition slot command";
        if(!error&&command!=CMD_STATUS&&command!=CMD_FRAME)d->recall_owned=false;
        finish_output_sources(d,&plan);
        transition_invalidate(d->transition);reply(a,id,error?STATUS_EXECUTION:STATUS_OK,0,error?error:"composition slot command complete");return;
    }
    if(!strcmp(op,"program_reset")){
        static const char *const fields[]={"op","output","slot","layer","expected_generation","settings","inputs",NULL};uint32_t generation;
        if(!json_fields(j,0,fields)||!json_u32(j,json_get(j,0,"expected_generation"),&generation)||generation!=vm->generation||!vm->portable){
            platform_free(j);error_reply(a,id,STATUS_CONFIG,"stale_generation","expected program generation must match");return;
        }
        PreparationRequest pending={.kind=PREPARE_RESET,.entry=vm->code,.slot=slot,.play=vm->playing,.generation=generation,.reset_from=vm};
        if(s){CompositionLayer *l=layer_token>=0?composition_layer(s,layer_id):&s->layers[0];strcpy(pending.layer,l->id);strcpy(pending.name,l->program);}
        else strcpy(pending.name,d->active_program);
        (void)submit_scene(a,d,id,pending,j->text,j->size);platform_free(j);return;
    }
    bool read_only=!strcmp(op,"program_state")||!strcmp(op,"program_snapshot"),input_update=!strcmp(op,"program_inputs");
    if(read_only||!strcmp(op,"program_reset")||!strcmp(op,"program_pause")||!strcmp(op,"program_resume")||!strcmp(op,"program_inputs")){
        AudioSourcePlan plan={0};
        bool resume=!strcmp(op,"program_resume"),pause=!strcmp(op,"program_pause");
        if(resume||pause||input_update){
            static const char *const fields[]={"op","slot","output","layer","expected_generation",NULL};uint32_t expected;
            static const char *const input_fields[]={"op","slot","output","layer","target","expected_generation","expected_inputs_revision","inputs",NULL};
            uint32_t revision;
            bool valid_inputs=!input_update||(vm->portable&&json_get(j,0,"inputs")>=0&&json_u32(j,json_get(j,0,"expected_inputs_revision"),&revision)&&revision==vm->portable->input_revision);
            /* Field and generation errors retain the control API's diagnostics. */
            if(vm->ready&&vm->control.enabled&&(!resume||(vm->control.initialized&&!vm->control.faulted))&&json_fields(j,0,input_update?input_fields:fields)&&
               valid_inputs&&json_u32(j,json_get(j,0,"expected_generation"),&expected)&&expected==vm->generation){
                const char *error=reserve_output_sources(d,(SourceDemandChange){.manual=input_update||(resume?!vm->playing:vm->playing),.power=resume&&!s&&!transition_target,.resume_vm=resume?vm:NULL,.exclude_vm=pause?vm:NULL},&plan);
                if(error){platform_free(j);error_reply(a,id,STATUS_CONFIG,"sources_unavailable",error);return;}
                if(resume)audio_source_plan_commit(&plan);
            }
        }
        if(!read_only&&!input_update){transition_invalidate(d->transition);composition_invalidate(s);}
        uint32_t generation=vm->generation,input_revision=vm->portable?vm->portable->input_revision:0;bool was_playing=vm->playing;
        (void)control_manage(vm,id,j,op);
        platform_free(j);
        if(!read_only&&a->last_code==STATUS_OK)preparation_cancel_output(a,d->hardware.id);
        if(a->last_code==STATUS_OK&&!s&&!transition_target&&(!strcmp(op,"program_resume")||!strcmp(op,"program_reset")))d->power_on=true;
        if(generation!=vm->generation||was_playing!=vm->playing||(vm->portable&&input_revision!=vm->portable->input_revision)){audio_source_plan_commit(&plan);if(!transition_target)composition_modified(s);d->recall_owned=false;}
        finish_output_sources(d,&plan);
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
        AudioSourcePlan plan={0};
        bool lifecycle=command==CMD_PLAY||command==CMD_STOP||command==CMD_RESET;
        const char *admission=lifecycle?reserve_output_sources(d,(SourceDemandChange){.manual=command==CMD_RESET||(command==CMD_PLAY?!vm->playing:vm->playing),.resume_vm=command==CMD_PLAY?vm:NULL,.exclude_vm=command==CMD_PLAY?NULL:vm},&plan):NULL;
        if(admission){error_reply(a,id,STATUS_CONFIG,"sources_unavailable",admission);return;}
        audio_source_plan_commit(&plan);
        if(command==CMD_PLAY||command==CMD_STOP||command==CMD_RESET)preparation_cancel_output(a,d->hardware.id);
        if(command!=CMD_STATUS){transition_invalidate(d->transition);composition_invalidate(s);}
        uint32_t generation=vm->generation;bool was_playing=vm->playing;
        uint8_t bytes[4];write_u32(bytes,delta);vm_command(vm,(uint8_t)command,id,command==CMD_FRAME?bytes:NULL,command==CMD_FRAME?4:0);
        if(generation!=vm->generation||was_playing!=vm->playing){composition_modified(s);d->recall_owned=false;}
        finish_output_sources(d,&plan);
        if(command==CMD_STOP&&vm->control.contribution_valid){
            memset(vm->control.committed,0,control_frame_bytes(vm));vm->control.contribution_frame++;
            /* Keep the cleared frame readable, but it is no longer a guest
               contribution eligible for unchanged-input reuse. */
            vm->control.contribution=(ContributionStamp){0};
        }
        if(vm->generation>d->layer_generation)d->layer_generation=vm->generation;
        if(command!=CMD_STATUS){transition_invalidate(d->transition);composition_invalidate(s);}
        return;
    }
    Arguments args;bool valid=arguments(j,&args);
    if(valid&&!strcmp(args.op,"program_activate")){
        if(a->upload_output->vm.uploading){platform_free(j);error_reply(a,id,STATUS_BUSY,"upload_busy","finish or cancel the transient upload first");return;}
        (void)activate(a,args.name,id,args.action,args.output,j,json_get(j,0,"settings"));
        platform_free(j);return;
    }
    platform_free(j);
    if(valid)manage(a,id,args);
    else error_reply(a,id,STATUS_PROTOCOL,"invalid_request","invalid management JSON or fields");
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
void app_execute_command(App *a,uint64_t source,SendMessage send,void *transport,uint16_t id,const Command *command){
    if(a->command_active){send_status(send,transport,id,STATUS_BUSY,0,"engine command in progress");return;}
    SendMessage previous_send=a->send;void *previous_transport=a->transport;uint64_t previous_source=a->source;
    a->command_active=true;a->send=send;a->transport=transport;a->source=source;
    execute_command(a,id,command);
    a->command_active=false;
    if(a->disconnect_pending){a->disconnect_pending=false;app_disconnect(a,a->upload_owner);}
    a->send=previous_send;a->transport=previous_transport;a->source=previous_source;
}
void app_disconnect(App *a,uint64_t source) {
    audio_gateway_disconnect(&a->audio,source);
    if (!source || a->upload_owner!=source) return;
    if(a->command_active){a->disconnect_pending=true;return;}
    a->upload_owner=0; a->uploading=false; a->staged=false;
    a->upload_output->vm.uploading=false;store_upload_cancel(&a->upload);
    memset(a->staging,0,sizeof(a->staging));
}
void app_reply_delivered(App *a,uint64_t source) {
    network_settings_reply_delivered(&a->network_settings,source,platform_time_us());
}
