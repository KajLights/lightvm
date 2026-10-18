#include "home_assistant.h"
#include <stdio.h>
#include <string.h>

#define KEY_LIMIT (DIGITAL_INPUT_LIMIT*DIGITAL_INPUT_CHANNELS)
static void key_root(const HomeAssistant *h,const HaKey *k,char *out,size_t capacity) {
    snprintf(out,capacity,"%s/input/%s/%u",h->root,k->input,k->channel+1);
}
static void object_id(const HaKey *k,char *out,size_t capacity) {
    static const char hex[]="0123456789abcdef";char id[2*OBJECT_NAME_LIMIT+1];size_t n=0;
    for(const unsigned char *p=(const unsigned char *)k->input;*p;p++){id[n++]=hex[*p>>4];id[n++]=hex[*p&15];}id[n]=0;
    snprintf(out,capacity,"i_%s_%u",id,k->channel+1);
}
static void config_topic(const HomeAssistant *h,const HaKey *k,unsigned kind,char *out,size_t capacity) {
    char object[80];object_id(k,object,sizeof(object));
    snprintf(out,capacity,"homeassistant/%s/%s/%s%s/config",kind==2?"light":"device_automation",h->app->controller_id,object,kind==0?"_press":kind==1?"_release":"");
}
static void quote(char *out,const char *in) {
    *out++='"';while(*in){unsigned char c=(unsigned char)*in++;if(c=='"'||c=='\\'){*out++='\\';*out++=(char)c;}else if(c<32){snprintf(out,7,"\\u%04x",c);out+=6;}else *out++=(char)c;}*out++='"';*out=0;
}
static bool discover(HomeAssistant *h,HaKey *k,unsigned kind) {
    char topic[MQTT_TOPIC_SIZE],root[160],object[80],payload[1600],name[256],label[48];
    config_topic(h,k,kind,topic,sizeof(topic));key_root(h,k,root,sizeof(root));object_id(k,object,sizeof(object));
    snprintf(label,sizeof(label),"%s indicator",k->name);quote(name,label);
    int n;
    if(kind==2)n=snprintf(payload,sizeof(payload),
        "{\"name\":%s,\"unique_id\":\"lightvm_input_%s_%s\",\"schema\":\"json\",\"brightness\":true,\"supported_color_modes\":[\"rgb\"],\"transition\":false,\"flash\":false,"
        "\"command_topic\":\"%s/set\",\"state_topic\":\"%s/state\",\"qos\":0,\"retain\":false,"
        "\"availability_mode\":\"all\",\"availability\":[{\"topic\":\"%s/availability\"},{\"topic\":\"%s/availability\"}],"
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"LightVM %s\",\"manufacturer\":\"Kajlights\",\"model\":\"LightVM\"}}",
        name,h->app->controller_id,object,root,root,h->root,root,h->app->controller_id,h->app->controller_id);
    else {
        char subtype[80];snprintf(subtype,sizeof(subtype),"%s_key_%u",k->input,k->channel+1);
        n=snprintf(payload,sizeof(payload),
        "{\"automation_type\":\"trigger\",\"type\":\"button_short_%s\",\"subtype\":\"%s\",\"topic\":\"%s/event\",\"payload\":\"%s\",\"qos\":0,"
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"LightVM %s\",\"manufacturer\":\"Kajlights\",\"model\":\"LightVM\"}}",
        kind?"release":"press",subtype,root,kind?"release":"press",h->app->controller_id,h->app->controller_id);
    }
    return n>0&&(size_t)n<sizeof(payload)&&mqtt_publish(&h->mqtt,topic,payload,(size_t)n,true);
}
static const DigitalInputConfig *find(const HomeAssistant *h,const char *id) {
    for(unsigned i=0;i<h->app->digital_inputs.count;i++){
        const DigitalInputConfig *c=&h->app->digital_inputs.devices[i].config;
        if(c->home_assistant&&!strcmp(c->id,id))return c;
    }
    return NULL;
}
void ha_keys_refresh(HomeAssistant *h) {
    for(unsigned i=0;i<KEY_LIMIT;i++){h->keys[i].phase=0;h->keys[i].known=false;h->keys[i].availability_known=false;h->keys[i].remove_phase=0;}
    digital_inputs_discard_events(&h->app->digital_inputs);
}
void ha_keys_reconcile(HomeAssistant *h) {
    if(h->key_revision==h->app->revision&&!h->keys_pending)return;
    h->key_revision=h->app->revision;h->keys_pending=false;
    for(unsigned i=0;i<KEY_LIMIT;i++){
        HaKey *k=&h->keys[i];if(!k->input[0])continue;
        const DigitalInputConfig *c=find(h,k->input);
        if(!c){k->removing=true;h->keys_pending=true;continue;}
        char name[33];snprintf(name,sizeof(name),"%s",c->names[k->channel]);
        if(!name[0])snprintf(name,sizeof(name),"Key %u",k->channel+1);
        if(k->removing||strcmp(k->name,name)||strcmp(k->output,c->indicators)){
            // Clear obsolete light discovery before dropping an indicator binding.
            if(k->output[0]&&!c->indicators[0]){k->removing=true;h->keys_pending=true;continue;}
            snprintf(k->name,sizeof(k->name),"%s",name);snprintf(k->output,sizeof(k->output),"%s",c->indicators);
            k->removing=false;k->phase=0;k->known=false;k->availability_known=false;k->remove_phase=0;
        }
    }
    for(unsigned i=0;i<h->app->digital_inputs.count;i++){
        const DigitalInputConfig *c=&h->app->digital_inputs.devices[i].config;if(!c->home_assistant)continue;
        for(unsigned channel=0;channel<16;channel++){
            HaKey *free_slot=NULL;bool found=false;
            for(unsigned n=0;n<KEY_LIMIT;n++){
                HaKey *k=&h->keys[n];if(!k->input[0]){if(!free_slot)free_slot=k;continue;}
                if(k->channel==channel&&!strcmp(k->input,c->id)){found=true;break;}
            }
            if(found||!free_slot)continue;
            HaKey *k=free_slot;snprintf(k->input,sizeof(k->input),"%s",c->id);k->channel=(unsigned char)channel;
            snprintf(k->output,sizeof(k->output),"%s",c->indicators);snprintf(k->name,sizeof(k->name),"%s",c->names[channel]);
            if(!k->name[0])snprintf(k->name,sizeof(k->name),"Key %u",channel+1);
        }
    }
}
bool ha_keys_command(HomeAssistant *h,const char *topic,const uint8_t *payload,size_t size,char *command,size_t capacity) {
    if(!size||size>384||memchr(payload,0,size))return false;
    for(unsigned i=0;i<KEY_LIMIT;i++){
        HaKey *k=&h->keys[i];if(!k->input[0]||k->removing||!k->output[0])continue;
        char root[160],expected[MQTT_TOPIC_SIZE];key_root(h,k,root,sizeof(root));
        int n=snprintf(expected,sizeof(expected),"%s/set",root);if(n<0||(size_t)n>=sizeof(expected)||strcmp(topic,expected))continue;
        n=snprintf(command,capacity,"{\"op\":\"indicator_set\",\"output\":\"%s\",\"pixel\":%u,\"value\":%.*s}",k->output,k->channel,(int)size,(const char *)payload);
        return n>0&&(size_t)n<capacity;
    }
    return false;
}
bool ha_keys_tick(HomeAssistant *h,uint64_t now) {
    // One attempted publication per call. Retained lifecycle work precedes events.
    for(unsigned i=0;i<KEY_LIMIT;i++){
        HaKey *k=&h->keys[i];if(!k->input[0])continue;
        char topic[MQTT_TOPIC_SIZE],root[160];key_root(h,k,root,sizeof(root));
        if(k->removing){
            if(k->remove_phase<3)config_topic(h,k,k->remove_phase,topic,sizeof(topic));
            else snprintf(topic,sizeof(topic),"%s/%s",root,k->remove_phase==3?"state":"availability");
            if(mqtt_publish(&h->mqtt,topic,NULL,0,true)&&++k->remove_phase==5)memset(k,0,sizeof(*k));
            return true;
        }
        if(k->phase<3){
            if(k->phase==2&&!k->output[0]){k->phase++;continue;}
            if(discover(h,k,k->phase))k->phase++;
            return true;
        }
    }
    // Expire stale events and never replay an outage backlog. Successful queueing
    // consumes an occurrence; QoS0 deliberately makes no exactly-once promise.
    for(unsigned n=0;n<DIGITAL_INPUT_EVENT_LIMIT&&h->app->digital_inputs.queued;n++){
        DigitalInputEvent e=h->app->digital_inputs.events[h->app->digital_inputs.head];
        const char *id=e.input<h->app->digital_inputs.count?h->app->digital_inputs.devices[e.input].config.id:"";
        HaKey *k=NULL;for(unsigned i=0;i<KEY_LIMIT;i++)if(h->keys[i].input[0]&&!h->keys[i].removing&&h->keys[i].channel==e.channel&&!strcmp(h->keys[i].input,id)){k=&h->keys[i];break;}
        if(!k||now-e.time>DIGITAL_INPUT_EVENT_TTL_US){digital_inputs_event(&h->app->digital_inputs,&e);continue;}
        char root[160],topic[MQTT_TOPIC_SIZE];key_root(h,k,root,sizeof(root));snprintf(topic,sizeof(topic),"%s/event",root);
        const char *value=e.pressed?"press":"release";
        if(mqtt_publish(&h->mqtt,topic,value,strlen(value),false))digital_inputs_event(&h->app->digital_inputs,&e);
        return true;
    }
    for(unsigned i=0;i<KEY_LIMIT;i++){
        HaKey *k=&h->keys[i];if(!k->input[0]||k->removing||!k->output[0])continue;
        AppOutput *d=app_output(h->app,k->output);if(!d)continue;
        char root[160],topic[MQTT_TOPIC_SIZE],state[160];key_root(h,k,root,sizeof(root));
        bool available=!d->indicator_fault;
        if(!k->availability_known||k->available!=available){
            snprintf(topic,sizeof(topic),"%s/availability",root);
            if(mqtt_publish(&h->mqtt,topic,available?"online":"offline",available?6:7,true)){k->available=available;k->availability_known=true;}
            return true;
        }
        size_t size=app_indicator_json(d,k->channel,state,sizeof(state));if(!size)continue;
        uint32_t crc=crc32_bytes((const uint8_t *)state,size);
        if(!k->known||k->state_crc!=crc){
            snprintf(topic,sizeof(topic),"%s/state",root);
            if(mqtt_publish(&h->mqtt,topic,state,size,true)){k->known=true;k->state_crc=crc;}
            return true;
        }
    }
    return false;
}
