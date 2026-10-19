#include "home_assistant.h"
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
        HaOutput *l=&h->outputs[i];l->discovered=false;l->selector_discovered=false;l->known=false;l->preset_known=false;l->remove_phase=0;
    }
}
static void response(void *context,uint8_t type,uint16_t id,const uint8_t *payload,size_t size) {
    HomeAssistant *h=context;(void)id;
    if(type==RESP_STATUS && size>=8){
        if(read_u32(payload))snprintf(h->app->mqtt_error,sizeof(h->app->mqtt_error),"%.*s",(int)(size-8),(const char *)payload+8);
        else h->app->mqtt_error[0]=0;
    }
}
static void message(void *context,const char *topic,const uint8_t *payload,size_t size,bool retained) {
    HomeAssistant *h=context;
    if(!strcmp(topic,"homeassistant/status")){
        if(size==6 && !memcmp(payload,"online",6))refresh(h);
        return;
    }
    /* Retained commands must never start playback on boot/reconnect. */
    if(retained || h->app->command_active)return;
    char indicator_command[512];
    if(ha_keys_command(h,topic,payload,size,indicator_command,sizeof(indicator_command))){
        app_dispatch(h->app,h->source,response,h,CMD_MANAGE,1,(const uint8_t *)indicator_command,strlen(indicator_command));return;
    }
    size_t prefix=strlen(h->root);
    if(strncmp(topic,h->root,prefix) || topic[prefix]!='/')return;
    const char *id=topic+prefix+1,*slash=strchr(id,'/');
    if(!slash || (size_t)(slash-id)>OBJECT_NAME_LIMIT)return;
    char output[OBJECT_NAME_LIMIT+1];memcpy(output,id,(size_t)(slash-id));output[slash-id]=0;
    AppOutput *d=app_output(h->app,output);
    if(!output[0] || !d || !d->hardware.ha_default_preset[0])return;
    bool preset_command=!strcmp(slash,"/preset/set"),on=false,off=false;
    char preset[OBJECT_NAME_LIMIT+1];
    if(preset_command){
        if(!size||size>OBJECT_NAME_LIMIT)return;
        memcpy(preset,payload,size);preset[size]=0;
        if(memchr(payload,0,size)||!object_name_valid(preset)||!exposed(&d->hardware,preset))return;
        if(playing(d)&&!strcmp(selected(h,d),preset))return;
    }else{
        if(strcmp(slash,"/set"))return;
        on=size==2 && !memcmp(payload,"ON",2);off=size==3 && !memcmp(payload,"OFF",3);
        if(!on && !off)return;
        if(on && playing(d))return;
        const char *current=selected(h,d);
        snprintf(preset,sizeof(preset),"%s",current[0]?current:d->hardware.ha_default_preset);
    }
    char command[256];
    int n=preset_command?snprintf(command,sizeof(command),"{\"op\":\"preset_recall\",\"output\":\"%s\",\"name\":\"%s\",\"expected_revision\":%lu}",output,preset,(unsigned long)d->composition_revision)
        :snprintf(command,sizeof(command),"{\"op\":\"output_power\",\"output\":\"%s\",\"name\":\"%s\",\"action\":\"%s\",\"expected_revision\":%lu}",output,preset,off?"off":"on",(unsigned long)d->composition_revision);
    app_dispatch(h->app,h->source,response,h,CMD_MANAGE,1,(const uint8_t *)command,(size_t)n);
    for(unsigned i=0;i<HA_OUTPUT_LIMIT;i++)if(!strcmp(h->outputs[i].id,output)){h->outputs[i].known=false;h->outputs[i].preset_known=false;}
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
static void config_topic(const HomeAssistant *h,const HaOutput *l,bool selector,char *topic,size_t size) {
    static const char hex[]="0123456789abcdef";
    char object[2*OBJECT_NAME_LIMIT+1];size_t n=0;
    for(const unsigned char *p=(const unsigned char *)l->id;*p;p++){object[n++]=hex[*p>>4];object[n++]=hex[*p&15];}
    object[n]=0;
    snprintf(topic,size,"homeassistant/%s/%s/%s%s/config",selector?"select":"light",h->app->controller_id,object,selector?"_preset":"");
}
static bool discovery(HomeAssistant *h,HaOutput *l,const AppOutput *d,bool selector) {
    char topic[MQTT_TOPIC_SIZE],payload[1900],name[6*71+3],label[72];
    snprintf(label,sizeof(label),"%s%s",l->name,selector?" Preset":"");quote_name(name,label);
    config_topic(h,l,selector,topic,sizeof(topic));
    int n=snprintf(payload,sizeof(payload),
        "{\"name\":%s,\"unique_id\":\"%s_%s%s\",\"command_topic\":\"%s/%s/%sset\","
        "\"state_topic\":\"%s/%s/%sstate\",\"availability_topic\":\"%s/availability\","
        "\"qos\":0,\"retain\":false,"
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"LightVM %s\",\"manufacturer\":\"Kajlights\",\"model\":\"LightVM\"}",
        name,h->app->controller_id,l->id,selector?"_preset":"",h->root,l->id,selector?"preset/":"",
        h->root,l->id,selector?"preset/":"",h->root,h->app->controller_id,h->app->controller_id);
    if(n<0 || (size_t)n>=sizeof(payload))return false;
    size_t used=(size_t)n;
    if(selector){
        n=snprintf(payload+used,sizeof(payload)-used,",\"options\":[");
        /* Keep the output name in the selector label; all outputs share a controller. */
        if(n<0 || (size_t)n>=sizeof(payload)-used)return false;
        used+=(size_t)n;bool first=true;
        for(unsigned i=0;i<d->hardware.ha_preset_count;i++){
            const char *preset=d->hardware.ha_presets[i];
            if(!store_find(&h->app->store,STORE_PRESET,preset))continue;
            n=snprintf(payload+used,sizeof(payload)-used,"%s\"%s\"",first?"":",",preset);
            if(n<0 || (size_t)n>=sizeof(payload)-used)return false;
            used+=(size_t)n;first=false;
        }
        n=snprintf(payload+used,sizeof(payload)-used,"]}");
    }else n=snprintf(payload+used,sizeof(payload)-used,",\"payload_on\":\"ON\",\"payload_off\":\"OFF\"}");
    if(n<0 || (size_t)n>=sizeof(payload)-used)return false;
    used+=(size_t)n;
    return mqtt_publish(&h->mqtt,topic,payload,used,true);
}
static void reconcile(HomeAssistant *h) {
    /* Private remembered targets must not republish discovery on every power command. */
    bool catalog_changed=h->catalog_generation!=h->app->store.preset_revision;
    h->catalog_generation=h->app->store.preset_revision;
    for(unsigned i=0;i<HA_OUTPUT_LIMIT;i++){
        HaOutput *l=&h->outputs[i];if(!l->id[0])continue;
        AppOutput *d=app_output(h->app,l->id);
        if(!d || !d->hardware.ha_default_preset[0]){l->removing=true;continue;}
        if(l->removing){l->removing=false;l->discovered=false;l->known=false;l->selector_discovered=false;l->preset_known=false;l->remove_phase=0;}
        const char *name=d->hardware.ha_name[0]?d->hardware.ha_name:d->hardware.id;
        if(strcmp(l->name,name)){snprintf(l->name,sizeof(l->name),"%s",name);l->discovered=false;l->selector_discovered=false;}
        uint32_t binding=crc32_bytes((const uint8_t *)d->hardware.ha_presets,sizeof(d->hardware.ha_presets));
        if(catalog_changed||binding!=l->binding_crc){l->binding_crc=binding;l->selector_discovered=false;l->preset_known=false;}
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
    if(h->app->command_active)return;
    reconcile(h);
    ha_keys_reconcile(h);
    mqtt_tick(&h->mqtt,&h->app->network.config.mqtt,ready && !h->app->network.recovery && !h->app->network_pending,now);
    h->app->mqtt_connected=h->mqtt.state==MQTT_ONLINE;
    if(h->mqtt.error[0])snprintf(h->app->mqtt_error,sizeof(h->app->mqtt_error),"%s",h->mqtt.error);
    if(!h->app->mqtt_connected){digital_inputs_discard_events(&h->app->digital_inputs);return;}
    if(h->generation!=h->mqtt.generation){h->generation=h->mqtt.generation;h->phase=0;refresh(h);h->app->mqtt_error[0]=0;}
    if(h->phase<5){
        if(h->mqtt.subscription)return;
        char filter[MQTT_TOPIC_SIZE];
        if(h->phase<2)snprintf(filter,sizeof(filter),"%s/+/%sset",h->root,h->phase?"preset/":"");
        else if(h->phase==2)snprintf(filter,sizeof(filter),"homeassistant/status");
        else snprintf(filter,sizeof(filter),"%s/input/+/+/%sset",h->root,h->phase==4?"press_feedback/":"");
        if(mqtt_subscribe(&h->mqtt,filter))h->phase++;
        return;
    }
    if(h->mqtt.subscription)return;
    /* One publication per tick bounds work and handles backpressure. */
    for(unsigned i=0;i<HA_OUTPUT_LIMIT;i++){
        HaOutput *l=&h->outputs[i];if(!l->id[0])continue;
        char topic[MQTT_TOPIC_SIZE];
        if(l->removing){
            if(l->remove_phase<2)config_topic(h,l,l->remove_phase==1,topic,sizeof(topic));
            else snprintf(topic,sizeof(topic),"%s/%s/%sstate",h->root,l->id,l->remove_phase==3?"preset/":"");
            if(mqtt_publish(&h->mqtt,topic,NULL,0,true)&&++l->remove_phase==4)memset(l,0,sizeof(*l));
            return;
        }
        AppOutput *d=app_output(h->app,l->id);
        if(!l->discovered){if(discovery(h,l,d,false))l->discovered=true;return;}
        if(!l->selector_discovered){if(discovery(h,l,d,true))l->selector_discovered=true;return;}
        bool on=playing(d);
        if(!l->known || l->playing!=on){
            snprintf(topic,sizeof(topic),"%s/%s/state",h->root,l->id);
            if(mqtt_publish(&h->mqtt,topic,on?"ON":"OFF",on?2:3,true)){l->known=true;l->playing=on;}
            return;
        }
        const char *preset=selected(h,d);
        if(!l->preset_known||strcmp(l->preset,preset)){
            snprintf(topic,sizeof(topic),"%s/%s/preset/state",h->root,l->id);
            const char *value=preset[0]?preset:"None";
            if(mqtt_publish(&h->mqtt,topic,value,strlen(value),true)){l->preset_known=true;snprintf(l->preset,sizeof(l->preset),"%s",preset);}
            return;
        }
    }
    if(!h->available){char topic[MQTT_TOPIC_SIZE];snprintf(topic,sizeof(topic),"%s/availability",h->root);if(mqtt_publish(&h->mqtt,topic,"online",6,true))h->available=true;return;}
    (void)ha_keys_tick(h,now);
}
