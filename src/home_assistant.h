#ifndef LIGHTVM_HOME_ASSISTANT_H
#define LIGHTVM_HOME_ASSISTANT_H
#include "app.h"
#include "mqtt.h"
#define HA_OUTPUT_LIMIT 8u
typedef struct {
    char id[OBJECT_NAME_LIMIT+1], name[65];
    uint32_t state_crc;
    unsigned remove_phase;
    bool discovered, known, removing;
} HaOutput;
typedef struct {
    char input[OBJECT_NAME_LIMIT+1],output[OBJECT_NAME_LIMIT+1],name[33];
    uint32_t state_crc;
    unsigned char channel,phase,remove_phase;
    bool removing,known,availability_known,available,input_known,input_available,feedback_known,feedback_enabled;
} HaKey;
typedef struct {
    App *app;
    Mqtt mqtt;
    uint64_t source;
    uint32_t last_operation;
    uint32_t generation, catalog_generation;
    unsigned phase;
    bool available;
    char root[96];
    HaOutput outputs[HA_OUTPUT_LIMIT];
    HaKey keys[DIGITAL_INPUT_LIMIT*DIGITAL_INPUT_CHANNELS];
    // Streaming discovery borrows the catalogue only while these revisions hold.
    char discovery_output[OBJECT_NAME_LIMIT+1];
    uint32_t discovery_store_generation, discovery_config_revision;
    uint32_t key_revision;
    bool keys_pending;
} HomeAssistant;
void ha_init(HomeAssistant *, App *, MqttStream);
void ha_tick(HomeAssistant *, bool network_ready, uint64_t now);
/* Close before tearing down the native network interface. LWT marks offline. */
void ha_close(HomeAssistant *);
// Input/indicator discovery shares the existing controller device and MQTT session.
void ha_keys_reconcile(HomeAssistant *);
void ha_keys_refresh(HomeAssistant *);
bool ha_keys_tick(HomeAssistant *,uint64_t now);
bool ha_keys_command(HomeAssistant *,const char *,const uint8_t *,size_t,Command *);
#endif
