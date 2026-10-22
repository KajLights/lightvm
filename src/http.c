#include "http.h"
#include "http_setup.h"
#if LIGHTVM_WEBSOCKET
#include "websocket.h"
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <limits.h>
static bool request_word(const char **cursor,char *out,size_t capacity){
    const char *p=*cursor;p+=strspn(p," \t\r\n\v\f");size_t n=strcspn(p," \t\r\n\v\f");
    if(!n||n>=capacity)return false;
    memcpy(out,p,n);out[n]=0;*cursor=p+n;return true;
}
static void capture_bytes(void *context,const uint8_t *bytes,size_t size) {
    Http *h=context;
    if (size>sizeof(h->response)-h->used) { h->overflow=true; return; }
    memcpy(h->response+h->used,bytes,size); h->used+=size;
}
static void capture(void *context,uint8_t kind,uint16_t id,const uint8_t *bytes,size_t size) { write_frame(capture_bytes,context,kind,id,bytes,size); }
void http_init(Http *h,App *a) { memset(h,0,sizeof(*h)); h->app=a; }
bool http_submit(Http *h,const HttpRequest *r) {
    if (h->count==HTTP_QUEUE) return false;
    h->queue[(h->head+h->count)%HTTP_QUEUE]=*r; h->count++; return true;
}
static void expire(Http *h,uint64_t now) {
    for (unsigned i=0;i<HTTP_SESSIONS;i++) if (h->sessions[i].source && now>=h->sessions[i].expires) {
        app_disconnect(h->app,h->sessions[i].source); memset(&h->sessions[i],0,sizeof(h->sessions[i]));
    }
}
void http_disconnect_sessions(Http *h) {
    for (unsigned i=0;i<HTTP_SESSIONS;i++) { app_disconnect(h->app,h->sessions[i].source); memset(&h->sessions[i],0,sizeof(h->sessions[i])); }
}
static HttpSession *session(Http *h,const char *token,uint64_t now) {
    for (unsigned i=0;i<HTTP_SESSIONS;i++) if (h->sessions[i].source && !strcmp(token,h->sessions[i].token)) { h->sessions[i].expires=now+300000000u; return &h->sessions[i]; }
    return NULL;
}
static unsigned command(Http *h,HttpRequest *r,uint64_t source) {
    const uint8_t *b=r->body;
    if (r->size<HEADER_SIZE || memcmp(b,"LVM1",4) || b[4]!=LIGHTVM_PROTOCOL_VERSION || read_u32(b+8)>FRAME_LIMIT || r->size!=HEADER_SIZE+read_u32(b+8) || b[5]>=RESP_STATUS) return 400;
    uint32_t crc=crc32_update(0xffffffffu,b+4,8); crc=crc32_update(crc,b+16,r->size-16)^0xffffffffu;
    if (crc!=read_u32(b+12)) return 400;
    h->reply_source=source;
    app_dispatch(h->app,source,capture,h,b[5],(uint16_t)(b[6]|b[7]<<8),b+16,r->size-16);
    return h->overflow?500:200;
}
static unsigned execute(Http *h,HttpRequest *r,uint64_t now) {
    if (r->error) return r->error;
    if (r->discovery_only && strcmp(r->path,"/api/v1/controllers")) return 404;
#if LIGHTVM_WEBSOCKET
    if (r->websocket_action) {
        WebSocket *w=r->websocket;
        if (!w || w->disconnected) return 400;
        if (r->websocket_action==WS_UPGRADE) {
            if (w->source || h->app->connection_sequence==UINT64_MAX) return 503;
            w->source=++h->app->connection_sequence; return 101;
        }
        if (!w->upgraded || !w->source) return 400;
        if (r->websocket_action==WS_BINARY) return command(h,r,w->source);
        memcpy(h->response,r->body,r->size); h->used=r->size; return 200;
    }
#endif
    if (!strcmp(r->method,"OPTIONS")) return 204;
    if (LIGHTVM_WIFI && (!strcmp(r->path,"/") || !strcmp(r->path,"/setup"))) return http_setup(h,r);
    if (!strcmp(r->path,"/api/v1/controllers")) {
        if (strcmp(r->method,"GET")) return 405;
        if (r->size) return 400;
        h->used=discovery_json(h->discovery,(char *)h->response,sizeof(h->response),now);
        return h->used?200:500;
    }
    if (!strcmp(r->method,"POST") && !strcmp(r->path,"/api/v1/sessions")) {
        if (r->size) return 400;
        HttpSession *s=NULL;
        for (unsigned i=0;i<HTTP_SESSIONS;i++) if (!h->sessions[i].source) { s=&h->sessions[i]; break; }
        if (!s || h->app->connection_sequence==UINT64_MAX) return 503;
        s->source=++h->app->connection_sequence; s->expires=now+300000000u;
        snprintf(s->token,sizeof(s->token),"%016llx%016llx",(unsigned long long)crc32_bytes((const uint8_t *)h->app->boot_id,strlen(h->app->boot_id)),(unsigned long long)s->source);
        int n=snprintf((char *)h->response,sizeof(h->response),"{\"session_id\":\"%s\",\"controller_id\":\"%s\",\"boot_id\":\"%s\",\"expires_in_ms\":300000}",s->token,h->app->controller_id,h->app->boot_id);
        h->used=(size_t)n; return 201;
    }
    if (!strcmp(r->method,"DELETE") && !strncmp(r->path,"/api/v1/sessions/",17)) {
        HttpSession *s=session(h,r->path+17,now); if (!s) return 404;
        app_disconnect(h->app,s->source); memset(s,0,sizeof(*s)); return 204;
    }
    if (strcmp(r->path,"/api/v1/command")) return 404;
    if (strcmp(r->method,"POST")) return 405;
    HttpSession *s=session(h,r->token,now); if (!s) return 404;
    return command(h,r,s->source);
}
static void headers(Http *h,unsigned code,bool binary) {
#if LIGHTVM_WEBSOCKET
    HttpRequest *r=&h->active; h->websocket_keep_open=false;
    if (r->websocket_action==WS_UPGRADE && code==101) {
        char accept[29]; websocket_accept(r->token,accept,sizeof(accept));
        h->header_size=(size_t)snprintf(h->header,sizeof(h->header),"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept);
        h->websocket_keep_open=true; return;
    }
    if (r->websocket_action && r->websocket_action!=WS_UPGRADE) {
        uint8_t opcode=r->websocket_action==WS_PING?WS_PONG:r->websocket_action;
        if (code>=400) { opcode=WS_CLOSE; unsigned reason=code==500?1011:1002; h->response[0]=(uint8_t)(reason>>8); h->response[1]=(uint8_t)reason; h->used=2; }
        h->header_size=websocket_frame_header((uint8_t *)h->header,opcode,h->used);
        h->websocket_keep_open=opcode!=WS_CLOSE; return;
    }
#endif
    const char *reason=code==200?"OK":code==201?"Created":code==204?"No Content":code==400?"Bad Request":code==403?"Forbidden":code==404?"Not Found":code==405?"Method Not Allowed":code==409?"Conflict":code==413?"Payload Too Large":code==415?"Unsupported Media Type":
#if LIGHTVM_WEBSOCKET
        code==426?"Upgrade Required":
#endif
        code==503?"Service Unavailable":"Internal Server Error";
    h->header_size=(size_t)snprintf(h->header,sizeof(h->header),"HTTP/1.1 %u %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
#if LIGHTVM_WEBSOCKET
        "Connection: %s\r\n"
#else
        "Connection: close\r\n"
#endif
        "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, X-LightVM-Session\r\n"
#if LIGHTVM_WEBSOCKET
        "%s"
#endif
        "\r\n",code,reason,h->html?"text/html; charset=utf-8":binary?"application/octet-stream":"application/json",(unsigned long)h->used
#if LIGHTVM_WEBSOCKET
        ,code==426?"close, Upgrade":"close"
        ,code==426?"Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n":""
#endif
    );
}
void http_tick(Http *h,uint64_t now) {
    if(h->executing||h->app->command_active)return;
    expire(h,now);
    if (!h->sending && h->count) {
        h->active=h->queue[h->head]; h->head=(h->head+1)%HTTP_QUEUE; h->count--;
        h->used=0; h->sent=0; h->header_sent=0; h->overflow=false; h->reply_source=0; h->html=false;
        h->executing=true;unsigned code=execute(h,&h->active,now);h->executing=false;
        if (code>=400 && !h->html) { h->used=(size_t)snprintf((char *)h->response,sizeof(h->response),"{\"error\":%u}",code); }
        headers(h,code,code==200 && !strcmp(h->active.path,"/api/v1/command")); h->deadline=now+2000000u; h->sending=true; h->closing=false;
    }
    if (!h->sending) return;
    const uint8_t *bytes=NULL; size_t count=0;
    if (h->header_sent<h->header_size) { bytes=(const uint8_t *)h->header+h->header_sent; count=h->header_size-h->header_sent; }
    else if (h->sent<h->used) { bytes=h->response+h->sent; count=h->used-h->sent; }
    if (count>512) count=512;
    if (count && !h->closing && now<h->deadline) {
        int n=h->active.io.write(h->active.io.context,bytes,count);
        if (n>0 && (size_t)n<=count) { if (h->header_sent<h->header_size) h->header_sent+=(size_t)n; else h->sent+=(size_t)n; h->deadline=now+2000000u; }
        else if (n<0) h->deadline=now;
    }
    bool complete=h->header_sent==h->header_size && h->sent==h->used;
#if LIGHTVM_WEBSOCKET
    if (complete && !h->closing && now<h->deadline && h->websocket_keep_open) {
        WebSocket *w=h->active.websocket;
        if (!w->flush || w->flush(w->io.context)) {
            if (h->active.websocket_action==WS_UPGRADE) w->upgraded=true;
            app_reply_delivered(h->app,h->reply_source); h->sending=false;
        }
        return;
    }
#endif
    if (complete || now>=h->deadline) {
        h->closing=true;
        bool closed=h->active.io.close(h->active.io.context);
        if (closed) {
            if (closed && complete) app_reply_delivered(h->app,h->reply_source);
            h->sending=false;
        }
    }
}
void http_cancel_io(Http *h,void *context) {
    if (h->sending && h->active.io.context==context) h->sending=false;
    HttpRequest kept[HTTP_QUEUE]; unsigned count=0;
    for (unsigned i=0;i<h->count;i++) { HttpRequest *r=&h->queue[(h->head+i)%HTTP_QUEUE]; if (r->io.context!=context) kept[count++]=*r; }
    memcpy(h->queue,kept,count*sizeof(*kept)); h->count=count; h->head=0;
}
bool http_input(HttpInput *input,const uint8_t *bytes,size_t size,HttpRequest *r) {
    if (input->ready) return false;
    if (size>sizeof(input->bytes)-input->used) { r->error=413; input->ready=true; return true; }
    memcpy(input->bytes+input->used,bytes,size); input->used+=size;
    size_t end=0;
    for (size_t i=3;i<input->used;i++) if (!memcmp(input->bytes+i-3,"\r\n\r\n",4)) { end=i+1; break; }
    if (!end) { if (input->used>=2048) { r->error=400; input->ready=true; return true; } return false; }
    if (end>2048 || memchr(input->bytes,0,end)) { r->error=400; input->ready=true; return true; }
    char header[2049]; memcpy(header,input->bytes,end); header[end]=0;
    char *line=strstr(header,"\r\n"); if (!line) { r->error=400; input->ready=true; return true; } *line=0;
    char version[16];
    const char *request_line=header;
    if(!request_word(&request_line,r->method,sizeof(r->method))||!request_word(&request_line,r->path,sizeof(r->path))||
       !request_word(&request_line,version,sizeof(version))||request_line[strspn(request_line," \t\r\n\v\f")]||strcmp(version,"HTTP/1.1"))r->error=400;
    bool length_seen=false,token_seen=false,media=false,form=false,host_seen=false,origin_seen=false; size_t length=0;
    bool setup_post=!strcmp(r->method,"POST") && !strcmp(r->path,"/setup");
    char host[96]="",origin[112]="";
#if LIGHTVM_WEBSOCKET
    WebSocketHandshake handshake={0};
    bool upgrading=!strcmp(r->path,WEBSOCKET_PATH);
#endif
    for (char *field=line+2;*field;) {
        char *next=strstr(field,"\r\n"); if (!next) break; *next=0;
        char *colon=strchr(field,':'); if (!colon) { if (*field) r->error=400; break; } *colon=0;
        char *value=colon+1; while (*value==' ' || *value=='\t') value++;
        char *tail=value+strlen(value); while (tail>value && (tail[-1]==' ' || tail[-1]=='\t')) *--tail=0;
#if LIGHTVM_WEBSOCKET
        if (upgrading) websocket_header(&handshake,field,value);
#endif
        if (!strcasecmp(field,"Content-Length")) {
            if (length_seen || !*value) r->error=400;
            length_seen=true;
            for (char *p=value;*p;p++) { if (*p<'0' || *p>'9' || length>528) { r->error=413; break; } length=length*10+(unsigned)(*p-'0'); }
        } else if (!strcasecmp(field,"Transfer-Encoding")) r->error=400;
        else if (!strcasecmp(field,"X-LightVM-Session")) { if (token_seen || strlen(value)>=sizeof(r->token)) r->error=400; else strcpy(r->token,value); token_seen=true; }
        else if (!strcasecmp(field,"Content-Type")) { media=!strcmp(value,"application/octet-stream"); form=!strcmp(value,"application/x-www-form-urlencoded"); }
        else if (setup_post && !strcasecmp(field,"Host")) { if(host_seen || strlen(value)>=sizeof(host))r->error=400; else strcpy(host,value);host_seen=true; }
        else if (setup_post && !strcasecmp(field,"Origin")) { if(origin_seen || strlen(value)>=sizeof(origin))r->error=400; else strcpy(origin,value);origin_seen=true; }
        field=next+2;
    }
    if (length>sizeof(r->body)) r->error=413;
    if (!strcmp(r->method,"POST") && !strcmp(r->path,"/api/v1/command") && (!media || !length_seen)) r->error=media?400:415;
    if (setup_post) {
        if (!form || !length_seen) r->error=form?400:415;
        r->form_origin_valid=http_setup_origin(host,origin);
    }
    if (r->error) { input->ready=true; return true; }
    if (input->used<end+length) return false;
    if (input->used!=end+length) r->error=400;
    r->size=length; memcpy(r->body,input->bytes+end,length);
#if LIGHTVM_WEBSOCKET
    if (upgrading && !r->error) r->error=websocket_upgrade(r,&handshake);
#endif
    input->ready=true; return true;
}
