#pragma once
#include <stdbool.h>
#include <stdint.h>
#define MQTT_HOST_CAPACITY 128u
typedef struct {
    bool enabled;
    uint16_t port;
    char host[MQTT_HOST_CAPACITY], username[65], password[65];
} MqttConfig;
