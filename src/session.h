#ifndef LIGHTVM_SESSION_H
#define LIGHTVM_SESSION_H
#include "app.h"
// Connections borrow one serialized engine. Their decoders and reply routes are independent.
typedef struct { App *app; Decoder decoder; uint64_t identity; SendMessage send; void *transport; } Session;
void session_init(Session *, App *, uint64_t identity, SendMessage, void *);
void session_feed(Session *, const uint8_t *, size_t);
void session_destroy(Session *);
#endif
