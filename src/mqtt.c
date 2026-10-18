#include "mqtt.h"
#include <stdio.h>
#include <string.h>

static bool queue(Mqtt *m, uint8_t header, const void *body, size_t size) {
    uint8_t length[4]; size_t n=0, remaining=size;
    do { length[n]=(uint8_t)(remaining%128); remaining/=128; if(remaining)length[n]|=128; n++; } while(remaining && n<4);
    if(remaining || 1+n+size>sizeof(m->tx)-m->queued)return false;
    m->tx[m->queued++]=header;
    memcpy(m->tx+m->queued,length,n);m->queued+=n;
    if(size)memcpy(m->tx+m->queued,body,size);
    m->queued+=size;return true;
}
static size_t string(uint8_t *out,const char *s) {
    size_t n=strlen(s);out[0]=(uint8_t)(n>>8);out[1]=(uint8_t)n;memcpy(out+2,s,n);return n+2;
}
static void down(Mqtt *m,const char *error,uint64_t now) {
    m->stream.close(m->stream.context);m->state=MQTT_DOWN;
    m->received=m->queued=m->sent=0;m->subscription=0;m->ping_deadline=0;
    snprintf(m->error,sizeof(m->error),"%s",error);
    unsigned shift=m->failures<5?m->failures:5;
    unsigned seconds=1u<<shift;if(seconds>30)seconds=30;
    m->retry_at=now+(uint64_t)seconds*1000000u;if(m->failures<5)m->failures++;
}
void mqtt_init(Mqtt *m,MqttStream stream,const char *id,const char *will,MqttMessage message,void *context) {
    memset(m,0,sizeof(*m));m->stream=stream;m->message=message;m->context=context;
    snprintf(m->client_id,sizeof(m->client_id),"%s",id);snprintf(m->will_topic,sizeof(m->will_topic),"%s",will);
}
void mqtt_close(Mqtt *m) {
    m->stream.close(m->stream.context);m->state=MQTT_DOWN;
    m->received=m->queued=m->sent=0;m->subscription=0;m->ping_deadline=0;
}
static bool connect_packet(Mqtt *m) {
    uint8_t body[512]={0,4,'M','Q','T','T',4,0x26,0,30}; /* clean session; retained QoS0 will */
    if(m->config.username[0])body[7]|=0x80;
    if(m->config.password[0])body[7]|=0x40;
    size_t n=10;n+=string(body+n,m->client_id);n+=string(body+n,m->will_topic);n+=string(body+n,"offline");
    if(m->config.username[0])n+=string(body+n,m->config.username);
    if(m->config.password[0])n+=string(body+n,m->config.password);
    return queue(m,0x10,body,n);
}
bool mqtt_publish(Mqtt *m,const char *topic,const void *payload,size_t size,bool retained) {
    size_t t=strlen(topic);
    if(m->state!=MQTT_ONLINE || !t || t>=MQTT_TOPIC_SIZE || size>MQTT_BUFFER_SIZE-8-t)return false;
    /* Construct directly in the unused tail: framing is at most three bytes. */
    size_t body=t+2+size,header=body>=128?3:2;
    if(header+body>sizeof(m->tx)-m->queued)return false;
    uint8_t *p=m->tx+m->queued;p[0]=retained?0x31:0x30;
    p[1]=(uint8_t)(body%128);if(body>=128){p[1]|=128;p[2]=(uint8_t)(body/128);}
    string(p+header,topic);if(size)memcpy(p+header+2+t,payload,size);
    m->queued+=header+body;return true;
}
bool mqtt_subscribe(Mqtt *m,const char *filter) {
    if(m->state!=MQTT_ONLINE || m->subscription || strlen(filter)>=MQTT_TOPIC_SIZE)return false;
    uint8_t body[MQTT_TOPIC_SIZE+5];uint16_t id=(uint16_t)(m->sequence+1);if(!id)id=1;
    body[0]=(uint8_t)(id>>8);body[1]=(uint8_t)id;
    size_t n=2+string(body+2,filter);body[n++]=0; /* request QoS0 */
    if(!queue(m,0x82,body,n))return false;
    m->sequence=id;m->subscription=id;return true;
}
/* 1=consumed, 0=wait for transmit capacity, -1=protocol error. */
static int packet(Mqtt *m,uint8_t h,const uint8_t *p,size_t n,uint64_t now) {
    if(m->state==MQTT_CONNACK){
        if(h!=0x20 || n!=2 || p[0] || p[1])return -1;
        m->state=MQTT_ONLINE;m->generation++;m->failures=0;m->error[0]=0;return 1;
    }
    if(m->state!=MQTT_ONLINE)return -1;
    if(h==0x90){
        if(n!=3 || !m->subscription || ((unsigned)p[0]<<8|p[1])!=m->subscription || p[2]!=0)return -1;
        m->subscription=0;return 1;
    }
    if(h==0xd0){if(n || !m->ping_deadline)return -1;m->ping_deadline=0;return 1;}
    if((h>>4)==3){
        unsigned qos=(h>>1)&3;
        if(n<2 || qos>1 || (!qos && (h&8)))return -1;
        size_t t=(size_t)p[0]<<8|p[1],offset=2+t;
        if(!t || t>=MQTT_TOPIC_SIZE || offset+(qos?2:0)>n)return -1;
        char topic[MQTT_TOPIC_SIZE];memcpy(topic,p+2,t);topic[t]=0;
        if(memchr(topic,0,t) || strchr(topic,'+') || strchr(topic,'#'))return -1;
        if(qos){
            if(!p[offset]&&!p[offset+1])return -1;
            if(!queue(m,0x40,p+offset,2))return 0;
            offset+=2;
        }
        if(m->message)m->message(m->context,topic,p+offset,n-offset,(h&1)!=0);
        return 1;
    }
    (void)now;return -1;
}
static bool receive(Mqtt *m,uint64_t now) {
    /* Bound work even when a peer continuously sends small packets. */
    for(unsigned budget=0;budget<8;budget++){
        size_t n=0,multiplier=1,header=1;bool complete=false;
        while(header<m->received && header<=4){
            uint8_t b=m->rx[header++];n+=(b&127)*multiplier;multiplier*=128;
            if(!(b&128)){complete=true;break;}
        }
        if(!complete){if(header==5)return false;break;}
        if(n>sizeof(m->rx)-header)return false;
        if(header+n>m->received)break;
        int result=packet(m,m->rx[0],m->rx+header,n,now);
        if(result<0)return false;
        if(!result)break;
        m->received-=header+n;memmove(m->rx,m->rx+header+n,m->received);
    }
    return true;
}
void mqtt_tick(Mqtt *m,const MqttConfig *config,bool ready,uint64_t now) {
    if(m->config.enabled!=config->enabled || m->config.port!=config->port ||
       strcmp(m->config.host,config->host) || strcmp(m->config.username,config->username) ||
       strcmp(m->config.password,config->password)){
        mqtt_close(m);m->config=*config;m->retry_at=0;m->failures=0;
    }
    if(!config->enabled || !ready){mqtt_close(m);m->retry_at=0;return;}
    if(m->state==MQTT_DOWN){
        if(now<m->retry_at)return;
        if(!m->stream.open(m->stream.context,config->host,config->port)){down(m,"MQTT TCP connection failed",now);return;}
        m->state=MQTT_TCP;m->deadline=now+10000000u;m->tx_progress=now;
    }
    if(m->state==MQTT_TCP){
        int connected=m->stream.poll(m->stream.context);
        if(connected<0 || now>=m->deadline){down(m,"MQTT TCP connection failed or timed out",now);return;}
        if(!connected)return;
        if(!connect_packet(m)){down(m,"MQTT CONNECT exceeds buffer",now);return;}
        m->state=MQTT_CONNACK;m->deadline=now+10000000u;m->last_sent=now;
    }
    if(m->received<sizeof(m->rx)){
        int n=m->stream.read(m->stream.context,m->rx+m->received,sizeof(m->rx)-m->received);
        if(n<0){down(m,"MQTT broker disconnected",now);return;}
        m->received+=(size_t)n;
    }
    if(!receive(m,now)){down(m,"MQTT broker rejected connection/subscription or sent an invalid packet",now);return;}
    if((m->state==MQTT_CONNACK && now>=m->deadline) || (m->subscription && now>=m->deadline) || (m->ping_deadline && now>=m->ping_deadline)){
        down(m,"MQTT reply timed out",now);return;
    }
    if(m->state==MQTT_ONLINE && !m->ping_deadline && now-m->last_sent>=15000000u){
        if(queue(m,0xc0,NULL,0))m->ping_deadline=now+10000000u;
    }
    if(m->queued>m->sent){
        int n=m->stream.write(m->stream.context,m->tx+m->sent,m->queued-m->sent);
        if(n<0){down(m,"MQTT write failed",now);return;}
        if(n){m->sent+=(size_t)n;m->tx_progress=now;m->last_sent=now;}
        else if(now-m->tx_progress>=5000000u){down(m,"MQTT write stalled",now);return;}
        if(m->sent==m->queued){m->sent=m->queued=0;m->tx_progress=now;}
    }else m->tx_progress=now;
    /* Subscription timeout starts after the SUBSCRIBE is queued/flushed. */
    if(!m->subscription && m->state==MQTT_ONLINE)m->deadline=now+10000000u;
}
