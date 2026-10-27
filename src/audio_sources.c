#include "audio_sources.h"
#include "bytecode_core.h"
#include "outputs.h"
#include "vm_allocator.h"
#include "platform.h"
#include "device/protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

uint64_t audio_source_random(void){
    uint64_t value=0;do{if(!platform_random_bytes(&value,sizeof(value)))return 0;}while(!value);return value;
}
bool audio_source_id(const Json *j,int token,uint64_t *out){
    char text[17];if(!json_string(j,token,text,sizeof(text))||strlen(text)!=16)return false;
    uint64_t value=0;
    for(unsigned i=0;i<16;i++){
        unsigned c=(unsigned char)text[i],digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:16;
        if(digit>15)return false;
        value=(value<<4)|digit;
    }
    *out=value;return value!=0;
}
static const char *address(char *text){
    size_t n=strlen(text);if(!n)return "source address is required";
    for(size_t i=0;i<n;i++)text[i]=(char)tolower((unsigned char)text[i]);
    if(text[n-1]=='.')text[--n]=0;
    if(n>=6&&!strcmp(text+n-6,".local"))return ".local resolution is unsupported; use router DNS or an IPv4 address";
    bool numeric=true;for(size_t i=0;i<n;i++)if(text[i]!='.'&&(text[i]<'0'||text[i]>'9'))numeric=false;
    if(numeric){
        const char *p=text;unsigned octets[4];
        for(unsigned i=0;i<4;i++){
            unsigned value=0,digits=0;
            while(*p>='0'&&*p<='9'){value=value*10+(unsigned)(*p++-'0');if(++digits>3||value>255)return "invalid source IPv4 address";}
            if(!digits||(i<3?*p++!='.':*p!=0))return "invalid source IPv4 address";
            octets[i]=value;
        }
        if(!octets[0]||octets[0]>=224)return "source address must be IPv4 unicast";
        snprintf(text,128,"%u.%u.%u.%u",octets[0],octets[1],octets[2],octets[3]);return NULL;
    }
    unsigned label=0;
    for(size_t i=0;i<n;i++){
        unsigned char c=(unsigned char)text[i];
        if(c=='.'){if(!label||text[i-1]=='-')return "invalid source DNS hostname";label=0;}
        else{if(!(c>='a'&&c<='z')&&!(c>='0'&&c<='9')&&c!='-')return "invalid source DNS hostname";
            if((!label&&c=='-')||++label>63)return "invalid source DNS hostname";}
    }
    return !label||text[n-1]=='-'?"invalid source DNS hostname":NULL;
}
const char *input_scope_parse(AudioSources *manager,const Json *j,int token,InputSourceScope **out){
    *out=NULL;if(token<0)return NULL;
    if(j->tokens[token].type!=JSON_OBJECT||j->tokens[token].children>AUDIO_PROVIDER_LIMIT)return "sources must declare at most four aliases";
    unsigned count=(unsigned)j->tokens[token].children;
    InputSourceScope *scope=demo_calloc(1,sizeof(*scope)+count*sizeof(InputSourceDeclaration));
    if(!scope)return "source declarations exceed memory budget";
    scope->manager=manager;scope->references=1;scope->count=count;
    const char *error="invalid source declaration";unsigned index=0;
    for(int t=token+1;t<j->tokens[token].next;t=j->tokens[t+1].next){
        InputSourceDeclaration *d=&scope->declarations[index++];AudioSourceConfig *c=&d->config;
        if(!json_string(j,t,d->alias,sizeof(d->alias))||!object_name_valid(d->alias)||strchr(d->alias,'.')||!strcmp(d->alias,"board")||!strcmp(d->alias,"light"))goto fail;
        for(unsigned k=0;k+1<index;k++)if(!strcmp(d->alias,scope->declarations[k].alias))goto fail;
        int obj=t+1;static const char *const udp[]={"type","address","port","source_id","analysis_profile","allow_synthetic",NULL};
        static const char *const gateway[]={"type","name","source_id","analysis_profile","allow_synthetic",NULL};
        c->type=json_equal(j,json_get(j,obj,"type"),"lvaf")?AUDIO_UDP:json_equal(j,json_get(j,obj,"type"),"gateway")?AUDIO_GATEWAY:0;
        if(!c->type||!json_fields(j,obj,c->type==AUDIO_UDP?udp:gateway))goto fail;
        if(!json_string(j,json_get(j,obj,c->type==AUDIO_UDP?"address":"name"),c->address,sizeof(c->address)))goto fail;
        if(c->type==AUDIO_UDP){const char *e=address(c->address);if(e){error=e;goto fail;}}
        else if(!object_name_valid(c->address))goto fail;
        c->port=c->type==AUDIO_UDP?49152:0;c->profile=1;
        int p=json_get(j,obj,"port");uint32_t port;
        if(p>=0){if(!json_u32(j,p,&port)||port<1024||port>65535)goto fail;c->port=(uint16_t)port;}
        p=json_get(j,obj,"analysis_profile");if(p>=0&&(!json_u32(j,p,&c->profile)||(c->profile!=1&&c->profile!=2))){error="unsupported analysis profile";goto fail;}
        p=json_get(j,obj,"source_id");if(p>=0&&!audio_source_id(j,p,&c->pin))goto fail;
        p=json_get(j,obj,"allow_synthetic");if(p>=0&&!json_bool(j,p,&c->synthetic))goto fail;
    }
    *out=scope;return NULL;
fail:demo_free(scope);return error;
}
static bool same(const AudioSourceConfig *a,const AudioSourceConfig *b){
    if(a->type!=b->type||strcmp(a->address,b->address))return false;
    return a->port==b->port&&a->pin==b->pin&&a->profile==b->profile&&a->synthetic==b->synthetic;
}
static void remove_provider(AudioSources *m,AudioProvider *p){
    if(p->config.type==AUDIO_UDP){
        KfControl close;uint8_t bytes[KF_CONTROL_SIZE];
        if(kf_subscriber_close(&p->receiver,&close)&&m->transport.send){size_t n=kf_control_encode(&close,bytes,sizeof(bytes));if(n)m->transport.send(m->transport.context,p,bytes,n);}
        if(m->transport.close)m->transport.close(m->transport.context,p);
        vm_heap_uncharge(m->transport.charged_bytes);
    }
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++)if(m->providers[i]==p)m->providers[i]=NULL;
    AudioGateway *g=p->gateway;
    bool shared=false;
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++)if(m->providers[i]&&m->providers[i]->gateway==g)shared=true;
    if(g&&!shared)demo_free(g);
    demo_free(p);
}
static void drop(AudioSources *m,AudioProvider *p){
    if(!p||--p->references)return;
    if(p->gateway&&p->gateway->owner){
        /* An independently owned claim retains one provider when unobserved. */
        bool shared=false;
        for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++)if(m->providers[i]&&m->providers[i]!=p&&m->providers[i]->gateway==p->gateway)shared=true;
        if(!shared)return;
    }
    remove_provider(m,p);
}
const char *input_scope_demand(InputSourceScope *s,bool active){
    if(!s)return NULL;
    if(!active){
        if(s->demand&&!--s->demand)for(unsigned i=0;i<s->count;i++){drop(s->manager,s->declarations[i].provider);s->declarations[i].provider=NULL;}
        return NULL;
    }
    if(s->demand){s->demand++;return NULL;}
    AudioSources *m=s->manager;const char *error="structured audio provider capacity exhausted";
    unsigned acquired=0;
    for(;acquired<s->count;acquired++){
        InputSourceDeclaration *d=&s->declarations[acquired];AudioProvider *p=NULL;unsigned empty=AUDIO_PROVIDER_LIMIT;
        for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++){
            if(m->providers[i]&&same(&m->providers[i]->config,&d->config)){p=m->providers[i];break;}
            if(!m->providers[i])empty=i;
        }
        AudioProvider *anchor=NULL;
        if(!p&&d->config.type==AUDIO_GATEWAY)for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++){
            AudioProvider *candidate=m->providers[i];
            if(candidate&&candidate->gateway&&!candidate->references&&!strcmp(candidate->config.address,d->config.address)){
                anchor=candidate;p=candidate;break;
            }
        }
        if(!p){
            if(empty==AUDIO_PROVIDER_LIMIT)goto fail;
            p=demo_calloc(1,sizeof(*p));if(!p){error="audio provider exceeds memory budget";goto fail;}
            if(d->config.type==AUDIO_UDP&&!vm_heap_charge(m->transport.charged_bytes)){demo_free(p);error="audio transport exceeds memory budget";goto fail;}
            p->config=d->config;kf_subscriber_init(&p->receiver,p->config.pin,p->config.profile,p->config.synthetic);
            if(d->config.type==AUDIO_GATEWAY)for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++){
                AudioProvider *other=m->providers[i];
                if(other&&other->gateway&&!strcmp(other->config.address,d->config.address)){p->gateway=other->gateway;break;}
            }
            m->providers[empty]=p;
        }
        if(anchor)anchor->config=d->config;
        p->references++;d->provider=p;
    }
    s->demand=1;return NULL;
fail:
    while(acquired){InputSourceDeclaration *d=&s->declarations[--acquired];drop(m,d->provider);d->provider=NULL;}
    return error;
}
void input_scope_retain(InputSourceScope *s){if(s)s->references++;}
void input_scope_release(InputSourceScope *s){
    if(!s||--s->references)return;
    if(s->demand){s->demand=1;input_scope_demand(s,false);}demo_free(s);
}
void input_scope_describe(const InputSourceScope *s,void (*write)(void *,const char *),void *ctx){
    write(ctx,"{");
    if(s)for(unsigned i=0;i<s->count;i++){
        const InputSourceDeclaration *d=&s->declarations[i];const AudioSourceConfig *c=&d->config;char b[256];
        snprintf(b,sizeof(b),"%s\"%s\":{\"type\":\"%s\",\"%s\":\"%s\",\"analysis_profile\":%u,\"allow_synthetic\":%s",
            i?",":"",d->alias,c->type==AUDIO_UDP?"lvaf":"gateway",c->type==AUDIO_UDP?"address":"name",c->address,(unsigned)c->profile,c->synthetic?"true":"false");write(ctx,b);
        if(c->type==AUDIO_UDP){snprintf(b,sizeof(b),",\"port\":%u",c->port);write(ctx,b);}
        if(c->pin){snprintf(b,sizeof(b),",\"source_id\":\"%016llx\"",(unsigned long long)c->pin);write(ctx,b);}write(ctx,"}");
    }
    write(ctx,"}");
}
bool input_scope_lookup(const InputSourceScope *s,const char *name,InputSignal *out,uint64_t *events,uint64_t now){
    if(!s)return false;
    const char *dot=strchr(name,'.');if(!dot)return false;size_t length=(size_t)(dot-name);
    for(unsigned i=0;i<s->count;i++){
        const InputSourceDeclaration *d=&s->declarations[i];if(strlen(d->alias)!=length||memcmp(name,d->alias,length))continue;
        memset(out,0,sizeof(*out));*events=0;out->type=T_FLOAT;const char *field=dot+1;
        bool event=!strcmp(field,"onsets");if(event)out->type=T_INT;
        AudioProvider *p=d->provider;if(!p)return true;
        KfSubscriber *r=p->config.type==AUDIO_GATEWAY?(p->gateway?&p->gateway->receiver:NULL):&p->receiver;
        if(!r)return true;
        *events=r->events;
        if(!kf_subscriber_available(r,now)||r->profile!=d->config.profile||(d->config.pin&&r->source!=d->config.pin)||
           (r->latest.flags&&!d->config.synthetic))return true;
        static const char *const names[]={"level","bass","mid","treble","centroid","onset"};
        bool found=event;
        for(unsigned j=0;j<6;j++)if(!strcmp(field,names[j])){out->value.f=r->latest.values[j];found=true;}
        if(strlen(field)==6&&!memcmp(field,"band",4)&&field[4]>='0'&&field[4]<='9'&&field[5]>='0'&&field[5]<='9'){
            unsigned band=(unsigned)(field[4]-'0')*10+(unsigned)(field[5]-'0');
            if(band<r->latest.band_count){out->value.f=r->latest.spectrum[band];found=true;}
        }
        out->valid=found;out->received=r->received;return true;
    }
    return false;
}
void audio_source_receive(AudioProvider *p,const uint8_t *data,size_t size,uint64_t now){
    if(size>=4&&!memcmp(data,"LVAS",4)){KfControl c;if(kf_control_decode(data,size,&c))kf_subscriber_control(&p->receiver,&c,now);else p->receiver.invalid++;}
    else{KfFrame f;if(kf_decode(data,size,&f))kf_subscriber_frame(&p->receiver,&f,now);else p->receiver.invalid++;}
}
void audio_sources_tick(AudioSources *m,uint64_t now){
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++){
        AudioProvider *p=m->providers[i];if(!p)continue;
        if(p->config.type==AUDIO_GATEWAY){
            AudioGateway *g=p->gateway;
            if(g&&g->staged&&now>=g->frame_deadline)g->staged=g->total=0;
            if(g&&g->owner&&now>=g->deadline){
                uint32_t generation=g->generation;uint64_t owner=g->owner;
                (void)audio_gateway_release(m,owner,generation);
            }
            continue;
        }
        if(!m->transport.open||now<p->retry_at)continue;
        if(!p->receiver.client){
            if(m->transport.close)m->transport.close(m->transport.context,p);
            if(!m->transport.open(m->transport.context,p)){p->retry_at=now+1000000;continue;}
            kf_subscriber_open(&p->receiver,audio_source_random(),now);
        }
        KfControl c;uint8_t bytes[KF_CONTROL_SIZE];
        if(kf_subscriber_poll(&p->receiver,now,(uint32_t)audio_source_random(),&c)){
            size_t size=kf_control_encode(&c,bytes,sizeof(bytes));
            if(size&&m->transport.send)m->transport.send(m->transport.context,p,bytes,size);
        }
    }
}
void audio_sources_disconnect(AudioSources *m){
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++){
        AudioProvider *p=m->providers[i];if(!p||p->config.type!=AUDIO_UDP)continue;
        if(m->transport.close)m->transport.close(m->transport.context,p);
        p->receiver.client=0;p->receiver.has_latest=false;p->retry_at=0;
    }
}
void audio_sources_destroy(AudioSources *m){for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++)if(m->providers[i])remove_provider(m,m->providers[i]);}
void audio_sources_status(const AudioSources *m,void (*write)(void *,const char *),void *ctx){
    write(ctx,"[");bool comma=false;uint64_t now=platform_time_us();
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++){
        const AudioProvider *p=m->providers[i];if(!p)continue;const KfSubscriber *r=p->gateway?&p->gateway->receiver:&p->receiver;char b[384];
        const char *status=p->error?p->error:r->status==KF_FULL?"capacity refused":
            r->status==KF_WRONG_SOURCE||(p->config.pin&&r->source&&r->source!=p->config.pin)?"identity mismatch":
            r->status==KF_UNSUPPORTED||r->profile!=p->config.profile?"unsupported format":
            r->has_latest&&r->latest.flags&&!p->config.synthetic?"synthetic blocked":
            kf_subscriber_available(r,now)?"connected":r->expires?"stale":"offline";
        snprintf(b,sizeof(b),"%s{\"type\":\"%s\",\"address\":\"%s\",\"profile\":%u,\"source_id\":\"%016llx\",\"status\":\"%s\"",
            comma?",":"",p->config.type==AUDIO_UDP?"lvaf":"gateway",p->config.address,(unsigned)r->profile,(unsigned long long)r->source,status);write(ctx,b);
        snprintf(b,sizeof(b),",\"port\":%u,\"expected_source_id\":\"%016llx\",\"allow_synthetic\":%s",
            p->config.port,(unsigned long long)p->config.pin,p->config.synthetic?"true":"false");write(ctx,b);
        bool ingress_owner=true;
        for(unsigned k=0;k<i;k++)if(p->gateway&&m->providers[k]&&m->providers[k]->gateway==p->gateway)ingress_owner=false;
        snprintf(b,sizeof(b),",\"references\":%u,\"packets\":%u,\"age_rejections\":%u,\"rtt_us\":%llu,\"bands\":%u,\"charged_bytes\":%zu",
            p->references,(unsigned)r->packets,(unsigned)r->expired,(unsigned long long)(r->clock.received-r->clock.sent),r->latest.band_count,
            sizeof(*p)+(p->config.type==AUDIO_UDP?m->transport.charged_bytes:p->gateway&&ingress_owner?sizeof(*p->gateway):0));
        write(ctx,b);
        snprintf(b,sizeof(b),",\"expected_profile\":%u,\"session\":\"%016llx\",\"sequence\":\"%016llx\",\"synthetic\":%s,\"band_edges\":[",
            (unsigned)p->config.profile,(unsigned long long)r->session,(unsigned long long)r->latest.sequence,r->latest.flags?"true":"false");write(ctx,b);
        if(r->has_latest&&r->latest.band_count)for(unsigned band=0;band<=r->latest.band_count;band++){
            snprintf(b,sizeof(b),"%s%u",band?",":"",r->latest.band_edges[band]);write(ctx,b);
        }
        write(ctx,"]}");comma=true;
    }write(ctx,"]");
}

static AudioGateway *gateway_owned(AudioSources *m,uint64_t owner,uint32_t generation){
    if(!owner||!generation)return NULL;
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++){
        AudioProvider *p=m->providers[i];
        if(p&&p->gateway&&p->gateway->owner==owner&&p->gateway->generation==generation)return p->gateway;
    }
    return NULL;
}
static void gateway_challenge(AudioGateway *g,uint64_t challenge,uint64_t now,AudioChallenge *out){
    g->challenge=challenge;g->challenge_sent=now;
    *out=(AudioChallenge){g->generation,challenge,now};
}
const char *audio_gateway_claim(AudioSources *m,const Json *j,uint64_t owner,uint64_t now,AudioChallenge *out){
    static const char *const fields[]={"op","name","source_id","session","analysis_profile","synthetic",NULL};
    char name[OBJECT_NAME_LIMIT+1];uint64_t source,session;uint32_t profile;bool synthetic=false;
    int flag=json_get(j,0,"synthetic");
    if(!owner||!json_fields(j,0,fields)||!json_string(j,json_get(j,0,"name"),name,sizeof(name))||!object_name_valid(name)||
       !audio_source_id(j,json_get(j,0,"source_id"),&source)||!audio_source_id(j,json_get(j,0,"session"),&session)||
       !json_u32(j,json_get(j,0,"analysis_profile"),&profile)||(profile!=1&&profile!=2)||
       (flag>=0&&!json_bool(j,flag,&synthetic)))return "invalid structured audio claim";
    if(m->generation==UINT32_MAX)return "audio generation exhausted; reboot controller";
    uint64_t challenge=audio_source_random();if(!challenge)return "audio challenge randomness unavailable";
    AudioProvider *first=NULL;unsigned empty=AUDIO_PROVIDER_LIMIT;
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++){
        AudioProvider *p=m->providers[i];
        if(!p)empty=i;
        else if(p->config.type==AUDIO_GATEWAY&&!strcmp(p->config.address,name))first=p;
    }
    if(!first&&empty==AUDIO_PROVIDER_LIMIT)return "structured audio provider capacity exhausted";
    AudioGateway *g=first?first->gateway:NULL;
    bool allocated=false;
    if(!g){g=demo_calloc(1,sizeof(*g));if(!g)return "audio ingress exceeds memory budget";allocated=true;}
    if(!first){
        first=demo_calloc(1,sizeof(*first));
        if(!first){if(allocated)demo_free(g);return "audio provider exceeds memory budget";}
        first->config.type=AUDIO_GATEWAY;first->config.profile=profile;strcpy(first->config.address,name);
        m->providers[empty]=first;
    }
    /* Allocate everything before revoking the previous owner. Counter totals
     * survive a new stream; each invocation owns its independent baseline. */
    uint64_t events=g->receiver.events;
    memset(g,0,sizeof(*g));kf_subscriber_init(&g->receiver,0,profile,synthetic);g->receiver.events=events;
    g->generation=++m->generation;g->owner=owner;g->deadline=now+15000000;g->synthetic=synthetic;
    g->receiver.client=g->receiver.lease=g->generation;g->receiver.source=source;g->receiver.session=session;
    g->receiver.expires=g->deadline;
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++){
        AudioProvider *p=m->providers[i];
        if(p&&p->config.type==AUDIO_GATEWAY&&!strcmp(p->config.address,name))p->gateway=g;
    }
    gateway_challenge(g,challenge,now,out);return NULL;
}
const char *audio_gateway_keep(AudioSources *m,uint64_t owner,uint32_t generation,uint64_t now,AudioChallenge *out){
    AudioGateway *g=gateway_owned(m,owner,generation);
    if(!g||now>=g->deadline)return "expired or unowned audio generation";
    uint64_t challenge=audio_source_random();if(!challenge)return "audio challenge randomness unavailable";
    /* A keepalive renews ownership, never the clock bound or feature age. */
    g->deadline=g->receiver.expires=now+15000000;gateway_challenge(g,challenge,now,out);return NULL;
}
const char *audio_gateway_clock(AudioSources *m,uint64_t owner,uint32_t generation,uint64_t challenge,uint64_t remote,uint64_t now){
    AudioGateway *g=gateway_owned(m,owner,generation);
    if(!g||now>=g->deadline||!remote||!challenge||challenge!=g->challenge||now<g->challenge_sent||now-g->challenge_sent>=5000000)
        return "invalid or expired audio clock challenge";
    uint64_t sent=g->challenge_sent;g->challenge=0;
    KfClockBound old=g->receiver.clock;
    if(old.remote){
        uint64_t lower=sent>old.received?sent-old.received:0;
        uint64_t upper=now>=old.sent?now-old.sent:0;
        uint64_t elapsed=remote>old.remote?remote-old.remote:0;
        if(remote<=old.remote||now<old.received||elapsed>upper+2000||(lower>2000&&elapsed<lower-2000)){
            g->receiver.has_latest=false;g->receiver.clock=(KfClockBound){0};
            g->receiver.client=0;return "audio clock discontinuity; claim a new session";
        }
    }
    if(!g->receiver.client)return "audio clock discontinuity; claim a new session";
    if(!old.remote)g->receiver.joined_us=remote;
    g->receiver.clock=(KfClockBound){sent,now,remote};
    g->deadline=g->receiver.expires=now+15000000;return NULL;
}
static uint64_t audio_read_u64(const uint8_t *p){return (uint64_t)read_u32(p)|((uint64_t)read_u32(p+4)<<32);}
const char *audio_gateway_fragment(AudioSources *m,uint64_t owner,const uint8_t *bytes,size_t size,uint64_t now){
    if(size<=AUDIO_FRAGMENT_HEADER||size>FRAME_LIMIT)return "invalid audio fragment size";
    AudioGateway *g=gateway_owned(m,owner,read_u32(bytes));
    if(!g||now>=g->deadline)return "expired or unowned audio generation";
    uint64_t frame=audio_read_u64(bytes+4);
    unsigned total=(unsigned)bytes[12]|((unsigned)bytes[13]<<8),offset=(unsigned)bytes[14]|((unsigned)bytes[15]<<8);
    size_t length=size-AUDIO_FRAGMENT_HEADER;
    if(g->staged&&now>=g->frame_deadline)g->staged=g->total=0;
    if(!frame||total<KF_WIRE_HEADER||total>KF_WIRE_MAX||offset>total||length>total-offset)return "invalid audio fragment bounds";
    if(!offset){
        if(g->staged){g->staged=g->total=0;return "overlapping audio frames";}
        if(frame<=g->frame_id)return "replayed audio frame identity";
        g->frame_id=frame;g->total=(uint16_t)total;g->frame_deadline=now+250000;
    }else if(!g->staged||frame!=g->frame_id||total!=g->total||offset!=g->staged){
        g->staged=g->total=0;return "reordered audio fragment";
    }
    memcpy(g->staging+offset,bytes+AUDIO_FRAGMENT_HEADER,length);g->staged=(uint16_t)(offset+length);
    if(g->staged!=g->total)return NULL;
    g->staged=g->total=0;
    KfFrame f;
    /* Gateway records use the reserved neutral lease value 1. Ownership stays
     * in the outer command; the common receiver gets its local generation. */
    if(!kf_decode(g->staging,total,&f)||f.lease!=1||f.flags!=(g->synthetic?1:0))return "invalid audio feature record";
    f.lease=g->receiver.lease;
    if(!kf_subscriber_frame(&g->receiver,&f,now))return "unavailable, mismatched or stale audio frame";
    g->deadline=g->receiver.expires=now+15000000;return NULL;
}
const char *audio_gateway_release(AudioSources *m,uint64_t owner,uint32_t generation){
    AudioGateway *g=gateway_owned(m,owner,generation);if(!g)return "unowned audio generation";
    g->owner=0;g->staged=g->total=0;g->challenge=0;g->receiver.client=0;g->receiver.has_latest=false;
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++){
        AudioProvider *p=m->providers[i];if(p&&p->gateway==g&&!p->references)remove_provider(m,p);
    }
    return NULL;
}
void audio_gateway_disconnect(AudioSources *m,uint64_t owner){
    if(!owner)return;
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++){
        AudioProvider *p=m->providers[i];
        if(p&&p->gateway&&p->gateway->owner==owner){
            /* Session closure may arrive during a streamed reply. Invalidate
             * immediately; the next engine tick frees unreferenced storage. */
            p->gateway->deadline=0;p->gateway->receiver.client=0;p->gateway->receiver.has_latest=false;
        }
    }
}
