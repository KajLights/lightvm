#ifndef LIGHTVM_HTTP_H
#define LIGHTVM_HTTP_H
#include "app.h"
#include "discovery.h"
#define HTTP_SESSIONS LIGHTVM_HTTP_CLIENTS
#define HTTP_QUEUE 4u
#define HTTP_INPUT_LIMIT (2048u+HEADER_SIZE+FRAME_LIMIT)
#define HTTP_DATA_LIMIT (MODULE_LIMIT > (CONFIG_LIMIT+PIXEL_CAPACITY*4u) ? MODULE_LIMIT : (CONFIG_LIMIT+PIXEL_CAPACITY*4u))
#define HTTP_RESPONSE_LIMIT (HTTP_DATA_LIMIT+((HTTP_DATA_LIMIT+FRAME_LIMIT-1u)/FRAME_LIMIT)*HEADER_SIZE+1024u)
typedef struct { uint8_t bytes[HTTP_INPUT_LIMIT]; size_t used; bool ready; } HttpInput;
typedef struct {
    void *context;
    // Accepted bytes are copied/owned by the native sender. 0=backpressure, -1=closed.
    int (*write)(void *,const uint8_t *,size_t);
    bool (*close)(void *);
} HttpIO;
typedef struct WebSocket WebSocket;
typedef struct {
    char method[8],path[80],token[33];
    uint8_t body[HEADER_SIZE+FRAME_LIMIT]; size_t size;
    unsigned error;
    bool discovery_only,form_origin_valid;
    HttpIO io;
#if LIGHTVM_WEBSOCKET
    WebSocket *websocket;
    uint8_t websocket_action;
#endif
} HttpRequest;
typedef struct { char token[33]; uint64_t source,expires; } HttpSession;
typedef struct Http {
    App *app; Discovery *discovery; HttpSession sessions[HTTP_SESSIONS];
    HttpRequest queue[HTTP_QUEUE],active; unsigned head,count;
    uint8_t response[HTTP_RESPONSE_LIMIT]; size_t used,sent;
    char header[512]; size_t header_size,header_sent;
    bool sending,closing,overflow,executing,html; uint64_t deadline,reply_source;
#if LIGHTVM_WEBSOCKET
    bool websocket_keep_open;
#endif
} Http;
void http_init(Http *,App *);
// All operations below run on the engine owner. Native ingress queues cross threads.
bool http_submit(Http *,const HttpRequest *);
bool http_input(HttpInput *,const uint8_t *,size_t,HttpRequest *);
void http_tick(Http *,uint64_t);
void http_cancel_io(Http *,void *);
void http_disconnect_sessions(Http *);
#endif
