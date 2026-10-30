#include "device/discovery.h"
#include "device/version.h"
#include "device/metadata.h"
#include "device/description.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

// A bounded IPv4 DNS-SD implementation for the common device management service. In particular,
// lightvm.local is a shared bootstrap A record: never probe it or set cache-flush.
// Unique host/service records use the MAC-derived name and are probed separately.
static const uint8_t multicast[4]={224,0,0,251};
static const char service[]="_lightvm._tcp.local",bootstrap[]="lightvm.local";
enum { A=1,PTR=12,TXT=16,SRV=33,ANY=255 };
enum { WANT_SERVICE=1,WANT_ALIAS=2,WANT_HOST=4,WANT_SRV=8,WANT_TXT=16,WANT_ENUM=32 };
typedef struct { uint8_t *bytes; size_t used,capacity; bool failed; } Packet;
typedef struct { char name[96]; uint16_t type,klass; uint32_t ttl; size_t data,end; } Record;
static uint16_t u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]<<8|p[1]); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void put(Packet *p,const void *data,size_t size) {
    if (size>p->capacity-p->used) { p->failed=true; return; }
    memcpy(p->bytes+p->used,data,size); p->used+=size;
}
static void number(Packet *p,uint32_t n,unsigned size) {
    uint8_t b[4]; for (unsigned i=0;i<size;i++) b[i]=(uint8_t)(n>>(8*(size-1-i))); put(p,b,size);
}
static void name(Packet *p,const char *s) {
    while (*s) {
        const char *dot=strchr(s,'.'); size_t n=dot?(size_t)(dot-s):strlen(s);
        if (!n || n>63) { p->failed=true; return; }
        number(p,(uint32_t)n,1); put(p,s,n); s+=n; if (*s) s++;
    }
    number(p,0,1);
}
// Following compression pointers only backwards rules out cycles and bounds work.
// This also rejects reserved label types, oversized names and embedded controls.
static bool read_name(const uint8_t *bytes,size_t size,size_t *offset,char *out,size_t capacity) {
    size_t at=*offset,used=0,end=0; unsigned hops=0;
    while (at<size && ++hops<=64) {
        size_t label=at; unsigned n=bytes[at++];
        if ((n&0xc0)==0xc0) {
            if (at>=size) return false;
            size_t target=((size_t)(n&63)<<8)|bytes[at++];
            if (target>=label) return false;
            if (!end) end=at;
            at=target; continue;
        }
        if (n&0xc0 || n>size-at) return false;
        if (!n) { if (used>=capacity) return false; out[used]=0; *offset=end?end:at; return true; }
        if (used) { if (used+1>=capacity) return false; out[used++]='.'; }
        if (n>=capacity-used) return false;
        for (unsigned i=0;i<n;i++) {
            unsigned ch=bytes[at++];
            if (!(isalnum(ch) || ch=='-' || ch=='_')) return false;
            out[used++]=(char)tolower(ch);
        }
    }
    return false;
}
static bool record(const uint8_t *bytes,size_t size,size_t *at,Record *r) {
    if (!read_name(bytes,size,at,r->name,sizeof(r->name)) || size-*at<10) return false;
    r->type=u16(bytes+*at); r->klass=u16(bytes+*at+2)&0x7fff; r->ttl=u32(bytes+*at+4);
    size_t length=u16(bytes+*at+8); *at+=10;
    if (length>size-*at) return false;
    r->data=*at; *at+=length; r->end=*at; return true;
}
static bool label(const char *s,size_t capacity) {
    size_t n=strlen(s); if (!n || n>=capacity) return false;
    for (size_t i=0;i<n;i++) if (!(isalnum((unsigned char)s[i]) || s[i]=='-' || s[i]=='_')) return false;
    return true;
}
static bool instance_label(const char *full,char out[32]) {
    const char *dot=strchr(full,'.');
    if (!dot || strcmp(dot+1,service) || (size_t)(dot-full)>=32) return false;
    memcpy(out,full,(size_t)(dot-full)); out[dot-full]=0;
    return label(out,32);
}
static bool host_label(const char *full,char out[32]) {
    const char *dot=strchr(full,'.');
    if (!dot || strcmp(dot,".local") || (size_t)(dot-full)>=32) return false;
    memcpy(out,full,(size_t)(dot-full)); out[dot-full]=0;
    return label(out,32);
}
static bool ip_valid(const uint8_t ip[4]) { return ip[0] && ip[0]<224 && ip[0]!=127; }
static uint64_t until(uint64_t now,uint32_t ttl) {
    // Our list is refreshed regularly; bound stale foreign advertisements too.
    if (ttl>DISCOVERY_TTL) ttl=DISCOVERY_TTL;
    return now+(uint64_t)ttl*1000000u;
}
static void expire(Discovery *d,uint64_t now) {
    for (unsigned i=1;i<DISCOVERY_LIMIT;i++) {
        DiscoveryController *p=&d->controllers[i];
        uint64_t last=p->ptr_until;
        if (p->srv_until>last) last=p->srv_until;
        if (p->ip_until>last) last=p->ip_until;
        if (p->txt_until>last) last=p->txt_until;
        if (p->hostname[0] && now>=last) memset(p,0,sizeof(*p));
    }
    bool full=true; for (unsigned i=1;i<DISCOVERY_LIMIT;i++) if (!d->controllers[i].hostname[0]) full=false;
    if (!full) d->truncated=false;
}
static DiscoveryController *peer(Discovery *d,const char *hostname,bool create) {
    if (!strcmp(hostname,d->controllers[0].hostname)) return NULL;
    DiscoveryController *empty=NULL;
    for (unsigned i=1;i<DISCOVERY_LIMIT;i++) {
        DiscoveryController *p=&d->controllers[i];
        if (!strcmp(hostname,p->hostname)) return p;
        if (!p->hostname[0] && !empty) empty=p;
    }
    if (!create) return NULL;
    if (!empty) { d->truncated=true; return NULL; }
    strcpy(empty->hostname,hostname); return empty;
}
static size_t rr(Packet *p,const char *owner,unsigned type,bool flush,uint32_t ttl) {
    name(p,owner); number(p,type,2); number(p,flush?0x8001:1,2); number(p,ttl,4);
    size_t length=p->used; number(p,0,2); return length;
}
static void finish_rr(Packet *p,size_t length) {
    if (p->failed || length+2>p->used) return;
    size_t n=p->used-length-2; p->bytes[length]=(uint8_t)(n>>8); p->bytes[length+1]=(uint8_t)n;
}
static void text(Packet *p,const char *key,const char *value) {
    size_t a=strlen(key),b=strlen(value); number(p,(uint32_t)(a+b),1); put(p,key,a); put(p,value,b);
}
static void own_records(Discovery *d,Packet *p,uint32_t ttl,bool alias,bool legacy) {
    DiscoveryController *self=&d->controllers[0]; char host[40],instance[64];
    snprintf(host,sizeof(host),"%s.local",self->hostname);
    snprintf(instance,sizeof(instance),"%s.%s",self->hostname,service);
    bool flush=!legacy;
    size_t n=rr(p,service,PTR,false,ttl); name(p,instance); finish_rr(p,n);
    n=rr(p,instance,SRV,flush,ttl); number(p,0,2); number(p,0,2); number(p,self->port,2); name(p,host); finish_rr(p,n);
    n=rr(p,instance,TXT,flush,ttl); text(p,"id=",self->controller_id); text(p,"board=",self->board); text(p,"version=",self->version); text(p,"kind=",self->kind);
    char caps[16]; snprintf(caps,sizeof(caps),"%u",(unsigned)self->capabilities); text(p,"caps=",caps); finish_rr(p,n);
    n=rr(p,host,A,flush,ttl); put(p,self->ip,4); finish_rr(p,n);
    if (alias) { n=rr(p,bootstrap,A,false,ttl?10:0); put(p,self->ip,4); finish_rr(p,n); }
}
static bool send_packet(Discovery *d,Packet *p,const uint8_t ip[4],uint16_t port) {
    return !p->failed && d->send && d->send(d->context,p->bytes,p->used,ip,port);
}
static void response(Discovery *d,uint32_t ttl,bool alias,const uint8_t ip[4],uint16_t port,
                     uint16_t id,const char *question,unsigned type,unsigned klass,bool enumeration) {
    Packet p={d->packet,0,sizeof(d->packet),false}; bool legacy=port!=DISCOVERY_PORT;
    number(&p,id,2); number(&p,0x8400,2); number(&p,question?1:0,2);
    number(&p,4+(alias?1:0)+(enumeration?1:0),2); number(&p,0,2); number(&p,0,2);
    if (question) { name(&p,question); number(&p,type,2); number(&p,klass&0x7fff,2); }
    own_records(d,&p,legacy && ttl>10?10:ttl,alias,legacy);
    if (enumeration) { size_t n=rr(&p,"_services._dns-sd._udp.local",PTR,false,ttl); name(&p,service); finish_rr(&p,n); }
    send_packet(d,&p,ip,port);
}
static bool complete(const DiscoveryController *p,uint64_t now) {
    return p->hostname[0] && p->controller_id[0] && p->board[0] && p->kind[0] && p->version[0] && p->port && p->ptr_until>now && p->srv_until>now && p->txt_until>now && p->ip_until>now;
}
static void query(Discovery *d,bool probe,uint64_t now) {
    Packet p={d->packet,0,sizeof(d->packet),false}; char host[40];
    snprintf(host,sizeof(host),"%s.local",d->controllers[0].hostname);
    unsigned known=0;
    if (!probe) for (unsigned i=1;i<DISCOVERY_LIMIT;i++) {
        DiscoveryController *peer=&d->controllers[i];
        if (complete(peer,now+1000000u)) known++;
    }
    number(&p,0,2); number(&p,0,2); number(&p,1,2); number(&p,known,2); number(&p,probe?1:0,2); number(&p,0,2);
    name(&p,probe?host:service); number(&p,probe?ANY:PTR,2); number(&p,1,2);
    if (probe) { size_t n=rr(&p,host,A,false,DISCOVERY_TTL); put(&p,d->controllers[0].ip,4); finish_rr(&p,n); }
    else for (unsigned i=1;i<DISCOVERY_LIMIT;i++) {
        DiscoveryController *peer=&d->controllers[i];
        if (complete(peer,now+1000000u)) {
            char instance[64]; snprintf(instance,sizeof(instance),"%s.%s",peer->hostname,service);
            uint64_t end=peer->ptr_until;
            if (peer->srv_until<end) end=peer->srv_until;
            if (peer->txt_until<end) end=peer->txt_until;
            if (peer->ip_until<end) end=peer->ip_until;
            size_t n=rr(&p,service,PTR,false,(uint32_t)((end-now)/1000000u)); name(&p,instance); finish_rr(&p,n);
        }
    }
    send_packet(d,&p,multicast,DISCOVERY_PORT);
}
void discovery_start(Discovery *d,const char *hostname,const DeviceHost *identity,const uint8_t ip[4],
                     uint16_t port,DiscoverySend send,void *context,uint64_t now) {
    memset(d,0,sizeof(*d));
    if (!identity || !identity->controller_id || !identity->board || !identity->version || !identity->kind) return;
    d->metadata=identity->metadata;
    const char *id=identity->controller_id,*board=identity->board;
    if (!device_version_valid(identity->version,false) || !label(identity->kind,32)) return;
    if (!label(hostname,sizeof(d->controllers[0].hostname)) || !label(id,sizeof(d->controllers[0].controller_id)) || !label(board,sizeof(d->controllers[0].board)) || !port) return;
    DiscoveryController *self=&d->controllers[0]; strcpy(self->hostname,hostname); strcpy(self->controller_id,id); strcpy(self->board,board);
    strcpy(self->version,identity->version); strcpy(self->kind,identity->kind); self->capabilities=identity->capabilities;
    memcpy(self->ip,ip,4); self->port=port; d->send=send; d->context=context; d->active=true;
    d->next_announce=now+(uint64_t)(ip[3]%100)*1000u; d->next_query=now;
}
void discovery_stop(Discovery *d,uint64_t now) {
    (void)now;
    if (d->active && d->announced) response(d,0,true,multicast,DISCOVERY_PORT,0,NULL,0,0,false);
    memset(d,0,sizeof(*d));
}
static void parse_txt(DiscoveryController *p,const uint8_t *bytes,size_t at,size_t end) {
    char id[64]="",board[32]="",version[96]="",kind[32]="";
    uint32_t capabilities=0; bool caps_valid=false;
    while (at<end) {
        unsigned n=bytes[at++]; if (n>end-at) return;
        const uint8_t *text=bytes+at; at+=n;
        char *out=NULL; size_t prefix=0,capacity=0;
        if (n>3 && !memcmp(text,"id=",3)) { out=id; prefix=3; capacity=sizeof(id); }
        else if (n>6 && !memcmp(text,"board=",6)) { out=board; prefix=6; capacity=sizeof(board); }
        else if (n>8 && !memcmp(text,"version=",8)) { out=version; prefix=8; capacity=sizeof(version); }
        else if (n>5 && !memcmp(text,"kind=",5)) { out=kind; prefix=5; capacity=sizeof(kind); }
        else if (n>5 && !memcmp(text,"caps=",5)) {
            uint32_t value=0;
            for (unsigned i=5;i<n;i++) {
                unsigned digit=text[i]-'0'; if (digit>9 || value>(UINT32_MAX-digit)/10) return;
                value=value*10+digit;
            }
            capabilities=value; caps_valid=true;
        }
        if (out) {
            size_t length=n-prefix; if (length>=capacity) return;
            memcpy(out,text+prefix,length); out[length]=0;
            if (memchr(out,0,length)) return;
        }
    }
    if (device_version_valid(version,false) && label(id,sizeof(id)) && label(board,sizeof(board)) && label(kind,sizeof(kind)) && caps_valid) {
        strcpy(p->controller_id,id); strcpy(p->board,board); strcpy(p->version,version); strcpy(p->kind,kind); p->capabilities=capabilities;
    }
}
void discovery_receive(Discovery *d,const uint8_t *bytes,size_t size,const uint8_t from[4],uint16_t port,uint64_t now) {
    if (!d->active || size<12 || size>DISCOVERY_PACKET_LIMIT) return;
    unsigned flags=u16(bytes+2),questions=u16(bytes+4),records=u16(bytes+6)+u16(bytes+8)+u16(bytes+10);
    if ((flags&0x780f) || questions>16 || records>32) return;
    size_t at=12; bool answer=false,alias=false,enumeration=false,unicast=port!=DISCOVERY_PORT;
    unsigned wanted=0,known=0;
    char first[96]=""; unsigned first_type=0,first_class=0;
    char host[40],instance[64]; snprintf(host,sizeof(host),"%s.local",d->controllers[0].hostname);
    snprintf(instance,sizeof(instance),"%s.%s",d->controllers[0].hostname,service);
    for (unsigned i=0;i<questions;i++) {
        char question[96]; if (!read_name(bytes,size,&at,question,sizeof(question)) || size-at<4) return;
        unsigned type=u16(bytes+at),klass=u16(bytes+at+2); at+=4;
        if ((klass&0x7fff)!=1 && (klass&0x7fff)!=ANY) continue;
        bool match=(!strcmp(question,bootstrap) && (type==A || type==ANY)) ||
            (!strcmp(question,host) && (type==A || type==ANY)) ||
            (!strcmp(question,service) && (type==PTR || type==ANY)) ||
            (!strcmp(question,instance) && (type==SRV || type==TXT || type==ANY)) ||
            (!strcmp(question,"_services._dns-sd._udp.local") && (type==PTR || type==ANY));
        if (match) {
            answer=true; alias|=!strcmp(question,bootstrap); enumeration|=!strcmp(question,"_services._dns-sd._udp.local");
            unicast|=(klass&0x8000)!=0;
            if (!first[0]) { strcpy(first,question); first_type=type; first_class=klass; }
            if (!strcmp(question,bootstrap)) wanted|=WANT_ALIAS;
            else if (!strcmp(question,host)) wanted|=WANT_HOST;
            else if (!strcmp(question,service)) wanted|=WANT_SERVICE;
            else if (!strcmp(question,instance)) wanted|=type==SRV?WANT_SRV:type==TXT?WANT_TXT:WANT_SRV|WANT_TXT;
            else wanted|=WANT_ENUM;
        }
    }
    size_t start=at;
    // Validate the whole datagram before changing the cache or emitting a reply.
    for (unsigned i=0;i<records;i++) { Record r; if (!record(bytes,size,&at,&r)) return; }
    if (at!=size) return;
    expire(d,now);
    at=start;
    for (unsigned i=0;i<records;i++) {
        Record r; if (!record(bytes,size,&at,&r)) return;
        bool authority=i>=u16(bytes+6) && i<u16(bytes+6)+u16(bytes+8);
        if (!(flags&0x8000) && i<u16(bytes+6) && r.klass==1) {
            size_t data=r.data; char target[96];
            if (r.type==PTR && r.ttl>=DISCOVERY_TTL/2 && !strcmp(r.name,service) && read_name(bytes,size,&data,target,sizeof(target)) && data==r.end && !strcmp(target,instance)) known|=WANT_SERVICE;
            if (r.type==A && r.end-r.data==4 && !memcmp(bytes+r.data,d->controllers[0].ip,4)) {
                if (!strcmp(r.name,bootstrap) && r.ttl>=5) known|=WANT_ALIAS;
                if (!strcmp(r.name,host) && r.ttl>=DISCOVERY_TTL/2) known|=WANT_HOST;
            }
            if (!strcmp(r.name,instance) && r.ttl>=DISCOVERY_TTL/2) {
                if (r.type==SRV && r.end-r.data>=7 && u16(bytes+r.data+4)==d->controllers[0].port) {
                    data+=6; if (read_name(bytes,size,&data,target,sizeof(target)) && data==r.end && !strcmp(target,host)) known|=WANT_SRV;
                } else if (r.type==TXT) {
                    DiscoveryController txt={0}; parse_txt(&txt,bytes,r.data,r.end);
                    if (!strcmp(txt.controller_id,d->controllers[0].controller_id) && !strcmp(txt.board,d->controllers[0].board) && !strcmp(txt.version,d->controllers[0].version) && !strcmp(txt.kind,d->controllers[0].kind) && txt.capabilities==d->controllers[0].capabilities) known|=WANT_TXT;
                }
            }
        }
        if ((flags&0x8000 || authority) && r.klass==1 && r.type==A && !strcmp(r.name,host) && r.end-r.data==4 && r.ttl && memcmp(bytes+r.data,d->controllers[0].ip,4)) {
            // A unique-name conflict: choose another suffix and probe again. The
            // shared bootstrap name is intentionally excluded from this path.
            if ((flags&0x8000) || memcmp(d->controllers[0].ip,bytes+r.data,4)<0) {
                char renamed[32]; snprintf(renamed,sizeof(renamed),"device-%02x%02x%02x-%u",d->controllers[0].ip[1],d->controllers[0].ip[2],d->controllers[0].ip[3],++d->collision);
                strcpy(d->controllers[0].hostname,renamed); d->probes=0; d->announced=false; d->next_announce=now+250000u;
                return;
            }
        }
    }
    if (flags&0x8000) {
        // Three passes make PTR, SRV/TXT and address ordering irrelevant. Only
        // PTR-confirmed device instances may occupy the bounded cache.
        for (unsigned pass=0;pass<3;pass++) {
            at=start;
            for (unsigned i=0;i<records;i++) {
                Record r; if (!record(bytes,size,&at,&r)) return;
                if (r.klass!=1) continue;
                char key[32],target[96]; DiscoveryController *p=NULL; size_t data=r.data;
                if (pass==0 && r.type==PTR && !strcmp(r.name,service)) {
                    if (!read_name(bytes,size,&data,target,sizeof(target)) || data!=r.end || !instance_label(target,key)) continue;
                    p=peer(d,key,r.ttl!=0); if (p) { if (!r.ttl) memset(p,0,sizeof(*p)); else p->ptr_until=until(now,r.ttl); }
                } else if (pass==1 && instance_label(r.name,key)) {
                    p=peer(d,key,false); if (!p) continue;
                    if (r.type==SRV && r.end-r.data>=7) {
                        data+=6;
                        if (!read_name(bytes,size,&data,target,sizeof(target)) || data!=r.end || !host_label(target,key) || strcmp(key,p->hostname)) continue;
                        p->port=u16(bytes+r.data+4); p->srv_until=until(now,r.ttl);
                    } else if (r.type==TXT) {
                        p->controller_id[0]=0; p->board[0]=0; p->kind[0]=0; p->version[0]=0; p->capabilities=0; parse_txt(p,bytes,r.data,r.end); p->txt_until=until(now,r.ttl);
                    }
                } else if (pass==2 && r.type==A && r.end-r.data==4 && host_label(r.name,key)) {
                    p=peer(d,key,false); if (p && ip_valid(bytes+r.data)) { memcpy(p->ip,bytes+r.data,4); p->ip_until=until(now,r.ttl); }
                }
            }
        }
    } else if (answer && d->announced && (unicast || (wanted&~known))) {
        if (unicast) response(d,DISCOVERY_TTL,alias,from,port,port!=DISCOVERY_PORT?u16(bytes):0,port!=DISCOVERY_PORT?first:NULL,first_type,first_class,enumeration);
        else {
            d->reply_alias|=alias;
            if (!d->reply_pending) { d->reply_pending=true; d->reply_at=now+20000u+(uint64_t)(d->controllers[0].ip[3]%100)*1000u; }
            // Enumeration is rare; send it with the same bounded answer packet.
            if (enumeration) response(d,DISCOVERY_TTL,false,multicast,DISCOVERY_PORT,0,NULL,0,0,true);
        }
    }
}
void discovery_tick(Discovery *d,uint64_t now) {
    if (!d->active) return;
    expire(d,now);
    if (now>=d->next_announce) {
        if (d->probes<3) { query(d,true,now); d->probes++; d->next_announce=now+250000u; }
        else {
            response(d,DISCOVERY_TTL,false,multicast,DISCOVERY_PORT,0,NULL,0,0,false);
            d->announced=true; d->next_announce=now+10000000u;
        }
    }
    if (now>=d->next_query) { query(d,false,now); d->next_query=now+5000000u; }
    if (d->reply_pending && now>=d->reply_at) {
        response(d,DISCOVERY_TTL,d->reply_alias,multicast,DISCOVERY_PORT,0,NULL,0,0,false);
        d->reply_pending=false; d->reply_alias=false;
    }
}
static void json(char *out,size_t capacity,size_t *used,const char *format,...) {
    if (*used>=capacity) return;
    va_list args; va_start(args,format); int n=vsnprintf(out+*used,capacity-*used,format,args); va_end(args);
    if (n<0 || (size_t)n>=capacity-*used) *used=capacity; else *used+=(size_t)n;
}
typedef struct {char *out;size_t capacity,*used;} DiscoveryText;
static void discovery_text(void *context,const char *text){DiscoveryText *w=context;json(w->out,w->capacity,w->used,"%s",text);}
size_t discovery_json(Discovery *d,char *out,size_t capacity,uint64_t now) {
    size_t used=0; unsigned count=0;
    if (d && d->active) expire(d,now);
    json(out,capacity,&used,"{\"schema\":\"lightvm.discovery\",\"capacity\":%u,\"available\":%s,\"truncated\":%s,\"controllers\":[",DISCOVERY_LIMIT,d && d->active?"true":"false",d && d->truncated?"true":"false");
    if (d && d->active) {
        for (unsigned i=0;i<DISCOVERY_LIMIT;i++) {
            DiscoveryController *p=&d->controllers[i]; if (i && !complete(p,now)) continue;
            json(out,capacity,&used,"%s{\"hostname\":\"%s.local\",\"controller_id\":\"%s\",\"board\":\"%s\",\"version\":\"%s\",\"kind\":\"%s\",\"capabilities\":%u,\"ip\":\"%u.%u.%u.%u\",\"port\":%u,\"url\":\"http://%u.%u.%u.%u:%u\",\"nickname\":",count++?",":"",p->hostname,p->controller_id,p->board,p->version,p->kind,(unsigned)p->capabilities,p->ip[0],p->ip[1],p->ip[2],p->ip[3],p->port,p->ip[0],p->ip[1],p->ip[2],p->ip[3],p->port);
            if(!i&&d->metadata&&d->metadata->nickname[0]){DiscoveryText w={out,capacity,&used};device_json_write_string(discovery_text,&w,d->metadata->nickname);}else json(out,capacity,&used,"null");
            json(out,capacity,&used,"}");
        }
    }
    json(out,capacity,&used,"]}"); return used<capacity?used:0;
}
