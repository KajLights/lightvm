#pragma once
#include "device/settings.h"
/* Services run on one management owner. Application callbacks never execute on
 * the native HTTP sender or Wi-Fi callback task. */
typedef struct DeviceHost {
    void *context;
    const char *controller_id,*boot_id,*product,*board,*version,*kind,*hostname_prefix;
    uint32_t capabilities;
    uint64_t *connection_sequence;
    Network *network;
    NetworkSettings *settings;
    bool (*busy)(void *);
    void (*dispatch)(void *,uint64_t,SendMessage,void *,uint8_t,uint16_t,const uint8_t *,size_t);
    void (*disconnect)(void *,uint64_t);
    void (*reply_delivered)(void *,uint64_t);
    enum Status (*save_network)(void *,const NetworkConfig *,uint32_t,uint64_t,char *,size_t);
} DeviceHost;
enum { DEVICE_CAP_CONFIGURATION=1u, DEVICE_CAP_LIGHT_OUTPUT=2u,
       DEVICE_CAP_AUDIO_ANALYSIS=4u, DEVICE_CAP_AUDIO_PUBLISHER=8u };
