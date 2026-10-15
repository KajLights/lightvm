#include "http.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
typedef struct { uint8_t bytes[4096]; size_t used; bool stalled,close_stalled,closed; } Sink;
static int output(void *context,const uint8_t *bytes,size_t size) { Sink *s=context; if (s->stalled) return 0; CHECK(size<=sizeof(s->bytes)-s->used); memcpy(s->bytes+s->used,bytes,size); s->used+=size; return (int)size; }
static bool close_sink(void *context) { Sink *s=context; if (s->close_stalled) return false; s->closed=true; return true; }
static void drain(Http *http,uint64_t now,Sink *sink) { for (unsigned i=0;i<200 && !sink->closed;i++) http_tick(http,now+i); CHECK(sink->closed); sink->bytes[sink->used]=0; }
static void service_ingress(void *context) { Http *h=context;CHECK(h->app->command_active);http_tick(h,0); }
int main(void) {
    static App app; static Http http; app_init(&app,NULL,NULL); strcpy(app.device_id,"test"); strcpy(app.boot_id,"boot"); http_init(&http,&app);
    Sink list={0}; HttpRequest get={.method="GET",.path="/api/v1/devices",.io={&list,output,close_sink}};
    CHECK(http_submit(&http,&get));app.command_active=true;http_tick(&http,0);
    CHECK(http.count==1&&!list.used&&!http.sending);app.command_active=false;
    app.service_io=service_ingress;app.service_context=&http;runtime_checkpoint();app.service_io=NULL;
    CHECK(!app.command_active&&http.count==1&&!list.used&&!http.sending);drain(&http,0,&list);
    CHECK(strstr((char *)list.bytes,"200 OK") && strstr((char *)list.bytes,"Content-Type: application/json") && strstr((char *)list.bytes,"\"devices\":[]"));
    CHECK(!http.sessions[0].source);
    memset(&list,0,sizeof(list)); get.discovery_only=true; strcpy(get.method,"POST"); strcpy(get.path,"/api/v1/sessions"); CHECK(http_submit(&http,&get)); drain(&http,0,&list); CHECK(strstr((char *)list.bytes,"404 Not Found") && !http.sessions[0].source);
    get.discovery_only=false; strcpy(get.path,"/api/v1/devices");
    memset(&list,0,sizeof(list)); strcpy(get.method,"POST"); CHECK(http_submit(&http,&get)); drain(&http,0,&list); CHECK(strstr((char *)list.bytes,"405 Method Not Allowed"));
    Sink sink={0}; HttpRequest request={.method="POST",.path="/api/v1/sessions",.io={&sink,output,close_sink}};
    CHECK(http_submit(&http,&request)); drain(&http,0,&sink); CHECK(strstr((char *)sink.bytes,"201 Created"));
    char token[33]; strcpy(token,http.sessions[0].token); uint64_t source=http.sessions[0].source;
    app.upload_owner=source; app.uploading=true;
    http_tick(&http,300000001u); CHECK(!app.upload_owner && !app.uploading && !http.sessions[0].source);
    memset(&sink,0,sizeof(sink)); strcpy(request.path,"/api/v1/command"); strcpy(request.token,token);
    CHECK(http_submit(&http,&request)); drain(&http,300000002u,&sink); CHECK(strstr((char *)sink.bytes,"404 Not Found"));
    memset(&sink,0,sizeof(sink)); sink.stalled=true; sink.close_stalled=true; strcpy(request.path,"/api/v1/sessions");
    CHECK(http_submit(&http,&request)); http_tick(&http,400000000u); CHECK(http.sending);
    http_tick(&http,402000001u); CHECK(http.sending && http.closing && !sink.closed);
    Sink next={0}; request.io.context=&next; CHECK(http_submit(&http,&request));
    sink.stalled=false; http_tick(&http,402000002u);
    CHECK(http.sending && http.count==1 && !next.used && !sink.used);
    sink.close_stalled=false; http_tick(&http,402000003u); CHECK(!http.sending && sink.closed);
    drain(&http,402000004u,&next); CHECK(strstr((char *)next.bytes,"201 Created"));
    for (unsigned i=0;i<HTTP_QUEUE;i++) CHECK(http_submit(&http,&request));
    CHECK(!http_submit(&http,&request));
    HttpInput input={0}; HttpRequest parsed={0};
    const char *bad="POST /api/v1/command HTTP/1.1\r\nContent-Length: 0\r\nContent-Length: 0\r\nContent-Type: application/octet-stream\r\n\r\n";
    CHECK(http_input(&input,(const uint8_t *)bad,strlen(bad),&parsed) && parsed.error==400);
    memset(&input,0,sizeof(input)); memset(&parsed,0,sizeof(parsed));
    const char *chunked="POST /api/v1/command HTTP/1.1\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\nContent-Type: application/octet-stream\r\n\r\n";
    CHECK(http_input(&input,(const uint8_t *)chunked,strlen(chunked),&parsed) && parsed.error==400);
    app_destroy(&app); return 0;
}
