#ifndef LIGHTVM_MQTT_POSIX_H
#define LIGHTVM_MQTT_POSIX_H
#include "mqtt.h"
typedef struct { int socket; uint16_t port; void *resolver; } MqttSocket;
MqttStream mqtt_socket_stream(MqttSocket *);
#endif
