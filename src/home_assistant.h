#ifndef LIGHTVM_HOME_ASSISTANT_H
#define LIGHTVM_HOME_ASSISTANT_H
#include "app.h"
#include "mqtt.h"
#define HA_LIGHT_LIMIT 8u
typedef struct {
    char id[OBJECT_NAME_LIMIT+1], name[65];
    bool discovered, known, playing, removing, config_removed;
} HaLight;
typedef struct {
    App *app;
    Mqtt mqtt;
    uint64_t source;
    uint32_t generation;
    unsigned phase;
    bool available;
    char root[96];
    HaLight lights[HA_LIGHT_LIMIT];
} HomeAssistant;
void ha_init(HomeAssistant *, App *, MqttStream);
void ha_tick(HomeAssistant *, bool network_ready, uint64_t now);
/* Close before tearing down the native network interface. LWT marks offline. */
void ha_close(HomeAssistant *);
#endif
