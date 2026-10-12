#ifndef LIGHTVM_SESSION_H
#define LIGHTVM_SESSION_H
#include "app.h"

// One engine owner calls these serially. Native transports own polling, queues,
// flushing and their lifetime; no OS/SDK calls are needed in this session.
typedef struct {
    App app;
    Decoder decoder;
} Session;
void session_init(Session *session, SendMessage send, void *transport);
void session_feed(Session *session, const uint8_t *bytes, size_t size);
void session_tick(Session *session);
void session_destroy(Session *session);
#endif
