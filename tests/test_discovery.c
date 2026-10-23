#include "device/discovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
typedef struct { uint8_t bytes[1024],ip[4]; size_t size; uint16_t port; unsigned count; } Wire;
static bool send(void *ctx,const uint8_t *b,size_t n,const uint8_t ip[4],uint16_t port) {
    Wire *w=ctx; CHECK(n<=sizeof(w->bytes)); memcpy(w->bytes,b,n); memcpy(w->ip,ip,4); w->size=n; w->port=port; w->count++; return true;
}
static void start(Discovery *d,Wire *w,unsigned number,uint16_t port) {
    char host[32],id[32]; snprintf(host,sizeof(host),"lightvm-%06u",number); snprintf(id,sizeof(id),"test-%u",number);
    const uint8_t ip[4]={192,168,1,(uint8_t)number}; DeviceHost identity={.controller_id=id,.board="pico_w",.version="0.1.0",.kind="light_controller",.capabilities=3}; discovery_start(d,host,&identity,ip,port,send,w,0); CHECK(d->active);
}
static void announce(Discovery *d) {
    discovery_tick(d,100000); discovery_tick(d,350000); discovery_tick(d,600000); discovery_tick(d,850000); CHECK(d->announced);
}
static size_t skip_name(const uint8_t *b,size_t n) { while (b[n]) { if ((b[n]&0xc0)==0xc0) return n+2; n+=b[n]+1; } return n+1; }
static size_t question(uint8_t *b,const char *name,unsigned type,unsigned klass,unsigned id) {
    memset(b,0,1024); b[0]=(uint8_t)(id>>8); b[1]=(uint8_t)id; b[5]=1; size_t n=12;
    while (*name) { const char *dot=strchr(name,'.'); size_t length=dot?(size_t)(dot-name):strlen(name); b[n++]=(uint8_t)length; memcpy(b+n,name,length); n+=length; name+=length; if (*name) name++; }
    b[n++]=0; b[n++]=(uint8_t)(type>>8); b[n++]=(uint8_t)type; b[n++]=(uint8_t)(klass>>8); b[n++]=(uint8_t)klass; return n;
}
int main(void) {
    static Discovery a,b,c; Wire wa={0},wb={0},wc={0}; char json[4096];
    start(&a,&wa,1,8080); start(&b,&wb,2,80); announce(&a); announce(&b);
    CHECK(wa.bytes[2]==0x84 && wa.bytes[7]==4);
    discovery_receive(&b,wa.bytes,wa.size,a.controllers[0].ip,5353,1000000);
    CHECK(discovery_json(&b,json,sizeof(json),1000000));
    CHECK(strstr(json,"test-1") && strstr(json,"test-2") && strstr(json,"http://192.168.1.1:8080"));
    discovery_receive(&b,wa.bytes,wa.size,a.controllers[0].ip,5353,2000000);
    CHECK(b.controllers[1].hostname[0] && !b.controllers[2].hostname[0]);
    CHECK(discovery_json(&b,json,sizeof(json),32000001) && !strstr(json,"test-1"));
    discovery_receive(&b,wa.bytes,wa.size,a.controllers[0].ip,5353,33000000);
    discovery_stop(&a,34000000); CHECK(!a.active);
    discovery_receive(&b,wa.bytes,wa.size,(uint8_t[]){192,168,1,1},5353,34000000);
    CHECK(discovery_json(&b,json,sizeof(json),34000000) && !strstr(json,"test-1") && strstr(json,"test-2"));
    start(&a,&wa,1,8080); announce(&a);
    for (size_t n=0;n<wa.size;n++) {
        start(&c,&wc,3,80); discovery_receive(&c,wa.bytes,n,a.controllers[0].ip,5353,1000000); CHECK(!c.controllers[1].hostname[0]);
    }
    // Compress the final A owner to the previous SRV target. Real mDNS packets
    // use compression across records; PTR/SRV/TXT/address order is independent.
    Wire compressed=wa; size_t at=12,target=0,owner=0,owner_end=0;
    for (unsigned i=0;i<4;i++) {
        owner=at; owner_end=skip_name(compressed.bytes,at); at=owner_end;
        size_t length=((size_t)compressed.bytes[at+8]<<8)|compressed.bytes[at+9];
        if (i==1) target=at+16;
        at+=10+length;
    }
    memmove(compressed.bytes+owner+2,compressed.bytes+owner_end,compressed.size-owner_end);
    compressed.bytes[owner]=(uint8_t)(0xc0|(target>>8)); compressed.bytes[owner+1]=(uint8_t)target;
    compressed.size-=owner_end-owner-2;
    start(&c,&wc,3,80); discovery_receive(&c,compressed.bytes,compressed.size,a.controllers[0].ip,5353,1000000);
    CHECK(discovery_json(&c,json,sizeof(json),1000000) && strstr(json,"test-1"));
    uint8_t q[1024]; size_t n=question(q,"lightvm.local",1,1,123);
    unsigned before=wb.count; discovery_receive(&b,q,n,(uint8_t[]){192,168,1,99},12345,35000000);
    CHECK(wb.count==before+1 && wb.port==12345 && wb.ip[3]==99);
    CHECK(wb.bytes[1]==123 && wb.bytes[5]==1 && wb.bytes[7]==5);
    at=skip_name(wb.bytes,12)+4;
    for (unsigned i=0;i<5;i++) { at=skip_name(wb.bytes,at); CHECK(wb.bytes[at+2]==0 && wb.bytes[at+3]==1); CHECK(wb.bytes[at+7]<=10); at+=10+(((size_t)wb.bytes[at+8]<<8)|wb.bytes[at+9]); }
    n=question(q,"lightvm.local",1,1,0); before=wb.count;
    discovery_receive(&b,q,n,(uint8_t[]){192,168,1,99},5353,36000000);
    CHECK(wb.count==before && b.reply_pending);
    discovery_tick(&b,36200000); CHECK(!b.reply_pending && wb.port==5353 && wb.ip[0]==224 && wb.bytes[7]==5);
    // The last shared alias record must never flush other LightVM addresses.
    at=12; for (unsigned i=0;i<4;i++) { at=skip_name(wb.bytes,at); at+=10+(((size_t)wb.bytes[at+8]<<8)|wb.bytes[at+9]); }
    at=skip_name(wb.bytes,at); CHECK(wb.bytes[at+2]==0 && wb.bytes[at+3]==1);
    memset(q,0,sizeof(q)); q[5]=1; q[12]=0xc0; q[13]=12;
    before=wb.count; discovery_receive(&b,q,18,(uint8_t[]){192,168,1,99},12345,37000000); CHECK(wb.count==before);
    start(&b,&wb,2,80);
    for (unsigned i=10;i<20;i++) { start(&a,&wa,i,80); announce(&a); discovery_receive(&b,wa.bytes,wa.size,a.controllers[0].ip,5353,1000000); }
    CHECK(b.truncated); CHECK(discovery_json(&b,json,sizeof(json),1000000) && strstr(json,"\"truncated\":true"));
    CHECK(discovery_json(&b,json,sizeof(json),31000001) && !strstr(json,"test-10"));
    CHECK(!discovery_json(&b,json,10,31000001));
    start(&a,&wa,1,80); announce(&a); start(&c,&wc,1,80); c.controllers[0].ip[3]=99;
    discovery_receive(&c,wa.bytes,wa.size,a.controllers[0].ip,5353,1000000);
    CHECK(c.collision==1 && strcmp(c.controllers[0].hostname,a.controllers[0].hostname) && !c.announced);
    // Known answers suppress redundant multicast replies. The A record in the
    // answer section of a query is a cache entry, never a competing name probe.
    start(&b,&wb,2,80); announce(&b); Wire saved=wb;
    n=question(q,"_lightvm._tcp.local",12,1,0); memcpy(q+n,saved.bytes+12,saved.size-12); n+=saved.size-12; q[7]=4;
    before=wb.count; discovery_receive(&b,q,n,(uint8_t[]){192,168,1,99},5353,1000000);
    CHECK(wb.count==before && !b.reply_pending && !b.collision);
    // Exercise length/type/pointer mutations under ASan/UBSan as well.
    for (size_t i=0;i<saved.size;i++) for (unsigned bit=0;bit<8;bit++) {
        Wire changed=saved; changed.bytes[i]^=(uint8_t)(1u<<bit); start(&c,&wc,3,80);
        discovery_receive(&c,changed.bytes,changed.size,(uint8_t[]){192,168,1,2},5353,1000000);
        CHECK(discovery_json(&c,json,sizeof(json),1000000));
    }
    // Shared service peers can be audio analysers with independent product names.
    DeviceHost audio={.controller_id="audio-1234",.board="xiao_esp32s3",.version="0.1.0-dev",.kind="audio_analyser",.capabilities=13};
    start(&b,&wb,2,80);
    discovery_start(&a,"kaj-fi-123456",&audio,(uint8_t[]){192,168,1,42},80,send,&wa,0);announce(&a);
    discovery_receive(&b,wa.bytes,wa.size,a.controllers[0].ip,5353,1000000);
    CHECK(discovery_json(&b,json,sizeof(json),1000000));
    CHECK(strstr(json,"kaj-fi-123456.local") && strstr(json,"audio_analyser") && strstr(json,"\"capabilities\":13") && strstr(json,"0.1.0-dev"));
    puts("bounded DNS-SD discovery, expiry, goodbye, aliases and malformed packets: passed");
    return 0;
}
