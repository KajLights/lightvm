#include "session.h"
static void receive_message(void *context,uint8_t type,uint16_t id,const uint8_t *payload,size_t size) {
    Session *s=context; app_dispatch(s->app,s->identity,s->send,s->transport,type,id,payload,size);
}
static void send_message(void *context,uint8_t type,uint16_t id,const uint8_t *payload,size_t size) {
    Session *s=context; s->send(s->transport,type,id,payload,size);
}
void session_init(Session *s,App *app,uint64_t identity,SendMessage send,void *transport) {
    s->app=app; s->identity=identity; s->send=send; s->transport=transport;
    decoder_init(&s->decoder,receive_message,send_message,s);
}
void session_feed(Session *s,const uint8_t *bytes,size_t size) { for (size_t i=0;i<size;i++) decoder_feed(&s->decoder,bytes[i]); }
void session_destroy(Session *s) { app_disconnect(s->app,s->identity); }
