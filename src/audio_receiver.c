#include "audio_receiver.h"
#include "bytecode_core.h"
#include "config.h"
#include "vm_allocator.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint64_t get(const uint8_t *p,unsigned n){uint64_t value=0;while(n--)value=(value<<8)|*p++;return value;}
static float real(const uint8_t *p){uint32_t bits=(uint32_t)get(p,4);float value;memcpy(&value,&bits,4);return value;}
static bool unit(float x){return isfinite(x)&&x>=0&&x<=1;}
bool audio_decode(const uint8_t *p,size_t size,AudioFrame *f){
    if(size<84||size>AUDIO_PACKET_LIMIT||memcmp(p,"LVAF",4)||p[4]!=4||p[5]!=6||p[6]>4||(p[7]&~1u)||
       get(p+48,4)!=48000||get(p+52,2)!=1024||get(p+54,2)!=2048||p[80]>64||p[81]!=1||p[82]||p[83]||
       size!=84u+8u*p[80]+24u*p[6])return false;
    memset(f,0,sizeof(*f));
    f->sensor=get(p+8,8);f->session=get(p+16,8);f->sequence=get(p+24,8);f->audio=get(p+32,8);f->send=get(p+40,8);
    f->flags=p[7];f->bands=p[80];f->event_count=p[6];
    if(!f->sensor||!f->session||!f->sequence||!f->audio||f->send<f->audio)return false;
    for(unsigned i=0;i<6;i++){f->values[i]=real(p+56+4*i);if(!unit(f->values[i]))return false;}
    for(unsigned i=0;i<f->bands;i++){
        const uint8_t *b=p+84+8*i;uint16_t start=get(b,2),end=get(b+2,2);
        if(start!=(i?f->edges[i]:2)||end<=start||end>512||!unit(real(b+4)))return false;
        f->edges[i]=start;f->edges[i+1]=end;
        if(i<AUDIO_SPECTRUM_INPUTS)f->spectrum[i]=real(b+4);
    }
    if(f->bands&&f->edges[f->bands]!=512)return false;
    for(unsigned i=0;i<f->event_count;i++){
        const uint8_t *e=p+84+8*f->bands+24*i;AudioEvent *event=&f->events[i];
        event->id=get(e,8);event->time=get(e+8,8);event->strength=real(e+20);
        if(!event->id||!event->time||event->time>f->audio||f->send-event->time>AUDIO_EVENT_TTL_US||
           e[16]!=1||e[17]||e[18]||e[19]||!unit(event->strength)||!event->strength||
           (i&&(event->id<=f->events[i-1].id||event->time<f->events[i-1].time)))return false;
    }
    return true;
}
bool audio_config_parse(const Json *j,int token,AudioReceiver **out,char *error,size_t capacity){
    *out=NULL;if(token<0)return true;
    static const char *const fields[]={"sensor_id","group","port","allow_synthetic",NULL};
    AudioConfig config={.group={239,255,42,99},.port=49152};char sensor[17],group[16];
    const char *failure="audio: expected a nonzero 16-digit hexadecimal sensor_id, 239.x.x.x group and port 1024..65535";
    if(!LIGHTVM_AUDIO_MULTICAST){failure="audio multicast is unavailable in this firmware";goto failed;}
    if(!json_fields(j,token,fields)||!json_string(j,json_get(j,token,"sensor_id"),sensor,sizeof(sensor))||strlen(sensor)!=16)goto failed;
    for(unsigned i=0;i<16;i++){
        unsigned digit=sensor[i]>='0'&&sensor[i]<='9'?(unsigned)(sensor[i]-'0'):
            sensor[i]>='a'&&sensor[i]<='f'?(unsigned)(sensor[i]-'a'+10):
            sensor[i]>='A'&&sensor[i]<='F'?(unsigned)(sensor[i]-'A'+10):16u;
        if(digit>15)goto failed;
        config.sensor=(config.sensor<<4)|digit;
    }
    if(!config.sensor)goto failed;
    int t=json_get(j,token,"group");
    if(t>=0){
        if(!json_string(j,t,group,sizeof(group)))goto failed;
        const char *s=group;
        for(unsigned i=0;i<4;i++){
            unsigned v=0,n=0;
            while(*s>='0'&&*s<='9'){v=v*10+(unsigned)(*s++-'0');if(++n>3||v>255)goto failed;}
            if(!n||(i<3?*s++!='.':*s!=0))goto failed;
            config.group[i]=(uint8_t)v;
        }
        if(config.group[0]!=239)goto failed;
    }
    t=json_get(j,token,"port");uint32_t port;
    if(t>=0){if(!json_u32(j,t,&port)||port<1024||port>65535)goto failed;config.port=(uint16_t)port;}
    t=json_get(j,token,"allow_synthetic");if(t>=0&&!json_bool(j,t,&config.allow_synthetic))goto failed;
    *out=demo_calloc(1,sizeof(**out));
    if(!*out){failure="audio receiver exceeds memory budget";goto failed;}
    (*out)->config=config;return true;
failed:
    snprintf(error,capacity,"%s",failure);return false;
}
static const char *const names[]={"audio.level","audio.bass","audio.mid","audio.treble","audio.centroid","audio.onset","audio.onsets",
    "audio.band00","audio.band01","audio.band02","audio.band03","audio.band04","audio.band05","audio.band06","audio.band07",
    "audio.band08","audio.band09","audio.band10","audio.band11","audio.band12","audio.band13","audio.band14","audio.band15"};
const char *audio_receiver_claim(AudioReceiver *r,InputRegistry *registry){
    static const unsigned types[]={T_FLOAT,T_FLOAT,T_FLOAT,T_FLOAT,T_FLOAT,T_FLOAT,T_INT,
        T_FLOAT,T_FLOAT,T_FLOAT,T_FLOAT,T_FLOAT,T_FLOAT,T_FLOAT,T_FLOAT,
        T_FLOAT,T_FLOAT,T_FLOAT,T_FLOAT,T_FLOAT,T_FLOAT,T_FLOAT,T_FLOAT};
    return input_registry_claim_typed(registry,names,types,AUDIO_SOURCES,&r->generation);
}
void audio_receiver_disconnect(AudioReceiver *r){if(r){r->listening=false;r->pending=false;}}
void audio_receiver_receive(AudioReceiver *r,const uint8_t *data,size_t size,uint64_t now){
    if(!r)return;
    AudioFrame f;if(!audio_decode(data,size,&f)){r->invalid++;return;}
    if(f.sensor!=r->config.sensor||(f.flags&&!r->config.allow_synthetic)){r->ignored++;return;}
    if(f.session!=r->session){
        for(unsigned i=0;i<4;i++)if(f.session==r->retired[i]){r->ignored++;return;}
        if(r->session&&now-r->received<AUDIO_TIMEOUT_US){r->ignored++;return;}
        if(r->session)r->retired[r->retire_next++%4]=r->session;
        r->session=f.session;r->sequence=0;r->event_high=0;r->event_seen=0;
        r->flags=f.flags;r->bands=f.bands;memcpy(r->edges,f.edges,sizeof(r->edges));r->pending=false;
    }
    if(f.flags!=r->flags||f.bands!=r->bands||memcmp(f.edges,r->edges,sizeof(r->edges))){r->invalid++;return;}
    bool newer=f.sequence>r->sequence;
    if(newer&&r->sequence&&(f.audio<=r->audio||f.send<=r->send)){r->invalid++;return;}
    if(!newer&&(f.audio>r->audio||f.send>r->send)){r->invalid++;return;}
    r->packets++;
    if(newer){
        if(r->sequence&&f.sequence-r->sequence>1){uint64_t gap=f.sequence-r->sequence-1;r->gaps+=gap>UINT32_MAX?UINT32_MAX:(uint32_t)gap;}
        if(r->pending)r->coalesced++;
        r->sequence=f.sequence;r->audio=f.audio;r->send=f.send;r->received=now;
        for(unsigned i=0;i<6;i++)r->values[i].f=f.values[i];
        for(unsigned i=0;i<AUDIO_SPECTRUM_INPUTS;i++)r->values[7+i].f=f.spectrum[i];
        r->pending=true;
    }else r->reordered++;
    for(unsigned i=0;i<f.event_count;i++){
        AudioEvent e=f.events[i];
        if(r->audio-e.time>AUDIO_EVENT_TTL_US||now-r->received>AUDIO_TIMEOUT_US){r->expired++;continue;}
        if(e.id>r->event_high){uint64_t shift=e.id-r->event_high;r->event_seen=shift>=64?0:r->event_seen<<shift;r->event_high=e.id;}
        uint64_t behind=r->event_high-e.id;
        if(behind>=64||(r->event_seen&(1ull<<behind))){r->duplicates++;continue;}
        r->event_seen|=1ull<<behind;r->onsets++;
        r->values[6].i=r->values[6].i==INT32_MAX?1:r->values[6].i+1;r->pending=true;
    }
}
void audio_receiver_publish(AudioReceiver *r,InputRegistry *registry,uint64_t now){
    if(!r||!r->pending||r->conflict)return;
    r->pending=false;
    if(now-r->received>AUDIO_TIMEOUT_US){r->expired++;return;}
    if(r->publication==UINT32_MAX){r->conflict=true;input_registry_release(registry,r->generation);return;}
    const char *error=input_registry_publish_typed(registry,r->generation,++r->publication,names,r->values,AUDIO_SOURCES,r->received);
    if(error){r->conflict=true;return;}
    r->published++;uint64_t delay=now-r->received;
    if(delay>r->max_service_us)r->max_service_us=delay>UINT32_MAX?UINT32_MAX:(uint32_t)delay;
}
