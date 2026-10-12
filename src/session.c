#include "session.h"

static void receive_message(void *context, uint8_t type, uint16_t id,
                            const uint8_t *payload, size_t size) {
    Session *session = context;
    app_command(&session->app, type, id, payload, size);
}
static void send_message(void *context, uint8_t type, uint16_t id,
                         const uint8_t *payload, size_t size) {
    Session *session = context;
    session->app.send(session->app.transport, type, id, payload, size);
}
void session_init(Session *session, SendMessage send, void *transport) {
    app_init(&session->app, send, transport);
    decoder_init(&session->decoder, receive_message, send_message, session);
}
void session_feed(Session *session, const uint8_t *bytes, size_t size) {
    for (size_t i = 0; i < size; ++i)
        decoder_feed(&session->decoder, bytes[i]);
}
void session_tick(Session *session) {
    app_tick(&session->app);
}
void session_destroy(Session *session) {
    app_destroy(&session->app);
}
