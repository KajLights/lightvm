#include "home_assistant.h"
#include "preparation.h"
#include <stdio.h>
#include <string.h>

static bool playing(const AppOutput *d) {
    return app_output_power(d);
}
static bool exposed(const HardwareOutput *d,const char *name) {
    for(unsigned i=0;i<d->ha_preset_count;i++)if(!strcmp(d->ha_presets[i],name))return true;
    return false;
}
static const char *selected(const HomeAssistant *h,const AppOutput *d) {
    const char *name=app_output_selected_preset(h->app,d);
    return name&&exposed(&d->hardware,name)?name:"";
}
static void refresh(HomeAssistant *h) {
    h->available=false;
    ha_keys_refresh(h);
    for(unsigned i=0;i<HA_OUTPUT_LIMIT;i++){
        HaOutput *l=&h->outputs[i];l->discovered=false;l->known=false;l->remove_phase=0;
    }
}
static void response(void *context,uint8_t type,uint16_t id,const uint8_t *payload,size_t size) {
    HomeAssistant *h=context;(void)id;
    if(type==RESP_STATUS && size>=8){
        if(read_u32(payload))snprintf(h->app->mqtt_error,sizeof(h->app->mqtt_error),"%.*s",(int)(size-8),(const char *)payload+8);
        else h->app->mqtt_error[0]=0;
    }
}
/* Decode from owned heap scratch, then release it before command execution. */
__attribute__((noinline)) static bool light_decode(HomeAssistant *h,const AppOutput *d,const Json *j,Command *command) {
    static const char *const fields[]={"state","brightness","color","color_mode","effect",NULL};
    if(!json_fields(j,0,fields)){
        snprintf(h->app->mqtt_error,sizeof(h->app->mqtt_error),"invalid light JSON command");return false;
    }
    *command=(Command){.kind=COMMAND_LIGHT_SET,.revision=d->light_revision,.composition_revision=d->composition_revision};
    strcpy(command->output,d->hardware.id);
    int state=json_get(j,0,"state"),brightness=json_get(j,0,"brightness"),color=json_get(j,0,"color"),effect=json_get(j,0,"effect");
    if(state<0&&brightness<0&&color<0&&effect<0)return false;
    if(state>=0){
        if(!json_equal(j,state,"OFF")&&!json_equal(j,state,"ON"))goto invalid;
        command->fields|=COMMAND_POWER_VALUE;command->on=json_equal(j,state,"ON");
    }
    if(brightness>=0){
        uint32_t level;if(!json_u32(j,brightness,&level)||level>255)goto invalid;
        if(!level){command->fields|=COMMAND_POWER_VALUE;command->on=false;}
        else{command->fields|=COMMAND_BRIGHTNESS;command->brightness=level/255.0f;}
    }
    if(color>=0){
        static const char *const rgb_fields[]={"r","g","b",NULL};uint32_t r,g,b;
        if(!json_fields(j,color,rgb_fields)||!json_u32(j,json_get(j,color,"r"),&r)||r>255||
           !json_u32(j,json_get(j,color,"g"),&g)||g>255||!json_u32(j,json_get(j,color,"b"),&b)||b>255)goto invalid;
        command->fields|=COMMAND_COLOR;command->color=(r<<16)|(g<<8)|b;
    }
    if(json_get(j,0,"color_mode")>=0&&!json_equal(j,json_get(j,0,"color_mode"),"rgb"))goto invalid;
    if(effect>=0){
        if(!json_string(j,effect,command->preset,sizeof(command->preset))||!object_name_valid(command->preset)||!exposed(&d->hardware,command->preset))goto invalid;
        command->fields|=COMMAND_PRESET;
    }else if((command->fields&COMMAND_POWER_VALUE)&&command->on&&!playing(d)){
        const char *current=selected(h,d);strcpy(command->preset,current[0]?current:d->hardware.ha_default_preset);
        command->fields|=COMMAND_PRESET;
    }
    return true;
invalid:
    snprintf(h->app->mqtt_error,sizeof(h->app->mqtt_error),"invalid light command value");return false;
}__attribute__((noinline)) static bool light_command(HomeAssistant *h,const AppOutput *d,const uint8_t *payload,size_t size,Command *command){
    Json *j=platform_calloc(1,sizeof(*j));bool ok=false;
    if(!j||!json_parse(j,payload,size))snprintf(h->app->mqtt_error,sizeof(h->app->mqtt_error),"invalid light JSON or parser allocation failed");
    else ok=light_decode(h,d,j,command);
    platform_free(j);return ok;
}

static void message(void *context,const char *topic,const uint8_t *payload,size_t size,bool retained) {
    HomeAssistant *h=context;
    if(!strcmp(topic,"homeassistant/status")){
        if(size==6 && !memcmp(payload,"online",6))refresh(h);
        return;
    }
    /* Retained commands must never start playback on boot/reconnect. */
    if(retained || h->app->command_active)return;
    Command request;
    if(ha_keys_command(h,topic,payload,size,&request)){
        app_execute_command(h->app,h->source,response,h,1,&request);return;
    }
    size_t prefix=strlen(h->root);
    if(strncmp(topic,h->root,prefix) || topic[prefix]!='/')return;
    const char *id=topic+prefix+1,*slash=strchr(id,'/');
    if(!slash || (size_t)(slash-id)>OBJECT_NAME_LIMIT)return;
    char output[OBJECT_NAME_LIMIT+1];memcpy(output,id,(size_t)(slash-id));output[slash-id]=0;
    AppOutput *d=app_output(h->app,output);
    if(!output[0] || !d || !d->hardware.ha_default_preset[0])return;
    if(strcmp(slash,"/set"))return;
    if(!light_command(h,d,payload,size,&request))return;
    app_execute_command(h->app,h->source,response,h,1,&request);
    for(unsigned i=0;i<HA_OUTPUT_LIMIT;i++)if(!strcmp(h->outputs[i].id,output))h->outputs[i].known=false;
}
void ha_init(HomeAssistant *h,App *app,MqttStream stream) {
    memset(h,0,sizeof(*h));h->app=app;h->source=++app->connection_sequence;
    h->keys_pending=true;
    snprintf(h->root,sizeof(h->root),"homeassistant/lightvm/%s",app->controller_id);
    char id[96],will[MQTT_TOPIC_SIZE];snprintf(id,sizeof(id),"lightvm-%s",app->controller_id);
    snprintf(will,sizeof(will),"%s/availability",h->root);
    mqtt_init(&h->mqtt,stream,id,will,message,h);
}
void ha_close(HomeAssistant *h) {mqtt_close(&h->mqtt);h->app->mqtt_connected=false;}
static void quote_name(char *out,const char *name) {
    char *p=out;*p++='"';
    while(*name){unsigned char c=(unsigned char)*name++;
        if(c=='"'||c=='\\'){*p++='\\';*p++=(char)c;}
        else if(c<32){snprintf(p,7,"\\u%04x",c);p+=6;}
        else *p++=(char)c;
    }
    *p++='"';*p=0;
}
static void config_topic(const HomeAssistant *h,const HaOutput *l,char *topic,size_t size) {
    static const char hex[]="0123456789abcdef";
    char object[2*OBJECT_NAME_LIMIT+1];size_t n=0;
    for(const unsigned char *p=(const unsigned char *)l->id;*p;p++){object[n++]=hex[*p>>4];object[n++]=hex[*p&15];}
    object[n]=0;
    snprintf(topic,size,"homeassistant/light/%s/%s/config",h->app->controller_id,object);
}
static __attribute__((noinline)) bool discovery(HomeAssistant *h,HaOutput *l,const AppOutput *d) {
    char topic[MQTT_TOPIC_SIZE],payload[1900],name[6*65+3];
    quote_name(name,l->name);config_topic(h,l,topic,sizeof(topic));
    int n=snprintf(payload,sizeof(payload),
        "{\"name\":%s,\"unique_id\":\"%s_%s\",\"schema\":\"json\",\"brightness\":true,"
        "\"supported_color_modes\":[\"rgb\"],\"transition\":false,\"flash\":false,\"effect\":true,"
        "\"command_topic\":\"%s/%s/set\",\"state_topic\":\"%s/%s/state\",\"availability_topic\":\"%s/availability\","
        "\"qos\":0,\"retain\":false,"
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"LightVM %s\",\"manufacturer\":\"Kajlights\",\"model\":\"LightVM\"},\"effect_list\":[",
        name,h->app->controller_id,l->id,h->root,l->id,h->root,l->id,h->root,h->app->controller_id,h->app->controller_id);
    if(n<0||(size_t)n>=sizeof(payload))return false;
    size_t used=(size_t)n;bool first=true;
    for(unsigned i=0;i<d->hardware.ha_preset_count;i++){
        const char *preset=d->hardware.ha_presets[i];
        if(!store_find(&h->app->store,STORE_PRESET,preset))continue;
        n=snprintf(payload+used,sizeof(payload)-used,"%s\"%s\"",first?"":",",preset);
        if(n<0||(size_t)n>=sizeof(payload)-used)return false;
        used+=(size_t)n;first=false;
    }
    n=snprintf(payload+used,sizeof(payload)-used,"]}");
    return n>=0&&(size_t)n<sizeof(payload)-used&&mqtt_publish(&h->mqtt,topic,payload,used+(size_t)n,true);
}
static void reconcile(HomeAssistant *h) {
    /* Private remembered targets must not republish discovery on every power command. */
    bool catalog_changed=h->catalog_generation!=h->app->store.type_revision[STORE_PRESET];
    h->catalog_generation=h->app->store.type_revision[STORE_PRESET];
    for(unsigned i=0;i<HA_OUTPUT_LIMIT;i++){
        HaOutput *l=&h->outputs[i];if(!l->id[0])continue;
        AppOutput *d=app_output(h->app,l->id);
        if(!d || !d->hardware.ha_default_preset[0]){l->removing=true;continue;}
        if(l->removing){l->removing=false;l->discovered=false;l->known=false;l->remove_phase=0;}
        const char *name=d->hardware.ha_name[0]?d->hardware.ha_name:d->hardware.id;
        if(strcmp(l->name,name)){snprintf(l->name,sizeof(l->name),"%s",name);l->discovered=false;}
        uint32_t binding=crc32_bytes((const uint8_t *)d->hardware.ha_presets,sizeof(d->hardware.ha_presets));
        if(catalog_changed||binding!=l->binding_crc){l->binding_crc=binding;l->discovered=false;l->known=false;}
    }
    for(unsigned n=0;n<h->app->output_count;n++){
        AppOutput *d=h->app->outputs[n];if(!d->hardware.ha_default_preset[0])continue;
        unsigned free_slot=HA_OUTPUT_LIMIT;bool found=false;
        for(unsigned i=0;i<HA_OUTPUT_LIMIT;i++){
            if(!strcmp(h->outputs[i].id,d->hardware.id)){found=true;break;}
            if(!h->outputs[i].id[0])free_slot=i;
        }
        if(found || free_slot==HA_OUTPUT_LIMIT)continue;
        HaOutput *l=&h->outputs[free_slot];snprintf(l->id,sizeof(l->id),"%s",d->hardware.id);
        snprintf(l->name,sizeof(l->name),"%s",d->hardware.ha_name[0]?d->hardware.ha_name:d->hardware.id);
    }
}
void ha_tick(HomeAssistant *h,bool ready,uint64_t now) {
    const char *operation_error;
    if(preparation_source_result(h->app,h->source,&h->last_operation,&operation_error))
        snprintf(h->app->mqtt_error,sizeof(h->app->mqtt_error),"%s",operation_error);
    if(h->app->command_active)return;
    reconcile(h);
    ha_keys_reconcile(h);
    mqtt_tick(&h->mqtt,&h->app->network.config.mqtt,ready && !h->app->network.recovery && !h->app->network_settings.pending,now);
    h->app->mqtt_connected=h->mqtt.state==MQTT_ONLINE;
    if(h->mqtt.error[0])snprintf(h->app->mqtt_error,sizeof(h->app->mqtt_error),"%s",h->mqtt.error);
    if(!h->app->mqtt_connected){digital_inputs_discard_events(&h->app->digital_inputs);return;}
    if(h->generation!=h->mqtt.generation){h->generation=h->mqtt.generation;h->phase=0;refresh(h);h->app->mqtt_error[0]=0;}
    if(h->phase<4){
        if(h->mqtt.subscription)return;
        char filter[MQTT_TOPIC_SIZE];
        if(h->phase==0)snprintf(filter,sizeof(filter),"%s/+/set",h->root);
        else if(h->phase==1)snprintf(filter,sizeof(filter),"homeassistant/status");
        else snprintf(filter,sizeof(filter),"%s/input/+/+/%sset",h->root,h->phase==3?"press_feedback/":"");
        if(mqtt_subscribe(&h->mqtt,filter))h->phase++;
        return;
    }
    if(h->mqtt.subscription)return;
    /* One publication per tick bounds work and handles backpressure. */
    for(unsigned i=0;i<HA_OUTPUT_LIMIT;i++){
        HaOutput *l=&h->outputs[i];if(!l->id[0])continue;
        char topic[MQTT_TOPIC_SIZE];
        if(l->removing){
            if(!l->remove_phase)config_topic(h,l,topic,sizeof(topic));
            else snprintf(topic,sizeof(topic),"%s/%s/state",h->root,l->id);
            if(mqtt_publish(&h->mqtt,topic,NULL,0,true)&&++l->remove_phase==2)memset(l,0,sizeof(*l));
            return;
        }
        AppOutput *d=app_output(h->app,l->id);
        if(!l->discovered){if(discovery(h,l,d))l->discovered=true;return;}
        char state[384];size_t size=app_output_light_json(h->app,d,state,sizeof(state),true);
        uint32_t crc=crc32_bytes((const uint8_t *)state,size);
        if(!l->known||l->state_crc!=crc){
            snprintf(topic,sizeof(topic),"%s/%s/state",h->root,l->id);
            if(mqtt_publish(&h->mqtt,topic,state,size,true)){l->known=true;l->state_crc=crc;}
            return;
        }
    }
    if(!h->available){char topic[MQTT_TOPIC_SIZE];snprintf(topic,sizeof(topic),"%s/availability",h->root);if(mqtt_publish(&h->mqtt,topic,"online",6,true))h->available=true;return;}
    (void)ha_keys_tick(h,now);
}
