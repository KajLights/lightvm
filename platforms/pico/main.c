#include "session.h"
#include "port.h"
#include "runtime.h"
#if LIGHTVM_WIFI
#include "network_port.h"
#endif
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/stdio/driver.h"
#include <assert.h>
#include <string.h>

typedef struct { uint8_t bytes[HEADER_SIZE+FRAME_LIMIT];size_t used; } UsbReply;
static void output_bytes(void *context, const uint8_t *bytes, size_t size) {
    UsbReply *reply=context;
    assert(size<=sizeof(reply->bytes)-reply->used);
    memcpy(reply->bytes+reply->used,bytes,size);reply->used+=size;
}
static void output_message(void *context, uint8_t type, uint16_t id,
                           const uint8_t *payload, size_t size) {
    (void)context;assert(size<=FRAME_LIMIT);
    /* One CDC write per protocol frame. Separate header/payload writes flush
       short USB packets and add round trips to multi-frame state responses. */
    UsbReply reply;reply.used=0;
    write_frame(output_bytes, &reply, type, id, payload, size);
    stdio_usb.out_chars((const char *)reply.bytes,(int)reply.used);
    stdio_flush();
}
static void service_io(void *context) {
    (void)context;
#if LIGHTVM_WIFI
    pico_network_poll();
#endif
    tight_loop_contents();
}
/* Two real ingress opportunities separate network work from engine work. A
 * slow HTTP send and a render/preparation slice cannot concatenate before USB
 * and physical inputs are polled. Commands still execute only on core 0. */
static void service_commands(App *app,Session *session,bool *connected){
    uint64_t now=platform_time_us();runtime_ingress(now);
    digital_inputs_tick(&app->digital_inputs,now);
    bool current=stdio_usb_connected();
    if(*connected&&!current){session_destroy(session);session_init(session,app,1,output_message,NULL);}
    *connected=current;
    /* One bulk read avoids a TinyUSB lock operation for every byte. */
    uint8_t input[256];int size=stdio_usb.in_chars((char *)input,sizeof(input));
    if(size>0){session_feed(session,input,(size_t)size);app_reply_delivered(app,1);}
}
int main(void) {
    static App app;
    static Session session;
    pico_port_init();
    stdio_init_all();
    stdio_set_translate_crlf(&stdio_usb, false);
    app_init(&app, output_message, NULL);
    session_init(&session, &app, 1, output_message, NULL);
#if LIGHTVM_WIFI
    pico_network_init(&app);
#endif
    app.service_io=service_io;
    bool connected=stdio_usb_connected();
    for (;;) {
#if LIGHTVM_WIFI
        pico_network_poll();
#endif
#if LIGHTVM_MQTT
        pico_mqtt_poll();
#endif
        service_commands(&app,&session,&connected);
        app_tick(&app);
        service_commands(&app,&session,&connected);
    }
}
