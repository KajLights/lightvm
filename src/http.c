#include "http.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <limits.h>
static size_t response_capacity(Http *h) {
#if LIGHTVM_FLASH_EXPERIMENT
    return h->capacity;
#else
    return sizeof(h->response);
#endif
}
static bool response_reserve(Http *h,size_t size) {
#if LIGHTVM_FLASH_EXPERIMENT
    if(size>HTTP_RESPONSE_LIMIT) return false;
    if(size<=h->capacity) return true;
    size_t capacity=(size+511u)&~511u;
    if(capacity>HTTP_RESPONSE_LIMIT) capacity=HTTP_RESPONSE_LIMIT;
    uint8_t *p=platform_realloc(h->response==h->emergency?NULL:h->response,capacity);
    if(!p) return false;
    if(h->response==h->emergency) memcpy(p,h->emergency,h->used);
    h->response=p; h->capacity=capacity; return true;
#else
    return size<=sizeof(h->response);
#endif
}
static void response_release(Http *h) {
#if LIGHTVM_FLASH_EXPERIMENT
    if(h->response!=h->emergency) platform_free(h->response);
    h->response=h->emergency; h->capacity=sizeof(h->emergency);
#else
    (void)h;
#endif
}
static void capture_bytes(void *context,const uint8_t *bytes,size_t size) {
    Http *h=context;
    if (size>HTTP_RESPONSE_LIMIT-h->used || !response_reserve(h,h->used+size)) { h->overflow=true; return; }
    memcpy(h->response+h->used,bytes,size); h->used+=size;
}
static void capture(void *context,uint8_t kind,uint16_t id,const uint8_t *bytes,size_t size) { write_frame(capture_bytes,context,kind,id,bytes,size); }
void http_init(Http *h,App *a) { memset(h,0,sizeof(*h)); h->app=a; h->sequence=a->connection_sequence; response_release(h); }
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
static unsigned execute(Http *h,HttpRequest *r,uint64_t now) {
    if (r->error) return r->error;
    if (r->discovery_only && strcmp(r->path,"/api/v1/devices")) return 404;
    if (!strcmp(r->method,"OPTIONS")) return 204;
    if (!strcmp(r->path,"/api/v1/devices")) {
        if (strcmp(r->method,"GET")) return 405;
        if (r->size) return 400;
        if(!response_reserve(h,8192)) return 503;
        h->used=discovery_json(h->discovery,(char *)h->response,response_capacity(h),now);
        return h->used?200:500;
    }
    if (!strcmp(r->method,"POST") && !strcmp(r->path,"/api/v1/sessions")) {
        if (r->size) return 400;
        HttpSession *s=NULL;
        for (unsigned i=0;i<HTTP_SESSIONS;i++) if (!h->sessions[i].source) { s=&h->sessions[i]; break; }
        if (!s || h->sequence==UINT64_MAX) return 503;
        s->source=++h->sequence; h->app->connection_sequence=h->sequence; s->expires=now+300000000u;
        snprintf(s->token,sizeof(s->token),"%016llx%016llx",(unsigned long long)crc32_bytes((const uint8_t *)h->app->boot_id,strlen(h->app->boot_id)),(unsigned long long)s->source);
        if(!response_reserve(h,512)) { memset(s,0,sizeof(*s)); return 503; }
        int n=snprintf((char *)h->response,response_capacity(h),"{\"session_id\":\"%s\",\"device_id\":\"%s\",\"boot_id\":\"%s\",\"expires_in_ms\":300000}",s->token,h->app->device_id,h->app->boot_id);
        h->used=(size_t)n; return 201;
    }
    if (!strcmp(r->method,"DELETE") && !strncmp(r->path,"/api/v1/sessions/",17)) {
        HttpSession *s=session(h,r->path+17,now); if (!s) return 404;
        app_disconnect(h->app,s->source); memset(s,0,sizeof(*s)); return 204;
    }
    if (strcmp(r->path,"/api/v1/command")) return 404;
    if (strcmp(r->method,"POST")) return 405;
    HttpSession *s=session(h,r->token,now); if (!s) return 404;
    const uint8_t *b=r->body;
    if (r->size<HEADER_SIZE || memcmp(b,"WASM",4) || b[4]!=LIGHTVM_PROTOCOL_VERSION || read_u32(b+8)>FRAME_LIMIT || r->size!=HEADER_SIZE+read_u32(b+8) || b[5]>=RESP_OUTPUT) return 400;
    uint32_t crc=crc32_update(0xffffffffu,b+4,8); crc=crc32_update(crc,b+16,r->size-16)^0xffffffffu;
    if (crc!=read_u32(b+12)) return 400;
    h->reply_source=s->source;
    app_dispatch(h->app,s->source,capture,h,b[5],(uint16_t)(b[6]|b[7]<<8),b+16,r->size-16);
    return h->overflow?500:200;
}
static void headers(Http *h,unsigned code,bool binary) {
    const char *reason=code==200?"OK":code==201?"Created":code==204?"No Content":code==400?"Bad Request":code==404?"Not Found":code==405?"Method Not Allowed":code==413?"Payload Too Large":code==415?"Unsupported Media Type":code==503?"Service Unavailable":"Internal Server Error";
    h->header_size=(size_t)snprintf(h->header,sizeof(h->header),"HTTP/1.1 %u %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nConnection: close\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, X-LightVM-Session\r\n\r\n",code,reason,binary?"application/octet-stream":"application/json",(unsigned long)h->used);
}
void http_tick(Http *h,uint64_t now) {
    expire(h,now);
    if (!h->sending && h->count) {
        h->active=h->queue[h->head]; h->head=(h->head+1)%HTTP_QUEUE; h->count--;
        h->used=0; h->sent=0; h->header_sent=0; h->overflow=false; h->reply_source=0;
        unsigned code=execute(h,&h->active,now);
        if (code>=400) { h->used=(size_t)snprintf((char *)h->response,response_capacity(h),"{\"error\":%u}",code); }
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
    if (complete || now>=h->deadline) {
        h->closing=true;
        bool closed=h->active.io.close(h->active.io.context);
        if (closed) {
            if (closed && complete) app_reply_delivered(h->app,h->reply_source);
            h->sending=false;
            response_release(h);
        }
    }
}
void http_cancel_io(Http *h,void *context) {
    if (h->sending && h->active.io.context==context) { h->sending=false; response_release(h); }
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
    char version[16],extra;
    if (sscanf(header,"%7s %79s %15s %c",r->method,r->path,version,&extra)!=3 || strcmp(version,"HTTP/1.1")) r->error=400;
    bool length_seen=false,token_seen=false,media=false; size_t length=0;
    for (char *field=line+2;*field;) {
        char *next=strstr(field,"\r\n"); if (!next) break; *next=0;
        char *colon=strchr(field,':'); if (!colon) { if (*field) r->error=400; break; } *colon=0;
        char *value=colon+1; while (*value==' ' || *value=='\t') value++;
        char *tail=value+strlen(value); while (tail>value && (tail[-1]==' ' || tail[-1]=='\t')) *--tail=0;
        if (!strcasecmp(field,"Content-Length")) {
            if (length_seen || !*value) r->error=400;
            length_seen=true;
            for (char *p=value;*p;p++) { if (*p<'0' || *p>'9' || length>528) { r->error=413; break; } length=length*10+(unsigned)(*p-'0'); }
        } else if (!strcasecmp(field,"Transfer-Encoding")) r->error=400;
        else if (!strcasecmp(field,"X-LightVM-Session")) { if (token_seen || strlen(value)>=sizeof(r->token)) r->error=400; else strcpy(r->token,value); token_seen=true; }
        else if (!strcasecmp(field,"Content-Type")) media=!strcmp(value,"application/octet-stream");
        field=next+2;
    }
    if (length>sizeof(r->body)) r->error=413;
    if (!strcmp(r->method,"POST") && !strcmp(r->path,"/api/v1/command") && (!media || !length_seen)) r->error=media?400:415;
    if (r->error) { input->ready=true; return true; }
    if (input->used<end+length) return false;
    if (input->used!=end+length) r->error=400;
    r->size=length; memcpy(r->body,input->bytes+end,length); input->ready=true; return true;
}
