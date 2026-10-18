#include "home_assistant.h"
#include <stdio.h>
#include <string.h>

static bool playing(const AppDevice *d) {
    return d->transition?d->transition->playing:d->scene?d->scene->playing:d->vm.playing;
}
static void refresh(HomeAssistant *h) {
    h->available=false;
    for(unsigned i=0;i<HA_LIGHT_LIMIT;i++){h->lights[i].discovered=false;h->lights[i].known=false;h->lights[i].config_removed=false;}
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
    size_t prefix=strlen(h->root);
    if(strncmp(topic,h->root,prefix) || topic[prefix]!='/')return;
    const char *id=topic+prefix+1,*slash=strchr(id,'/');
    if(!slash || strcmp(slash,"/set") || (size_t)(slash-id)>OBJECT_NAME_LIMIT)return;
    char device[OBJECT_NAME_LIMIT+1];memcpy(device,id,(size_t)(slash-id));device[slash-id]=0;
    AppDevice *d=app_device(h->app,device);
    if(!device[0] || !d || !d->hardware.ha_preset[0])return;
    bool on=size==2 && !memcmp(payload,"ON",2),off=size==3 && !memcmp(payload,"OFF",3);
    if(!on && !off)return;
    /* QoS1 retransmissions and repeated ON do not restart a running effect. */
    if(on && playing(d))return;
    char command[256];
    int n=on?snprintf(command,sizeof(command),"{\"op\":\"preset_activate\",\"device\":\"%s\",\"name\":\"%s\",\"expected_revision\":%lu}",device,d->hardware.ha_preset,(unsigned long)d->scene_revision)
        :snprintf(command,sizeof(command),"{\"op\":\"device_command\",\"device\":\"%s\",\"command\":%u}",device,CMD_STOP);
    app_dispatch(h->app,h->source,response,h,CMD_MANAGE,1,(const uint8_t *)command,(size_t)n);
    for(unsigned i=0;i<HA_LIGHT_LIMIT;i++)if(!strcmp(h->lights[i].id,device))h->lights[i].known=false;
}
void ha_init(HomeAssistant *h,App *app,MqttStream stream) {
    memset(h,0,sizeof(*h));h->app=app;h->source=++app->connection_sequence;
    snprintf(h->root,sizeof(h->root),"homeassistant/lightvm/%s",app->device_id);
    char id[96],will[MQTT_TOPIC_SIZE];snprintf(id,sizeof(id),"lightvm-%s",app->device_id);
    snprintf(will,sizeof(will),"%s/availability",h->root);
    mqtt_init(&h->mqtt,stream,id,will,message,h);
}
void ha_close(HomeAssistant *h) {mqtt_close(&h->mqtt);h->app->mqtt_connected=false;}
/* Names are the only arbitrary text in discovery. A bounded JSON string escape
 * keeps names with quotes, backslashes, Unicode or control characters valid. */
static void quote_name(char *out,const char *name) {
    char *p=out;*p++='"';
    while(*name){unsigned char c=(unsigned char)*name++;
        if(c=='"'||c=='\\'){*p++='\\';*p++=(char)c;}
        else if(c<32){snprintf(p,7,"\\u%04x",c);p+=6;}
        else *p++=(char)c;
    }
    *p++='"';*p=0;
}
static void config_topic(const HomeAssistant *h,const HaLight *l,char *topic,size_t size) {
    /* Hardware IDs also allow dots, which HA discovery object IDs forbid.
     * Hex encoding is collision free for every valid hardware identifier. */
    static const char hex[]="0123456789abcdef";
    char object[2*OBJECT_NAME_LIMIT+1];size_t n=0;
    for(const unsigned char *p=(const unsigned char *)l->id;*p;p++){object[n++]=hex[*p>>4];object[n++]=hex[*p&15];}
    object[n]=0;
    snprintf(topic,size,"homeassistant/light/%s/%s/config",h->app->device_id,object);
}
static bool discovery(HomeAssistant *h,HaLight *l) {
    char topic[MQTT_TOPIC_SIZE],payload[1400],name[6*64+3];quote_name(name,l->name);
    config_topic(h,l,topic,sizeof(topic));
    int n=snprintf(payload,sizeof(payload),
        "{\"name\":%s,\"unique_id\":\"%s_%s\",\"command_topic\":\"%s/%s/set\","
        "\"state_topic\":\"%s/%s/state\",\"availability_topic\":\"%s/availability\","
        "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"qos\":0,\"retain\":false,"
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"LightVM %s\",\"manufacturer\":\"Kajlights\",\"model\":\"LightVM\"}}",
        name,h->app->device_id,l->id,h->root,l->id,h->root,l->id,h->root,h->app->device_id,h->app->device_id);
    return n>0 && (size_t)n<sizeof(payload) && mqtt_publish(&h->mqtt,topic,payload,(size_t)n,true);
}
static void reconcile(HomeAssistant *h) {
    for(unsigned i=0;i<HA_LIGHT_LIMIT;i++){
        HaLight *l=&h->lights[i];if(!l->id[0])continue;
        AppDevice *d=app_device(h->app,l->id);
        if(!d || !d->hardware.ha_preset[0]){l->removing=true;continue;}
        if(l->removing){l->removing=false;l->discovered=false;l->known=false;l->config_removed=false;}
        const char *name=d->hardware.ha_name[0]?d->hardware.ha_name:d->hardware.id;
        if(strcmp(l->name,name)){snprintf(l->name,sizeof(l->name),"%s",name);l->discovered=false;}
    }
    for(unsigned n=0;n<h->app->device_count;n++){
        AppDevice *d=h->app->devices[n];if(!d->hardware.ha_preset[0])continue;
        unsigned free_slot=HA_LIGHT_LIMIT;bool found=false;
        for(unsigned i=0;i<HA_LIGHT_LIMIT;i++){
            if(!strcmp(h->lights[i].id,d->hardware.id)){found=true;break;}
            if(!h->lights[i].id[0])free_slot=i;
        }
        if(found || free_slot==HA_LIGHT_LIMIT)continue;
        HaLight *l=&h->lights[free_slot];snprintf(l->id,sizeof(l->id),"%s",d->hardware.id);
        snprintf(l->name,sizeof(l->name),"%s",d->hardware.ha_name[0]?d->hardware.ha_name:d->hardware.id);
        l->discovered=false;l->known=false;
    }
}
void ha_tick(HomeAssistant *h,bool ready,uint64_t now) {
    if(h->app->command_active)return;
    reconcile(h);
    mqtt_tick(&h->mqtt,&h->app->network.config.mqtt,ready && !h->app->network.recovery && !h->app->network_pending,now);
    h->app->mqtt_connected=h->mqtt.state==MQTT_ONLINE;
    if(h->mqtt.error[0])snprintf(h->app->mqtt_error,sizeof(h->app->mqtt_error),"%s",h->mqtt.error);
    if(!h->app->mqtt_connected)return;
    if(h->generation!=h->mqtt.generation){h->generation=h->mqtt.generation;h->phase=0;refresh(h);h->app->mqtt_error[0]=0;}
    if(h->phase==0){char filter[MQTT_TOPIC_SIZE];snprintf(filter,sizeof(filter),"%s/+/set",h->root);if(mqtt_subscribe(&h->mqtt,filter))h->phase++;return;}
    if(h->mqtt.subscription)return;
    if(h->phase==1){if(mqtt_subscribe(&h->mqtt,"homeassistant/status"))h->phase++;return;}
    /* One publication per tick bounds engine work and handles backpressure. */
    for(unsigned i=0;i<HA_LIGHT_LIMIT;i++){
        HaLight *l=&h->lights[i];if(!l->id[0])continue;
        char topic[MQTT_TOPIC_SIZE];
        if(l->removing){
            if(!l->config_removed){config_topic(h,l,topic,sizeof(topic));if(mqtt_publish(&h->mqtt,topic,NULL,0,true))l->config_removed=true;return;}
            snprintf(topic,sizeof(topic),"%s/%s/state",h->root,l->id);
            if(mqtt_publish(&h->mqtt,topic,NULL,0,true))memset(l,0,sizeof(*l));
            return;
        }
        if(!l->discovered){if(discovery(h,l))l->discovered=true;return;}
        AppDevice *d=app_device(h->app,l->id);bool on=d && playing(d);
        if(!l->known || l->playing!=on){
            snprintf(topic,sizeof(topic),"%s/%s/state",h->root,l->id);
            if(mqtt_publish(&h->mqtt,topic,on?"ON":"OFF",on?2:3,true)){l->known=true;l->playing=on;}
            return;
        }
    }
    if(!h->available){char topic[MQTT_TOPIC_SIZE];snprintf(topic,sizeof(topic),"%s/availability",h->root);if(mqtt_publish(&h->mqtt,topic,"online",6,true))h->available=true;}
}
