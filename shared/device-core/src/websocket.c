#include "device/websocket.h"
#include <string.h>
#include <strings.h>

static bool token(const char *value,const char *wanted) {
    while (*value) {
        while (*value==' ' || *value=='\t' || *value==',') value++;
        const char *end=strchr(value,','); if (!end) end=value+strlen(value);
        const char *tail=end; while (tail>value && (tail[-1]==' ' || tail[-1]=='\t')) tail--;
        if ((size_t)(tail-value)==strlen(wanted) && !strncasecmp(value,wanted,(size_t)(tail-value))) return true;
        value=*end?end+1:end;
    }
    return false;
}
static int base64_value(char c) {
    if (c>='A' && c<='Z') return c-'A';
    if (c>='a' && c<='z') return c-'a'+26;
    if (c>='0' && c<='9') return c-'0'+52;
    return c=='+'?62:c=='/'?63:-1;
}
void websocket_header(WebSocketHandshake *h,const char *name,const char *value) {
    unsigned bit=0;
    if (!strcasecmp(name,"Host")) { bit=1; h->host=*value!=0; }
    else if (!strcasecmp(name,"Upgrade")) { bit=2; h->upgrade=token(value,"websocket"); }
    else if (!strcasecmp(name,"Connection")) { bit=4; h->connection=token(value,"upgrade"); }
    else if (!strcasecmp(name,"Sec-WebSocket-Version")) { bit=8; h->version=!strcmp(value,"13"); }
    else if (!strcasecmp(name,"Sec-WebSocket-Key")) {
        bit=16;
        if (strlen(value)!=24 || value[22]!='=' || value[23]!='=') h->invalid=true;
        else {
            for (unsigned i=0;i<22;i++) if (base64_value(value[i])<0) h->invalid=true;
            if ((base64_value(value[21])&15)!=0) h->invalid=true;
            memcpy(h->key,value,25);
        }
    }
    if (bit && (h->seen&bit)) h->invalid=true;
    h->seen|=bit;
}
unsigned websocket_upgrade(HttpRequest *r,const WebSocketHandshake *h) {
    if (strcmp(r->method,"GET") || r->size || h->invalid || h->seen!=31 || !h->host || !h->upgrade || !h->connection) return 400;
    if (!h->version) return 426;
    memcpy(r->token,h->key,sizeof(h->key)); r->websocket_action=WS_UPGRADE; return 0;
}
static uint32_t rotate(uint32_t value,unsigned bits) { return (value<<bits)|(value>>(32-bits)); }
// SHA-1 is used solely for RFC 6455's fixed 60-byte handshake input, not for
// authentication. Two blocks and a 16-word rolling schedule bound stack use.
size_t websocket_accept(const char *key,char *out,size_t capacity) {
    if (capacity<29 || strlen(key)!=24) return 0;
    uint8_t input[128]={0}; memcpy(input,key,24); memcpy(input+24,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11",36);
    input[60]=0x80; input[126]=1; input[127]=0xe0;
    uint32_t hash[5]={0x67452301u,0xefcdab89u,0x98badcfeu,0x10325476u,0xc3d2e1f0u};
    for (unsigned block=0;block<2;block++) {
        uint32_t words[16];
        for (unsigned i=0;i<16;i++) { const uint8_t *p=input+block*64+i*4; words[i]=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
        uint32_t a=hash[0],b=hash[1],c=hash[2],d=hash[3],e=hash[4];
        for (unsigned i=0;i<80;i++) {
            if (i>=16) words[i&15]=rotate(words[(i-3)&15]^words[(i-8)&15]^words[(i-14)&15]^words[i&15],1);
            uint32_t f,k;
            if (i<20) { f=(b&c)|(~b&d); k=0x5a827999u; }
            else if (i<40) { f=b^c^d; k=0x6ed9eba1u; }
            else if (i<60) { f=(b&c)|(b&d)|(c&d); k=0x8f1bbcdcu; }
            else { f=b^c^d; k=0xca62c1d6u; }
            uint32_t next=rotate(a,5)+f+e+k+words[i&15]; e=d; d=c; c=rotate(b,30); b=a; a=next;
        }
        hash[0]+=a; hash[1]+=b; hash[2]+=c; hash[3]+=d; hash[4]+=e;
    }
    uint8_t digest[20]; for (unsigned i=0;i<20;i++) digest[i]=(uint8_t)(hash[i/4]>>(24-8*(i%4)));
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t used=0;
    for (unsigned i=0;i<20;i+=3) {
        uint32_t v=(uint32_t)digest[i]<<16; if (i+1<20) v|=(uint32_t)digest[i+1]<<8; if (i+2<20) v|=digest[i+2];
        out[used++]=alphabet[v>>18]; out[used++]=alphabet[(v>>12)&63]; out[used++]=i+1<20?alphabet[(v>>6)&63]:'='; out[used++]=i+2<20?alphabet[v&63]:'=';
    }
    out[used]=0; return used;
}
size_t websocket_frame_header(uint8_t *out,uint8_t opcode,size_t size) {
    out[0]=(uint8_t)(0x80|opcode);
    if (size<126) { out[1]=(uint8_t)size; return 2; }
    if (size<=UINT16_MAX) { out[1]=126; out[2]=(uint8_t)(size>>8); out[3]=(uint8_t)size; return 4; }
    out[1]=127; for (unsigned i=0;i<8;i++) out[2+i]=(uint8_t)((uint64_t)size>>(56-8*i)); return 10;
}
void websocket_init(WebSocket *w,Http *http,HttpIO io,bool (*flush)(void *)) {
    memset(w,0,sizeof(*w)); w->http=http; w->io=io; w->flush=flush;
}
void websocket_closed(WebSocket *w) {
    if (!w->http) return;
    http_cancel_io(w->http,w->io.context); w->disconnected=true; w->upgraded=false;
    websocket_tick(w,0);
}
static bool submit(WebSocket *w,uint8_t action,const uint8_t *bytes,size_t size) {
    HttpRequest r={0}; r.io=w->io; r.websocket=w; r.websocket_action=action; r.size=size;
    if (size) memcpy(r.body,bytes,size);
    return http_submit(w->http,&r);
}
static bool utf8(const uint8_t *p,size_t size) {
    uint32_t value=0,minimum=0; unsigned remaining=0;
    for (size_t i=0;i<size;i++) {
        uint8_t c=p[i];
        if (remaining) {
            if ((c&0xc0)!=0x80) return false;
            value=value<<6|(c&63); remaining--;
            if (!remaining && (value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff))) return false;
        } else if (c<128) continue;
        else if (c>=0xc2 && c<=0xdf) { remaining=1; value=c&31; minimum=0x80; }
        else if (c>=0xe0 && c<=0xef) { remaining=2; value=c&15; minimum=0x800; }
        else if (c>=0xf0 && c<=0xf4) { remaining=3; value=c&7; minimum=0x10000; }
        else return false;
    }
    return remaining==0;
}
static void finish_frame(WebSocket *w) {
    if (w->opcode==WS_CLOSE) {
        if (w->frame_size==1) { w->failure=1002; return; }
        if (w->frame_size>=2) {
            unsigned code=(unsigned)w->control[0]*256+w->control[1];
            if (!((code>=1000 && code<=1014 && code!=1004 && code!=1005 && code!=1006) || (code>=3000 && code<=4999))) { w->failure=1002; return; }
            if (!utf8(w->control+2,w->frame_size-2)) { w->failure=1007; return; }
        }
        if (submit(w,WS_CLOSE,w->control,w->frame_size)) w->close_queued=true; else w->failure=1013;
    } else if (w->opcode==WS_PING) {
        if (!submit(w,WS_PING,w->control,w->frame_size)) w->failure=1013;
    } else if (w->opcode!=WS_PONG && w->final) {
        if (!submit(w,WS_BINARY,w->message,w->message_used)) w->failure=1013;
        w->fragmented=false; w->message_used=0;
    }
    w->header_used=0; w->header_size=0; w->frame_used=0;
}
static bool parse_header(WebSocket *w) {
    const uint8_t *h=w->header; w->opcode=h[0]&15; w->final=(h[0]&128)!=0;
    if ((h[0]&0x70) || !(h[1]&128) || (w->opcode!=0 && w->opcode!=1 && w->opcode!=2 && w->opcode!=8 && w->opcode!=9 && w->opcode!=10)) { w->failure=1002; return false; }
    size_t size=h[1]&127;
    if (size==126) { size=(size_t)h[2]*256+h[3]; if (size<126) { w->failure=1002; return false; } }
    else if (size==127) {
        if (h[2]&128) { w->failure=1002; return false; }
        // Every canonical 64-bit encoding exceeds this module's message limit.
        bool large=false; for (unsigned i=2;i<8;i++) if (h[i]) large=true;
        w->failure=large?1009:1002; return false;
    }
    if (w->opcode>=8) {
        if (!w->final || size>125) { w->failure=1002; return false; }
    } else {
        if ((w->opcode==0)!=w->fragmented) { w->failure=1002; return false; }
        if (w->opcode==1) { w->failure=1003; return false; }
        if (size>sizeof(w->message)-w->message_used) { w->failure=1009; return false; }
        if (!w->final) w->fragmented=true;
    }
    w->frame_size=size; w->frame_used=0;
    return true;
}
void websocket_feed(WebSocket *w,const uint8_t *bytes,size_t size,uint64_t now) {
    if (!w->upgraded || w->disconnected || w->failure || w->close_queued) return;
    for (size_t i=0;i<size && !w->failure && !w->close_queued;i++) {
        w->last_input=now;
        if (w->header_used<2 || w->header_used<w->header_size) {
            w->header[w->header_used++]=bytes[i];
            if (w->header_used==2) {
                uint8_t opcode=w->header[0]&15;
                if ((w->header[0]&0x70) || !(w->header[1]&128) ||
                    (opcode!=0 && opcode!=1 && opcode!=2 && opcode!=8 && opcode!=9 && opcode!=10) ||
                    (opcode>=8 && (!(w->header[0]&128) || (w->header[1]&127)>=126))) { w->failure=1002; break; }
                w->header_size=6+((w->header[1]&127)==126?2:(w->header[1]&127)==127?8:0);
            }
            if (w->header_used==w->header_size && parse_header(w) && !w->frame_size) finish_frame(w);
            continue;
        }
        uint8_t byte=bytes[i]^w->header[w->header_size-4+(w->frame_used&3)];
        if (w->opcode>=8) w->control[w->frame_used]=byte; else w->message[w->message_used++]=byte;
        if (++w->frame_used==w->frame_size) finish_frame(w);
    }
}
void websocket_tick(WebSocket *w,uint64_t now) {
    if (!w->http) return;
    if (w->disconnected) {
        if (w->source && !w->http->host->busy(w->http->host->context)) { w->http->host->disconnect(w->http->host->context,w->source); w->source=0; }
        return;
    }
    if (!w->upgraded || w->close_queued) return;
    if (!w->failure && (w->header_used || w->fragmented) && now>=w->last_input && now-w->last_input>=2000000u) w->failure=1002;
    if (w->failure) {
        uint8_t code[2]={(uint8_t)(w->failure>>8),(uint8_t)w->failure};
        if (submit(w,WS_CLOSE,code,sizeof(code))) w->close_queued=true;
    }
}
