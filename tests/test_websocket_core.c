#include "websocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
typedef struct { WebSocket *ws; uint8_t bytes[4096]; size_t used; bool stalled,closed,flushed; } Sink;
static int output(void *context,const uint8_t *bytes,size_t size) {
    Sink *s=context; if (s->stalled) return 0;
    if (size>7) size=7;
    CHECK(size<sizeof(s->bytes)-s->used); memcpy(s->bytes+s->used,bytes,size); s->used+=size; return (int)size;
}
static bool close_sink(void *context) { Sink *s=context; s->closed=true; if (s->ws) websocket_closed(s->ws); return true; }
static bool flush(void *context) { return ((Sink *)context)->flushed; }
static void upgrade(Http *http,WebSocket *ws,Sink *sink) {
    sink->ws=ws; HttpIO io={sink,output,close_sink}; websocket_init(ws,http,io,flush);
    HttpInput input={0}; HttpRequest r={.io=io,.websocket=ws};
    const char *request="GET /api/v1/ws HTTP/1.1\r\nHost: local\r\nUpgrade: WebSocket\r\nConnection: keep-alive, upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
    size_t size=strlen(request);
    for (size_t i=0;i<size;i++) CHECK(http_input(&input,(const uint8_t *)request+i,1,&r)==(i+1==size));
    CHECK(!r.error && r.websocket_action==WS_UPGRADE); CHECK(http_submit(http,&r));
}
int main(void) {
    char accept[29]; CHECK(websocket_accept("dGhlIHNhbXBsZSBub25jZQ==",accept,sizeof(accept))==28);
    CHECK(!strcmp(accept,"s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    static App app; static Http http; app_init(&app,NULL,NULL); http_init(&http,&app);
    WebSocket ws; Sink sink={0}; upgrade(&http,&ws,&sink);
    app.command_active=true; http_tick(&http,1); CHECK(!ws.source && http.count==1); app.command_active=false;
    for (unsigned i=0;i<100;i++) http_tick(&http,2+i);
    CHECK(ws.source && !ws.upgraded && http.sending && !sink.closed);
    sink.flushed=true; http_tick(&http,103); CHECK(ws.upgraded && !http.sending);
    CHECK(strstr((char *)sink.bytes,"Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    Sink ordinary={0}; HttpRequest session={.method="POST",.path="/api/v1/sessions",.io={&ordinary,output,close_sink}};
    CHECK(http_submit(&http,&session)); for (unsigned i=0;i<100;i++) http_tick(&http,104+i);
    CHECK(ordinary.closed && http.sessions[0].source && http.sessions[0].source!=ws.source);
    uint64_t identity=ws.source; app.upload_owner=identity; app.uploading=true;
    app.command_active=true; websocket_closed(&ws); CHECK(ws.source==identity && app.upload_owner==identity);
    app.command_active=false; websocket_tick(&ws,104); CHECK(!ws.source && !app.upload_owner && !app.uploading);
    Sink stalled={.stalled=true,.flushed=true}; upgrade(&http,&ws,&stalled);
    http_tick(&http,200); CHECK(ws.source && http.sending);
    http_tick(&http,2000201); CHECK(stalled.closed && !ws.source && !http.sending);
    Sink partial={.flushed=true}; upgrade(&http,&ws,&partial);
    for (unsigned i=0;i<100;i++) http_tick(&http,3000000+i);
    CHECK(ws.upgraded);
    const uint8_t header[]={0x82,0x80}; websocket_feed(&ws,header,sizeof(header),4000000);
    websocket_tick(&ws,6000001); CHECK(ws.close_queued && ws.failure==1002);
    for (unsigned i=0;i<100;i++) http_tick(&http,6000002+i);
    CHECK(partial.closed && !ws.source);
    Sink overflow={.flushed=true}; upgrade(&http,&ws,&overflow);
    for (unsigned i=0;i<100;i++) http_tick(&http,7000000+i);
    CHECK(ws.upgraded);
    const uint8_t ping[]={0x89,0x80,1,2,3,4};
    for (unsigned i=0;i<HTTP_QUEUE+1;i++) websocket_feed(&ws,ping,sizeof(ping),8000000);
    CHECK(http.count==HTTP_QUEUE && ws.failure==1013 && !ws.close_queued);
    for (unsigned i=0;i<100;i++) { http_tick(&http,8000001+i); websocket_tick(&ws,8000001+i); }
    CHECK(overflow.closed && !ws.source && !http.count && !http.sending);
    uint8_t extended[10]; CHECK(websocket_frame_header(extended,WS_BINARY,65536)==10);
    CHECK(extended[0]==0x82 && extended[1]==127 && extended[7]==1 && extended[8]==0 && extended[9]==0);
    app_destroy(&app); return 0;
}
