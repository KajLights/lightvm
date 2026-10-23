#pragma once
#include "device/network.h"
#include "device/store.h"
#include "device/protocol.h"
typedef struct {
    Network *network;
    Store *store;
    NetworkConfig candidate;
    bool pending;
    uint64_t due,reply_source;
    char error[160];
} NetworkSettings;
void network_settings_init(NetworkSettings *,Network *,Store *);
void network_settings_load(NetworkSettings *);
enum Status network_settings_save(NetworkSettings *,const NetworkConfig *,uint32_t,uint64_t,char *,size_t);
void network_settings_tick(NetworkSettings *,uint64_t);
void network_settings_reply_delivered(NetworkSettings *,uint64_t,uint64_t);
/* Shared optimistic-concurrency guard for live and persisted application settings. */
bool settings_revision_matches(uint32_t current,uint32_t expected,char *,size_t);
