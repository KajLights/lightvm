#ifndef LIGHTVM_PICO_MQTT_PORT_H
#define LIGHTVM_PICO_MQTT_PORT_H
#include "mqtt.h"
struct tcp_pcb;
struct pbuf;
typedef struct {
    struct tcp_pcb *pcb;
    struct pbuf *input;
    unsigned offset;
    uint16_t port;
    int state;
} PicoMqtt;
MqttStream pico_mqtt_stream(PicoMqtt *);
#endif
