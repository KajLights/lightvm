#ifndef LIGHTVM_WEBSOCKET_H
#define LIGHTVM_WEBSOCKET_H
#include "http.h"

#define WEBSOCKET_PATH "/api/v1/ws"
#define WEBSOCKET_MESSAGE_LIMIT (HEADER_SIZE + FRAME_LIMIT)
enum { WS_UPGRADE=1, WS_BINARY=2, WS_CLOSE=8, WS_PING=9, WS_PONG=10 };
typedef struct { char key[25]; unsigned seen; bool invalid,host,upgrade,connection,version; } WebSocketHandshake;
// Native callbacks feed bytes on the engine owner; dispatch stays in Http's
// bounded queue. Each connection owns its source identity, never an HTTP token.
struct WebSocket {
    Http *http; HttpIO io;
    bool (*flush)(void *); // Optional asynchronous sender barrier; never blocks.
    uint64_t source,last_input;
    uint8_t header[14],message[WEBSOCKET_MESSAGE_LIMIT],control[125];
    size_t header_used,header_size,message_used,frame_size,frame_used;
    uint8_t opcode;
    bool final,fragmented,upgraded,disconnected,close_queued;
    uint16_t failure;
};
void websocket_header(WebSocketHandshake *,const char *name,const char *value);
unsigned websocket_upgrade(HttpRequest *,const WebSocketHandshake *);
size_t websocket_accept(const char *key,char *out,size_t capacity);
size_t websocket_frame_header(uint8_t *out,uint8_t opcode,size_t size);
void websocket_init(WebSocket *,Http *,HttpIO,bool (*flush)(void *));
void websocket_feed(WebSocket *,const uint8_t *,size_t,uint64_t now);
void websocket_tick(WebSocket *,uint64_t now);
void websocket_closed(WebSocket *);
#endif
