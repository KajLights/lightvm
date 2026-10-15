#include "app.h"
#include "json.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

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
static void empty_config(App *a) {
    memset(&a->hardware, 0, sizeof(a->hardware));
    a->config_size = (uint32_t)strlen(EMPTY_CONFIG);
    memcpy(a->config_json, EMPTY_CONFIG, a->config_size);
    a->vm.target = NULL; a->vm.output = NULL;
}
static bool config_validate(App *a, const void *data, uint32_t size, HardwareConfig *config, char *error, size_t capacity) {
    if (!hardware_parse(data, size, config, error, capacity)) return false;
    if (config->startup[0] && !store_find(&a->store, config->startup)) {
        snprintf(error, capacity, "startup.procedure: stored procedure does not exist"); return false;
    }
    return true;
}
static bool config_apply(App *a, const HardwareConfig *config, const void *data, uint32_t size) {
    vm_destroy(&a->vm); a->vm.uploading = false;
    a->active_procedure[0] = 0; a->active_crc = 0;
    pixels_destroy(&a->pixels);
    ++a->revision;
    if (config->has_pixels && !pixels_init(&a->pixels, &config->pixels, device_type("ws2812b"))) {
        empty_config(a); return false;
    }
    a->hardware = *config;
    memmove(a->config_json, data, size); a->config_size = size;
    a->vm.target = config->target_a ? &a->pixels : NULL;
    a->vm.output = a->vm.target;
    return true;
}
// Snapshot storage alternates banks. Preserve the running version before an
// erase; rebind unchanged programs to the new snapshot after a successful write.
static bool update_store(App *a,const char *name,const void *data,uint32_t size,bool remove) {
    if(!vm_materialize(&a->vm))return false;
    bool ok=remove ? store_delete(&a->store,name) : store_put(&a->store,name,data,size);
    const StoreEntry *entry=store_find(&a->store,a->active_procedure);
    if(entry && a->vm.ready && entry->size==a->vm.expected && entry->crc==a->active_crc) {
        a->vm.storage_offset=(uint32_t)a->store.bank*STORE_BANK_BYTES+entry->offset;
        a->vm.from_storage=true;a->vm.program.cache_size=0;
    }
    return ok;
}
static bool activate(App *a, const char *name, uint16_t id, const char *action, const char *export_name, const char *slot) {
    if (strcmp(slot,"A")) { error_reply(a,id,STATUS_CONFIG,"unsupported_slot","this runtime supports deployment slot A only"); return false; }
    const StoreEntry *entry = store_find(&a->store, name);
    if (!entry || !strcmp(name, STORE_CONFIG)) { error_reply(a,id,STATUS_STORAGE,"not_found","stored procedure does not exist"); return false; }
    if (strcmp(action,"play") && strcmp(action,"run")) { error_reply(a,id,STATUS_PROTOCOL,"invalid_action","action must be play or run"); return false; }
    if (!strcmp(action,"play") && !a->vm.target) { error_reply(a,id,STATUS_CONFIG,"missing_target","target A has no configured output"); return false; }
    vm_destroy(&a->vm); a->vm.uploading=false; a->active_procedure[0]=0; a->active_crc=0;
    a->vm.expected=entry->size; a->vm.received=entry->size;
    a->vm.from_storage=true;
    a->vm.storage_offset=(uint32_t)a->store.bank*STORE_BANK_BYTES+entry->offset;
    uint8_t chunk[256];uint32_t crc=0xffffffffu;
    for(uint32_t at=0;at<entry->size;) {
        size_t n=entry->size-at;if(n>sizeof(chunk))n=sizeof(chunk);
        if(!vm_read(&a->vm,at,chunk,n)) { vm_destroy(&a->vm);error_reply(a,id,STATUS_STORAGE,"read_failed","procedure read failed");return false; }
        crc=crc32_update(crc,chunk,n);at+=(uint32_t)n;
    }
    if((crc^0xffffffffu)!=entry->crc) { vm_destroy(&a->vm);error_reply(a,id,STATUS_STORAGE,"read_failed","procedure checksum failed");return false; }
    const char * error=vm_load(&a->vm);
    if (error) { vm_destroy(&a->vm); error_reply(a,id,STATUS_LOAD,"load_failed",error); return false; }
    a->vm.ready=true; memcpy(a->vm.slot,slot,strlen(slot)+1);
    memcpy(a->active_procedure,name,strlen(name)+1); a->active_crc=entry->crc;
    vm_command(&a->vm,!strcmp(action,"play") ? CMD_PLAY : CMD_RUN,id,(const uint8_t *)export_name,
               !strcmp(action,"run") ? strlen(export_name) : 0);
    if (a->last_code) { vm_destroy(&a->vm); a->active_procedure[0]=0; a->active_crc=0; return false; }
    return true;
}
void app_init(App *a, SendMessage send, void *transport) {
    memset(a,0,sizeof(*a)); a->connection_sequence=1; a->send=send; a->transport=transport;
#if LIGHTVM_BOOT_SEQUENCE
    (void)boot_sequence_init(&a->boot_sequence);
#endif
    network_init(&a->network);
    a->network.recovery=LIGHTVM_POWER_RECOVERY && app_boot_sequence_triggered(a);
    platform_identity(a->device_id,sizeof(a->device_id),a->boot_id,sizeof(a->boot_id));
    vm_init(&a->vm,NULL,vm_send,a); empty_config(a);
    a->booting=true;
    if (!store_mount(&a->store)) snprintf(a->boot_error,sizeof(a->boot_error),"storage: no valid snapshot or read failure");
    const StoreEntry *config=store_find(&a->store,STORE_CONFIG);
    if (config) {
        HardwareConfig hardware;
        char error[160];
        if (!store_read(&a->store,config,0,a->config_json,config->size)) snprintf(a->boot_error,sizeof(a->boot_error),"configuration read failed");
        else if (!config_validate(a,a->config_json,config->size,&hardware,error,sizeof(error))) snprintf(a->boot_error,sizeof(a->boot_error),"%s",error);
        else if (!config_apply(a,&hardware,a->config_json,config->size)) snprintf(a->boot_error,sizeof(a->boot_error),"native device initialization failed");
        else if (hardware.startup[0] && !a->network.recovery) (void)activate(a,hardware.startup,0,"play","","A");
        if (a->boot_error[0]) { vm_destroy(&a->vm); pixels_destroy(&a->pixels); empty_config(a); }
    }
    const StoreEntry *saved_network=store_find(&a->store,STORE_NETWORK);
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
void app_destroy(App *a) { vm_destroy(&a->vm); pixels_destroy(&a->pixels); }
bool app_boot_sequence_triggered(const App *a) {
#if LIGHTVM_BOOT_SEQUENCE
    return a->boot_sequence.triggered;
#else
    (void)a; return false;
#endif
}
void app_tick(App *a) {
    uint64_t now=platform_time_us();
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
    vm_tick(&a->vm);
}
static void resume_clock(App *a) {
    // Persistence can stall physical output; retain guest state and discard the gap.
    a->vm.last_frame=platform_time_us(); a->vm.next_frame=a->vm.last_frame+FRAME_INTERVAL_US;
}
static void describe(App *a, uint16_t id) {
    Writer w={.app=a,.id=id};
    const PlatformInfo *info=platform_info();
    literal(&w,"{\"connection_api\":1,\"management_api\":1,\"schema\":\"lightvm.config/1\",\"platform\":"); quoted(&w,info->platform);
    literal(&w,",\"device_id\":"); quoted(&w,a->device_id);
    literal(&w,",\"boot_id\":"); quoted(&w,a->boot_id);
    literal(&w,",\"wifi_supported\":"); literal(&w,LIGHTVM_WIFI?"true":"false");
    literal(&w,",\"wifi_ap_supported\":"); literal(&w,LIGHTVM_WIFI?"true":"false");
    literal(&w,",\"recovery_supported\":"); literal(&w,LIGHTVM_POWER_RECOVERY?"true":"false");
    literal(&w,",\"http_supported\":"); literal(&w,LIGHTVM_HTTP?"true":"false");
    literal(&w,",\"boot_sequence_supported\":"); literal(&w,LIGHTVM_BOOT_SEQUENCE?"true":"false");
#if LIGHTVM_BOOT_SEQUENCE
    literal(&w,",\"boot_sequence\":{\"count\":"); number(&w,a->boot_sequence.count);
    literal(&w,",\"triggered\":"); literal(&w,app_boot_sequence_triggered(a)?"true":"false");
    literal(&w,",\"error\":"); quoted(&w,a->boot_sequence.error); literal(&w,"}");
#endif
    literal(&w,",\"network_revision\":"); number(&w,a->store.network_revision);
    literal(&w,",\"bytecode_format\":\"LBC1/2\"");
    literal(&w,",\"vm_heap_limit\":"); number(&w,VM_HEAP_LIMIT);
    literal(&w,",\"board\":"); quoted(&w,info->board);
    literal(&w,",\"device_types\":[{\"type\":\"ws2812b\",\"max_instances\":1,\"required\":[\"pin\",\"count\"],\"order_default\":\"GRB\",\"brightness_default\":255}],\"pins\":[");
    size_t count; const BoardPin *pins=platform_pins(&count);
    for (size_t i=0;i<count;++i) { if (i) literal(&w,","); quoted(&w,pins[i].name); }
    literal(&w,"],\"pixel_capacity\":"); number(&w,PIXEL_CAPACITY);
    literal(&w,",\"config_limit\":"); number(&w,CONFIG_LIMIT);
    literal(&w,",\"module_limit\":"); number(&w,MODULE_LIMIT);
    literal(&w,",\"control_abi\":"); number(&w,CONTROL_ABI_VERSION);
    literal(&w,",\"program_schemas\":[\"lightvm.program/3\"],\"slots\":[\"A\"],\"slot_binding\":\"deployment\",\"legacy_animation_api\":1,\"pixel_format\":\"rgb8\",\"frame_policy\":\"black_before_step\",\"alpha\":false,\"parameter_limit\":8,\"composition\":false,\"program_generation\":"); number(&w,a->vm.generation);
    literal(&w,",\"procedure_limit\":"); number(&w,PROCEDURE_LIMIT);
    literal(&w,",\"storage_available\":"); literal(&w,a->store.available ? "true" : "false");
    literal(&w,",\"storage_bytes\":"); number(&w,a->store.available ? STORAGE_BYTES : 0);
    literal(&w,",\"free_bytes\":"); number(&w,a->store.available ? store_free_bytes(&a->store) : 0);
    literal(&w,",\"catalog_revision\":"); number(&w,a->store.generation);
    literal(&w,",\"active_revision\":"); number(&w,a->revision);
    literal(&w,",\"saved_revision\":"); number(&w,a->store.config_revision);
    literal(&w,",\"active_procedure\":"); quoted(&w,a->active_procedure);
    literal(&w,",\"active_crc32\":"); number(&w,a->active_crc);
    literal(&w,",\"boot_error\":"); quoted(&w,a->boot_error);
    literal(&w,",\"animation_error\":"); quoted(&w,a->vm.animation_error);
    literal(&w,",\"target\":\"A\",\"uploading\":"); literal(&w,a->uploading ? "true" : "false");
    literal(&w,"}"); flush(&w); reply(a,id,STATUS_OK,0,"capabilities");
}
typedef struct { char op[33],name[33],action[16],export_name[64],slot[33]; uint32_t revision,saved_revision; bool has_revision,has_saved_revision; } Arguments;
static bool arguments(const uint8_t *p,size_t size,Arguments *args) {
    Json j;
    memset(args,0,sizeof(*args)); memcpy(args->action,"play",5); memcpy(args->export_name,"run",4); memcpy(args->slot,"A",2);
    static const char *const fields[]={"op","name","action","export","slot","expected_revision","expected_saved_revision",NULL};
    if (!json_parse(&j,p,size) || !json_fields(&j,0,fields) || !json_string(&j,json_get(&j,0,"op"),args->op,sizeof(args->op))) return false;
    int name=json_get(&j,0,"name"),action=json_get(&j,0,"action"),ex=json_get(&j,0,"export");
    if (name>=0 && (!json_string(&j,name,args->name,sizeof(args->name)) || !object_name_valid(args->name))) return false;
    if (action>=0 && !json_string(&j,action,args->action,sizeof(args->action))) return false;
    if (ex>=0 && !json_string(&j,ex,args->export_name,sizeof(args->export_name))) return false;
    int slot=json_get(&j,0,"slot");
    if (slot>=0 && !json_string(&j,slot,args->slot,sizeof(args->slot))) return false;
    int revision=json_get(&j,0,"expected_revision"),saved=json_get(&j,0,"expected_saved_revision");
    args->has_revision=revision>=0; args->has_saved_revision=saved>=0;
    return (!args->has_revision || json_u32(&j,revision,&args->revision)) && (!args->has_saved_revision || json_u32(&j,saved,&args->saved_revision));
}
static bool referenced(App *a,const char *name) {
    if (!strcmp(a->hardware.startup,name) || !strcmp(a->active_procedure,name)) return true;
    const StoreEntry *entry=store_find(&a->store,STORE_CONFIG);
    if (entry) {
        // Reuse staging only when there is no pending transaction/candidate.
        uint8_t *data=platform_calloc(1,entry->size);
        if (!data) return true;
        HardwareConfig hardware; char error[160];
        bool valid=store_read(&a->store,entry,0,data,entry->size) && hardware_parse(data,entry->size,&hardware,error,sizeof(error));
        platform_free(data);
        if (!valid || !strcmp(hardware.startup,name)) return true;
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
static void manage(App *a,uint16_t id,const uint8_t *p,size_t size) {
    if (control_manage(&a->vm,id,p,size)) return;
    Arguments args;
    if (!arguments(p,size,&args)) { error_reply(a,id,STATUS_PROTOCOL,"invalid_request","invalid management JSON or fields"); return; }
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
        literal(&w,"}"); flush(&w); reply(a,id,STATUS_OK,0,"network status"); return;
    }
    if (!strcmp(args.op,"network_validate") || !strcmp(args.op,"network_commit")) {
        if (!a->staged || a->object_kind!=3 || a->upload_owner!=a->source) { error_reply(a,id,STATUS_UPLOAD,"no_candidate","stage a network configuration in this session first"); return; }
        NetworkConfig config; char error[160];
        if (!network_parse(a->staging,a->expected,&a->network.config,&config,error,sizeof(error)) ||
            (config.wifi_enabled && !LIGHTVM_WIFI) || (config.http_enabled && !LIGHTVM_HTTP) ||
            (config.http_enabled && !config.wifi_enabled && strcmp(platform_info()->platform,"host"))) {
            error_reply(a,id,STATUS_CONFIG,"invalid_network",error[0]?error:"selected network feature is unavailable or HTTP requires Wi-Fi"); return;
        }
        if (!strcmp(args.op,"network_validate")) { reply(a,id,STATUS_OK,0,"network configuration valid"); return; }
        if (!args.has_revision || args.revision!=a->store.network_revision || a->network_pending) { error_reply(a,id,STATUS_CONFIG,"stale_revision","network revision must match; wait for pending changes"); return; }
        char data[4096]; size_t count=network_json(&config,true,data,sizeof(data));
        bool ok=count && update_store(a,STORE_NETWORK,data,(uint32_t)count,false); resume_clock(a);
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
        const StoreEntry *entry=store_find(&a->store,STORE_CONFIG);
        if (entry) read_object(a,id,entry);
        else { Writer w={.app=a,.id=id}; literal(&w,EMPTY_CONFIG); flush(&w); reply(a,id,STATUS_OK,0,"no saved configuration"); }
        return;
    }
    if (!strcmp(args.op,"procedure_list")) {
        Writer w={.app=a,.id=id}; literal(&w,"["); unsigned found=0;
        for (uint32_t i=0;i<a->store.count;++i) {
            const StoreEntry *entry=&a->store.entries[i]; if (!strcmp(entry->name,STORE_CONFIG) || !strcmp(entry->name,STORE_NETWORK)) continue;
            if (found++) literal(&w,",");
            literal(&w,"{\"name\":"); quoted(&w,entry->name); literal(&w,",\"size\":"); number(&w,entry->size);
            literal(&w,",\"crc32\":"); number(&w,entry->crc); literal(&w,"}");
        }
        literal(&w,"]"); flush(&w); reply(a,id,STATUS_OK,(int32_t)found,"stored procedures"); return;
    }
    if (!strcmp(args.op,"procedure_get")) {
        const StoreEntry *entry=store_find(&a->store,args.name);
        if (!entry) error_reply(a,id,STATUS_STORAGE,"not_found","stored procedure does not exist"); else read_object(a,id,entry);
        return;
    }
    if (!strcmp(args.op,"procedure_activate") && a->vm.uploading) { error_reply(a,id,STATUS_BUSY,"upload_busy","finish or cancel the transient upload first"); return; }
    if (!strcmp(args.op,"procedure_activate")) { (void)activate(a,args.name,id,args.action,args.export_name,args.slot); return; }
    if (!strcmp(args.op,"config_validate") || !strcmp(args.op,"config_apply")) {
        if (!a->staged || a->object_kind!=1 || a->upload_owner!=a->source) { error_reply(a,id,STATUS_CONFIG,"no_candidate","upload and finish a configuration first"); return; }
        HardwareConfig config; char error[160];
        if (!config_validate(a,a->staging,a->expected,&config,error,sizeof(error))) { error_reply(a,id,STATUS_CONFIG,"invalid_config",error); return; }
        if (!strcmp(args.op,"config_validate")) { reply(a,id,STATUS_OK,0,"configuration valid"); return; }
        if (!args.has_revision || args.revision!=a->revision || a->revision==UINT32_MAX) { error_reply(a,id,STATUS_CONFIG,"stale_revision","expected active revision is required and must match"); return; }
        if (!config_apply(a,&config,a->staging,a->expected)) { error_reply(a,id,STATUS_CONFIG,"initialization_failed","native device initialization failed; active configuration is empty"); return; }
        a->boot_error[0]=0; a->staged=false; a->upload_owner=0;
        reply(a,id,STATUS_OK,(int32_t)a->revision,"configuration applied in RAM; playback stopped"); return;
    }
    if (!strcmp(args.op,"config_save")) {
        if (!args.has_revision || args.revision!=a->revision || !args.has_saved_revision || args.saved_revision!=a->store.config_revision) {
            error_reply(a,id,STATUS_CONFIG,"stale_revision","expected active and saved revisions are required and must match"); return;
        }
        HardwareConfig config; char error[160];
        if (!config_validate(a,a->config_json,a->config_size,&config,error,sizeof(error))) { error_reply(a,id,STATUS_CONFIG,"invalid_config",error); return; }
        bool ok=update_store(a,STORE_CONFIG,a->config_json,a->config_size,false); resume_clock(a);
        if (!ok) error_reply(a,id,STATUS_STORAGE,"save_failed","storage unavailable, full or write failed");
        else reply(a,id,STATUS_OK,(int32_t)a->store.config_revision,"configuration saved");
        return;
    }
    if (!strcmp(args.op,"procedure_save") || !strcmp(args.op,"procedure_delete")) {
        if (!args.has_revision || args.revision!=a->store.generation) { error_reply(a,id,STATUS_STORAGE,"stale_revision","expected catalog revision is required and must match"); return; }
        bool ok;
        if (!strcmp(args.op,"procedure_save")) {
            if (!a->staged || a->object_kind!=2 || a->upload_owner!=a->source || strcmp(args.name,a->object_name)) { error_reply(a,id,STATUS_UPLOAD,"no_candidate","upload and finish this named procedure first"); return; }
            ok=update_store(a,args.name,a->staging,a->expected,false);
            if (ok) { a->staged=false; a->upload_owner=0; }
        } else {
            if (!store_find(&a->store,args.name)) { error_reply(a,id,STATUS_STORAGE,"not_found","stored procedure does not exist"); return; }
            if (referenced(a,args.name)) { error_reply(a,id,STATUS_STORAGE,"referenced","procedure is referenced by active/saved configuration or runtime"); return; }
            ok=update_store(a,args.name,NULL,0,true);
        }
        resume_clock(a);
        if (!ok) error_reply(a,id,STATUS_STORAGE,"save_failed","storage unavailable, full or write failed");
        else reply(a,id,STATUS_OK,(int32_t)a->store.generation,"procedure catalog updated");
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
    unsigned object_kind=!strcmp(kind,"config") ? 1u : !strcmp(kind,"procedure") ? 2u : !strcmp(kind,"network") ? 3u : 0u;
    if (!object_kind || expected<(object_kind==2 ? 8u : 1u) || expected>(object_kind==2 ? MODULE_LIMIT : CONFIG_LIMIT)) return false;
    int n=json_get(&j,0,"name");
    if (object_kind==2 && (!json_string(&j,n,name,sizeof(name)) || !object_name_valid(name))) return false;
    if (object_kind!=2 && n>=0) return false;
    a->object_kind=object_kind; a->expected=expected; a->expected_crc=crc; a->received=0;
    memcpy(a->object_name,name,sizeof(name)); a->uploading=true; a->staged=false;
    a->upload_deadline=platform_time_us()+30000000u; return true;
}
static void command_impl(void *context,uint8_t type,uint16_t id,const uint8_t *p,size_t size) {
    App *a=context;
    if (a->upload_owner && a->upload_owner!=a->source && (type==CMD_BEGIN || type==CMD_OBJECT_BEGIN || type==CMD_CHUNK || type==CMD_FINISH || type==CMD_OBJECT_CHUNK || type==CMD_OBJECT_FINISH || type==CMD_OBJECT_CANCEL)) {
        error_reply(a,id,STATUS_BUSY,"upload_busy","upload belongs to another session"); return;
    }
    if (type==CMD_MANAGE) { manage(a,id,p,size); return; }
    if (type==CMD_OBJECT_BEGIN) {
        if (a->uploading || a->vm.uploading) { error_reply(a,id,STATUS_BUSY,"upload_busy","another upload is in progress; cancel it first"); return; }
        if (!object_begin(a,p,size)) error_reply(a,id,STATUS_UPLOAD,"invalid_upload","invalid object upload metadata or size");
        else { a->upload_owner=a->source; reply(a,id,STATUS_OK,0,"object upload started"); }
        return;
    }
    if (type==CMD_OBJECT_CHUNK) {
        if (!a->uploading || size<=4) { error_reply(a,id,STATUS_UPLOAD,"no_upload","no upload or empty chunk"); return; }
        uint32_t offset=read_u32(p); size_t n=size-4;
        if (offset<=a->received && n<=a->received-offset && !memcmp(a->staging+offset,p+4,n)) {
            a->upload_deadline=platform_time_us()+30000000u; reply(a,id,STATUS_OK,(int32_t)a->received,"duplicate chunk"); return;
        }
        if (offset!=a->received || n>a->expected-a->received) { error_reply(a,id,STATUS_UPLOAD,"invalid_offset","unexpected chunk offset or size"); return; }
        memcpy(a->staging+offset,p+4,n); a->received+=(uint32_t)n; a->upload_deadline=platform_time_us()+30000000u;
        reply(a,id,STATUS_OK,(int32_t)a->received,"chunk accepted"); return;
    }
    if (type==CMD_OBJECT_FINISH) {
        if (size || !a->uploading || a->received!=a->expected) { error_reply(a,id,STATUS_UPLOAD,"incomplete_upload","object upload incomplete"); return; }
        a->uploading=false;
        if (crc32_bytes(a->staging,a->expected)!=a->expected_crc) { error_reply(a,id,STATUS_UPLOAD,"checksum_mismatch","object checksum mismatch"); return; }
        if (a->object_kind==1) {
            HardwareConfig config; char error[160];
            if (!hardware_parse(a->staging,a->expected,&config,error,sizeof(error))) { error_reply(a,id,STATUS_CONFIG,"invalid_config",error); return; }
        } else if (a->object_kind==3) {
            NetworkConfig config; char error[160];
            if (!network_parse(a->staging,a->expected,&a->network.config,&config,error,sizeof(error))) { a->upload_owner=0; error_reply(a,id,STATUS_CONFIG,"invalid_network",error); return; }
        } else {
            const char * error=vm_validate(a->staging,a->expected);
            if (error) { error_reply(a,id,STATUS_LOAD,"invalid_module",error); return; }
        }
        a->staged=true; a->upload_deadline=platform_time_us()+30000000u; reply(a,id,STATUS_OK,(int32_t)a->expected,"object staged; not saved or activated"); return;
    }
    if (type==CMD_OBJECT_CANCEL) {
        if (size) { error_reply(a,id,STATUS_PROTOCOL,"invalid_request","cancel takes no payload"); return; }
        a->uploading=false; a->staged=false; a->vm.uploading=false; a->upload_owner=0; reply(a,id,STATUS_OK,0,"object upload cancelled"); return;
    }
    if (type==CMD_BEGIN && (a->uploading || a->staged || a->vm.uploading)) { error_reply(a,id,STATUS_BUSY,"upload_busy","an object upload is in progress"); return; }
    if (type==CMD_BEGIN && size==8 && read_u32(p)>=8 && read_u32(p)<=MODULE_LIMIT) { a->active_procedure[0]=0; a->active_crc=0; }
    if (type==CMD_RESET && !size) { a->uploading=false; a->staged=false; a->active_procedure[0]=0; a->active_crc=0; }
    vm_command(&a->vm,type,id,p,size);
    if (type==CMD_BEGIN && a->last_code==STATUS_OK) a->upload_owner=a->source;
    if ((type==CMD_CHUNK || type==CMD_BEGIN) && a->vm.uploading && a->upload_owner==a->source) a->upload_deadline=platform_time_us()+30000000u;
    if (type==CMD_FINISH && !a->vm.uploading) a->upload_owner=0;
    if (type==CMD_RESET && !size) a->upload_owner=0;
}
void app_command(void *context,uint8_t type,uint16_t id,const uint8_t *p,size_t size) {
    App *a=context; uint64_t previous=a->source; if (!a->source) a->source=1;
    command_impl(a,type,id,p,size); if (!a->uploading && !a->staged && !a->vm.uploading) a->upload_owner=0; a->source=previous;
}
void app_dispatch(App *a,uint64_t source,SendMessage send,void *transport,uint8_t type,uint16_t id,const uint8_t *p,size_t size) {
    SendMessage previous_send=a->send; void *previous_transport=a->transport; uint64_t previous_source=a->source;
    a->send=send; a->transport=transport; a->source=source;
    command_impl(a,type,id,p,size);
    if (!a->uploading && !a->staged && !a->vm.uploading) a->upload_owner=0;
    a->send=previous_send; a->transport=previous_transport; a->source=previous_source;
}
void app_disconnect(App *a,uint64_t source) {
    if (!source || a->upload_owner!=source) return;
    a->upload_owner=0; a->uploading=false; a->staged=false;
    if (a->vm.uploading) { a->vm.uploading=false; vm_destroy(&a->vm); }
    memset(a->staging,0,sizeof(a->staging));
}
void app_reply_delivered(App *a,uint64_t source) {
    if (a->network_pending && a->network_reply_source==source) { a->network_due=platform_time_us()+100000u; a->network_reply_source=0; }
}
