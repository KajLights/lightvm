// Portable asynchronous MQTT 3.1.1 experiment. No native MQTT/HA dependencies.
#include "core_mqtt_serializer.h"
#include <string.h>
#define LIMIT 2048u
#define KEEPALIVE 10u
#ifdef __wasm__
#define IMPORT(name) __attribute__((import_module("lightvm.experimental.v1"),import_name(name)))
#define EXPORT(name) __attribute__((export_name(name)))
#else
#define IMPORT(name)
#define EXPORT(name)
#endif
IMPORT("tcp_connect") int32_t net_connect(uint32_t,uint32_t);
IMPORT("tcp_state") int32_t net_state(uint32_t);
IMPORT("tcp_read") int32_t net_read(uint32_t,void *,uint32_t);
IMPORT("tcp_write") int32_t net_write(uint32_t,const void *,uint32_t);
IMPORT("tcp_close") int32_t net_close(uint32_t);
IMPORT("time_ms") uint64_t now_ms(void);
IMPORT("identity") int32_t device_identity(char *,uint32_t);
typedef struct { uint32_t generation,revision; double speed; uint32_t frames,heap,render_us,result,commands,rejected; } Animation;
IMPORT("animation_state") int32_t animation_state(Animation *,uint32_t);
IMPORT("speed_set") int32_t speed_set(double,uint32_t,uint32_t);

static uint8_t rx[LIMIT],tx[LIMIT],outstanding[LIMIT],seen_ids[8192];
static size_t rx_size,tx_size,tx_offset,outstanding_size;
static uint32_t address,port,handle,state,discovery_phase,reconnects,malformed,duplicate_commands;
static uint16_t sequence,pending_id,subscription_id;
static uint64_t retry_at,activity,progress_at,pending_at,ping_at,diagnostic_at;
static bool ping_pending,state_dirty;
static char identity[65],base[100],command_topic[128],availability[128],scratch[1200],topic[180];
static Animation animation;

typedef struct { char *p; size_t size,used; bool failed; } Text;
static void append(Text *b,const char *s) {
    size_t n=strlen(s); if (n>=b->size-b->used) { b->failed=true; return; }
    memcpy(b->p+b->used,s,n); b->used+=n; b->p[b->used]=0;
}
static void integer(Text *b,uint32_t v) {
    char s[11]; size_t n=0; do { s[n++]=(char)('0'+v%10); v/=10; } while (v);
    while (n) { char c[2]={s[--n],0}; append(b,c); }
}
static void decimal(Text *b,double v) {
    if (v<0) { append(b,"-"); v=-v; }
    uint32_t scaled=(uint32_t)(v*1000000.0+0.5); integer(b,scaled/1000000u); append(b,".");
    uint32_t fraction=scaled%1000000u; for (uint32_t divisor=100000;divisor;divisor/=10) { char c[2]={(char)('0'+fraction/divisor),0}; append(b,c); fraction%=divisor; }
}
static void make_topic(char *p,size_t n,const char *suffix) { Text b={p,n,0,false}; p[0]=0; append(&b,base); append(&b,suffix); }
static uint16_t packet_id(void) { if (!++sequence) ++sequence; return sequence; }
static void disconnect(void) {
    if (handle) net_close(handle);
    handle=0; state=0; rx_size=tx_size=tx_offset=outstanding_size=0; pending_id=subscription_id=0;
    ping_pending=false; retry_at=now_ms()+1000; ++reconnects;
}
static bool queue_publish(const char *name,const char *payload,MQTTQoS_t qos,bool retain) {
    if (tx_size || (qos==MQTTQoS1 && pending_id)) return false;
    MQTTPublishInfo_t p={.qos=qos,.retain=retain,.pTopicName=name,.topicNameLength=(uint16_t)strlen(name),.pPayload=payload,.payloadLength=strlen(payload)};
    size_t remaining,size; MQTTFixedBuffer_t b={tx,sizeof(tx)};
    uint16_t id=qos==MQTTQoS1?packet_id():0;
    if (MQTT_GetPublishPacketSize(&p,&remaining,&size)!=MQTTSuccess || size>LIMIT || MQTT_SerializePublish(&p,id,remaining,&b)!=MQTTSuccess) return false;
    tx_size=size; tx_offset=0;
    if (id) { memcpy(outstanding,tx,size); outstanding_size=size; pending_id=id; pending_at=now_ms(); }
    return true;
}
static bool connect_packet(void) {
    MQTTConnectInfo_t c={.cleanSession=true,.keepAliveSeconds=KEEPALIVE,.pClientIdentifier=identity,.clientIdentifierLength=(uint16_t)strlen(identity)};
    MQTTPublishInfo_t will={.qos=MQTTQoS1,.retain=true,.pTopicName=availability,.topicNameLength=(uint16_t)strlen(availability),.pPayload="offline",.payloadLength=7};
    MQTTFixedBuffer_t b={tx,LIMIT}; size_t remaining,size;
    if (MQTT_GetConnectPacketSize(&c,&will,&remaining,&size)!=MQTTSuccess || size>LIMIT || MQTT_SerializeConnect(&c,&will,remaining,&b)!=MQTTSuccess) return false;
    tx_size=size; tx_offset=0; progress_at=now_ms(); return true;
}
static bool subscribe(void) {
    MQTTSubscribeInfo_t subscriptions[2]={
        {.qos=MQTTQoS1,.pTopicFilter=command_topic,.topicFilterLength=(uint16_t)strlen(command_topic)},
        {.qos=MQTTQoS0,.pTopicFilter="homeassistant/status",.topicFilterLength=20}};
    size_t remaining,size; MQTTFixedBuffer_t b={tx,LIMIT}; subscription_id=packet_id();
    if (MQTT_GetSubscribePacketSize(subscriptions,2,&remaining,&size)!=MQTTSuccess || size>LIMIT || MQTT_SerializeSubscribe(subscriptions,2,subscription_id,remaining,&b)!=MQTTSuccess) return false;
    tx_size=size; tx_offset=0; progress_at=now_ms(); return true;
}
static bool discovery(uint32_t index) {
    static const char *const keys[]={"speed","vm_heap","render_us","commands"};
    Text t={topic,sizeof(topic),0,false}; topic[0]=0; append(&t,"homeassistant/"); append(&t,index?"sensor/":"number/");
    append(&t,identity); append(&t,"/"); append(&t,keys[index]); append(&t,"/config");
    Text b={scratch,sizeof(scratch),0,false}; scratch[0]=0;
    append(&b,"{\"name\":\""); append(&b,keys[index]); append(&b,"\",\"unique_id\":\""); append(&b,identity); append(&b,"_"); append(&b,keys[index]);
    append(&b,"\",\"device\":{\"identifiers\":[\""); append(&b,identity); append(&b,"\"],\"name\":\"LightVM MQTT experiment\",\"manufacturer\":\"Kajlights\"},\"availability_topic\":\"");
    append(&b,availability); append(&b,"\",\"state_topic\":\""); append(&b,base); append(&b,index?"/diagnostics":"/speed/state"); append(&b,"\"");
    if (!index) { append(&b,",\"command_topic\":\""); append(&b,command_topic); append(&b,"\",\"min\":-10,\"max\":10,\"step\":0.01,\"mode\":\"box\""); }
    else { append(&b,",\"entity_category\":\"diagnostic\",\"value_template\":\"{{ value_json."); append(&b,keys[index]); append(&b," }}\""); if (index==1) append(&b,",\"unit_of_measurement\":\"B\""); if (index==2) append(&b,",\"unit_of_measurement\":\"µs\""); }
    append(&b,"}"); return !t.failed && !b.failed && queue_publish(topic,scratch,MQTTQoS1,true);
}
// Strict, bounded numeric commands; JSON envelope optionally carries concurrency tokens.
static bool number(const uint8_t *p,size_t n,double *value) {
    if (!n || n>40) return false;
    size_t i=0; bool negative=p[0]=='-'; if (negative) ++i;
    if (i==n || p[i]<'0' || p[i]>'9') return false;
    double v=0; size_t digits=0;
    while (i<n && p[i]>='0' && p[i]<='9') { v=v*10+p[i++]-'0'; if (++digits>10) return false; }
    if (digits>1 && p[negative?1:0]=='0') return false;
    if (i<n && p[i]=='.') {
        ++i; size_t start=i; double scale=0.1;
        while (i<n && p[i]>='0' && p[i]<='9') { v+=(p[i++]-'0')*scale; scale*=0.1; }
        if (i==start) return false;
    }
    if (i!=n) return false;
    *value=negative?-v:v; return true;
}
static void whitespace(const uint8_t *p,size_t n,size_t *i) { while (*i<n && (p[*i]==' ' || p[*i]=='\t' || p[*i]=='\r' || p[*i]=='\n')) ++*i; }
static bool command(const uint8_t *p,size_t n,double *speed,uint32_t *gen,uint32_t *rev) {
    *gen=animation.generation; *rev=animation.revision;
    if (n && p[0]!='{') return number(p,n,speed);
    size_t i=1; unsigned mask=0;
    while (i<n) {
        whitespace(p,n,&i); if (i>=n || p[i++]!='"') return false;
        size_t start=i; while (i<n && p[i]!='"') ++i; if (i==n) return false;
        size_t length=i-start; unsigned key=length==5 && !memcmp(p+start,"speed",5)?1:length==10 && !memcmp(p+start,"generation",10)?2:length==8 && !memcmp(p+start,"revision",8)?4:0;
        if (!key || (mask&key)) return false;
        mask|=key; ++i; whitespace(p,n,&i); if (i==n || p[i++]!=':') return false;
        whitespace(p,n,&i); start=i; while (i<n && p[i]!=',' && p[i]!='}' && p[i]!=' ' && p[i]!='\t' && p[i]!='\r' && p[i]!='\n') ++i;
        double v; if (!number(p+start,i-start,&v)) return false;
        if (key==1) *speed=v;
        else { if (v<0 || v>4294967295.0 || (double)(uint32_t)v!=v) return false; if (key==2) *gen=(uint32_t)v; else *rev=(uint32_t)v; }
        whitespace(p,n,&i); if (i==n) return false;
        uint8_t separator=p[i++];
        if (separator=='}') { whitespace(p,n,&i); return i==n && (mask==1 || mask==7); }
        if (separator!=',') return false;
    }
    return false;
}
static int process_packet(MQTTPacketInfo_t *packet) {
    uint16_t id=0; bool session=false; uint8_t type=packet->type&0xf0u;
    if (type==MQTT_PACKET_TYPE_PUBLISH) {
        MQTTPublishInfo_t p;
        if (state!=4 || MQTT_DeserializePublish(packet,&id,&p)!=MQTTSuccess || p.qos>MQTTQoS1) return -1;
        bool duplicate=p.qos==MQTTQoS1 && p.dup && (seen_ids[id>>3]&(1u<<(id&7)));
        if (duplicate) ++duplicate_commands;
        else if (p.topicNameLength==strlen(command_topic) && !memcmp(p.pTopicName,command_topic,p.topicNameLength)) {
            double speed; uint32_t generation,revision;
            if (!command(p.pPayload,p.payloadLength,&speed,&generation,&revision)) ++malformed;
            else { (void)speed_set(speed,generation,revision); state_dirty=true; }
        } else if (p.topicNameLength==20 && !memcmp(p.pTopicName,"homeassistant/status",20) && p.payloadLength==6 && !memcmp(p.pPayload,"online",6)) discovery_phase=0;
        if (p.qos==MQTTQoS1) {
            seen_ids[id>>3]|=(uint8_t)(1u<<(id&7));
            MQTTFixedBuffer_t b={tx,LIMIT}; if (MQTT_SerializeAck(&b,MQTT_PACKET_TYPE_PUBACK,id)!=MQTTSuccess) return -1;
            tx_size=4; tx_offset=0;
        }
        return 1; // One publication/parameter transaction per cooperative step.
    }
    if (MQTT_DeserializeAck(packet,&id,&session)!=MQTTSuccess) return -1;
    if (type==MQTT_PACKET_TYPE_CONNACK && state==2 && !session) { state=3; return 0; }
    if (type==MQTT_PACKET_TYPE_SUBACK && state==3 && id==subscription_id && packet->remainingLength==4 && packet->pRemainingData[2]<=1 && packet->pRemainingData[3]<=1) { state=4; discovery_phase=0; diagnostic_at=0; state_dirty=true; return 0; }
    if (type==MQTT_PACKET_TYPE_PUBACK && state==4) {
        if (pending_id && id==pending_id) { pending_id=0; outstanding_size=0; }
        return 0;
    }
    if (type==MQTT_PACKET_TYPE_PINGRESP && state==4 && ping_pending) { ping_pending=false; return 0; }
    return -1;
}
EXPORT("service_init") void service_init(uint32_t ip,uint32_t tcp_port) {
    if (handle) net_close(handle);
    address=ip; port=tcp_port; handle=state=0; sequence=pending_id=subscription_id=0;
    rx_size=tx_size=tx_offset=outstanding_size=0; retry_at=0; ping_pending=false;
    device_identity(identity,sizeof(identity));
    Text b={base,sizeof(base),0,false}; base[0]=0; append(&b,"lightvm/"); append(&b,identity);
    make_topic(command_topic,sizeof(command_topic),"/speed/set"); make_topic(availability,sizeof(availability),"/availability");
    memset(seen_ids,0,sizeof(seen_ids));
}
EXPORT("service_step") int32_t service_step(void) {
    uint64_t now=now_ms(); animation_state(&animation,sizeof(animation));
    if (!handle) {
        if (now<retry_at) return 0;
        int32_t h=net_connect(address,port); if (h<0) { retry_at=now+1000; return 0; }
        handle=(uint32_t)h; state=1; progress_at=now; activity=now;
    }
    int32_t connected=net_state(handle);
    if (connected<0 || (state<4 && now-progress_at>5000)) { disconnect(); return 0; }
    if (!connected) return 1;
    if (state==1) { if (!connect_packet()) { disconnect(); return 0; } state=2; }
    if (tx_size) {
        int32_t n=net_write(handle,tx+tx_offset,(uint32_t)(tx_size-tx_offset));
        if (n<0) { disconnect(); return 0; }
        tx_offset+=(size_t)n;
        if (n) activity=now;
        if (tx_offset==tx_size) tx_size=tx_offset=0;
    }
    if (rx_size<LIMIT) {
        int32_t n=net_read(handle,rx+rx_size,(uint32_t)(LIMIT-rx_size));
        if (n<0) { disconnect(); return 0; }
        rx_size+=(size_t)n;
    }
    for (unsigned work=0;work<4 && rx_size && !tx_size;++work) {
        // Inspect fragmentation before coreMQTT's complete-header deserializer.
        size_t header=1; uint32_t remaining=0,multiplier=1; bool complete=false;
        while (header<rx_size && header<=4) {
            uint8_t c=rx[header++]; remaining+=(c&127u)*multiplier; multiplier*=128;
            if (!(c&128u)) { complete=true; break; }
        }
        if (!complete) { if (header==5) { ++malformed; disconnect(); } break; }
        if (remaining>LIMIT-header) { ++malformed; disconnect(); return 0; }
        MQTTPacketInfo_t packet={0};
        if (MQTT_ProcessIncomingPacketTypeAndLength(rx,&rx_size,&packet)!=MQTTSuccess || packet.headerLength!=header) { ++malformed; disconnect(); return 0; }
        if (rx_size<header+remaining) break;
        packet.pRemainingData=rx+header;
        int result=process_packet(&packet);
        if (result<0) { ++malformed; disconnect(); return 0; }
        size_t consumed=header+remaining; rx_size-=consumed; memmove(rx,rx+consumed,rx_size);
        if (result>0) break;
    }
    if (!handle) return 0;
    if (state==3 && !subscription_id && !tx_size) { if (!subscribe()) { disconnect(); return 0; } }
    if (state!=4) return (int32_t)state;
    if (ping_pending && now-ping_at>KEEPALIVE*1000u) { disconnect(); return 0; }
    if (tx_size) return 4;
    if (!ping_pending && now-activity>=KEEPALIVE*500u) {
        MQTTFixedBuffer_t b={tx,LIMIT}; MQTT_SerializePingreq(&b); tx_size=2; tx_offset=0; ping_pending=true; ping_at=now; return 4;
    }
    if (pending_id) {
        if (now-pending_at>2000) { memcpy(tx,outstanding,outstanding_size); tx[0]|=8; tx_size=outstanding_size; tx_offset=0; pending_at=now; }
        return 4;
    }
    if (discovery_phase<4) { if (discovery(discovery_phase)) ++discovery_phase; return 4; }
    if (discovery_phase==4) { if (queue_publish(availability,"online",MQTTQoS1,true)) ++discovery_phase; return 4; }
    if (state_dirty) {
        make_topic(topic,sizeof(topic),"/speed/state"); Text b={scratch,sizeof(scratch),0,false}; scratch[0]=0; decimal(&b,animation.speed);
        if (queue_publish(topic,scratch,MQTTQoS1,true)) state_dirty=false;
        return 4;
    }
    if (now>=diagnostic_at) {
        make_topic(topic,sizeof(topic),"/diagnostics"); Text b={scratch,sizeof(scratch),0,false}; scratch[0]=0;
        append(&b,"{\"vm_heap\":"); integer(&b,animation.heap); append(&b,",\"render_us\":"); integer(&b,animation.render_us);
        append(&b,",\"commands\":"); integer(&b,animation.commands); append(&b,",\"rejected\":"); integer(&b,animation.rejected);
        append(&b,",\"malformed\":"); integer(&b,malformed); append(&b,",\"duplicates\":"); integer(&b,duplicate_commands);
        append(&b,",\"reconnects\":"); integer(&b,reconnects); append(&b,"}");
        if (queue_publish(topic,scratch,MQTTQoS0,false)) diagnostic_at=now+1000;
    }
    return 4;
}
