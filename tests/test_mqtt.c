/* Exercise the transport without a network, engine or HA dependency. */
#include "mqtt.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint8_t input[4096],output[8192];size_t size,used,written;unsigned opens,closes,messages;bool blocked;size_t chunk;} Fake;
static bool open_fake(void *ctx,const char *host,uint16_t port){Fake *f=ctx;assert(!strcmp(host,"127.0.0.1")&&port==1883);f->opens++;return true;}
static int poll_fake(void *ctx){(void)ctx;return 1;}
static int read_fake(void *ctx,uint8_t *out,size_t capacity){Fake *f=ctx;size_t n=f->size-f->used;if(n>capacity)n=capacity;if(f->chunk&&n>f->chunk)n=f->chunk;memcpy(out,f->input+f->used,n);f->used+=n;return (int)n;}
static int write_fake(void *ctx,const uint8_t *bytes,size_t n){Fake *f=ctx;if(f->blocked)return 0;if(f->chunk&&n>f->chunk)n=f->chunk;assert(n<=sizeof(f->output)-f->written);memcpy(f->output+f->written,bytes,n);f->written+=n;return (int)n;}
static void close_fake(void *ctx){((Fake *)ctx)->closes++;}
static void message(void *ctx,const char *topic,const uint8_t *p,size_t size,bool retained){Fake *f=ctx;assert(!strcmp(topic,"a/b")&&size==2&&!memcmp(p,"ON",2)&&retained);f->messages++;}
static const MqttConfig config={.enabled=true,.port=1883,.host="127.0.0.1",.username="user",.password="secret"};
static void init(Mqtt *m,Fake *f){memset(f,0,sizeof(*f));mqtt_init(m,(MqttStream){f,open_fake,poll_fake,read_fake,write_fake,close_fake},"test","a/availability",message,f);mqtt_tick(m,&config,true,1);assert(m->state==MQTT_CONNACK);}
static void feed(Fake *f,const uint8_t *p,size_t n){assert(f->used==f->size);memcpy(f->input,p,n);f->size=n;f->used=0;}
static void online(Mqtt *m,Fake *f){const uint8_t ack[]={0x20,2,0,0};feed(f,ack,sizeof(ack));mqtt_tick(m,&config,true,2);assert(m->state==MQTT_ONLINE);}
int main(void){
    Mqtt m;Fake f;init(&m,&f);
    assert(f.output[0]==0x10 && f.output[9]==0xe6); /* username, password, retained will, clean session */
    assert(memcmp(f.output+4,"MQTT",4)==0);online(&m,&f);
    MqttConfig identical=config;identical.host[15]='x';
    mqtt_tick(&m,&identical,true,3);assert(m.state==MQTT_ONLINE && f.opens==1);
    f.chunk=1;const uint8_t publish[]={0x33,9,0,3,'a','/','b',0,7,'O','N'};feed(&f,publish,sizeof(publish));
    for(unsigned i=0;i<sizeof(publish)+4;i++)mqtt_tick(&m,&config,true,10+i);
    assert(f.messages==1);assert(!memcmp(f.output+f.written-4,"\x40\x02\x00\x07",4));
    assert(mqtt_subscribe(&m,"a/+/set"));assert(!mqtt_subscribe(&m,"other"));
    uint8_t suback[]={0x90,3,0,1,0};feed(&f,suback,sizeof(suback));
    for(unsigned i=0;i<5;i++)mqtt_tick(&m,&config,true,30+i);
    assert(!m.subscription);
    uint8_t bad[]={0x30,0xff,0xff,0xff,0xff};feed(&f,bad,sizeof(bad));
    for(unsigned i=0;i<5;i++)mqtt_tick(&m,&config,true,40+i);
    assert(m.state==MQTT_DOWN&&m.error[0]);unsigned opens=f.opens;mqtt_tick(&m,&config,true,45);assert(f.opens==opens);
    mqtt_tick(&m,&config,true,1000045);assert(f.opens==opens+1);
    mqtt_close(&m);
    init(&m,&f);uint8_t denied[]={0x20,2,0,5};feed(&f,denied,sizeof(denied));mqtt_tick(&m,&config,true,2);assert(m.state==MQTT_DOWN);
    init(&m,&f);online(&m,&f);mqtt_tick(&m,&config,true,15000001);assert(m.ping_deadline);
    mqtt_tick(&m,&config,true,25000001);assert(m.state==MQTT_DOWN);
    init(&m,&f);online(&m,&f);f.blocked=true;assert(mqtt_publish(&m,"a/state","OFF",3,true));mqtt_tick(&m,&config,true,6000002);assert(m.state==MQTT_DOWN);
    init(&m,&f);online(&m,&f);uint8_t oversized[]={0x30,0xff,0x7f};feed(&f,oversized,sizeof(oversized));mqtt_tick(&m,&config,true,3);assert(m.state==MQTT_DOWN);
    init(&m,&f);online(&m,&f);assert(mqtt_subscribe(&m,"a/set"));uint8_t rejected[]={0x90,3,0,1,0x80};feed(&f,rejected,sizeof(rejected));mqtt_tick(&m,&config,true,3);assert(m.state==MQTT_DOWN);
    init(&m,&f);online(&m,&f);mqtt_tick(&m,&config,false,3);assert(m.state==MQTT_DOWN);
    puts("MQTT transport contracts passed");return 0;
}
