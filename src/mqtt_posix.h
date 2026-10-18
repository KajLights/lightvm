#ifndef LIGHTVM_MQTT_POSIX_H
#define LIGHTVM_MQTT_POSIX_H
#include "mqtt.h"
typedef struct { int socket; } MqttSocket;
MqttStream mqtt_socket_stream(MqttSocket *);
#endif
