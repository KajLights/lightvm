#ifndef LIGHTVM_NETWORK_H
#define LIGHTVM_NETWORK_H
#include "config.h"
#include "mqtt.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define NETWORK_LIMIT 8u
typedef struct { char ssid[33], password[65]; bool hidden; } WifiNetwork;
typedef enum { NET_MODE_STATION, NET_MODE_AP } NetworkMode;
typedef struct {
    bool wifi_enabled, http_enabled;
    MqttConfig mqtt;
    NetworkMode mode;
    char ap_ssid[33], ap_password[64];
    char country[3];
    uint16_t port;
    unsigned count;
    WifiNetwork networks[NETWORK_LIMIT];
} NetworkConfig;
typedef enum { NET_DISABLED, NET_SCANNING, NET_CONNECTING, NET_CONNECTED, NET_BACKOFF, NET_AP_STARTING, NET_AP_ACTIVE } NetworkState;
// Native calls start asynchronous work. poll returns 0=pending, 1=success,
// -1=failure; station readiness includes DHCP, AP readiness includes its DHCP
// server and does not require any clients. Only the engine owner uses this contract.
typedef struct {
    void *context;
    bool (*configure)(void *, const NetworkConfig *);
    bool (*scan)(void *);
    int (*scan_poll)(void *, const char *, bool *);
    bool (*connect)(void *, const WifiNetwork *);
    int (*link)(void *, char *, size_t, char *, size_t);
    void (*disconnect)(void *);
    uint32_t (*random)(void *);
} NetworkDriver;
typedef struct {
    NetworkConfig config;
    NetworkDriver driver;
    NetworkState state;
    unsigned candidate, failures;
    int connected_index;
    bool visible[NETWORK_LIMIT], quick_retry, ready, saved_valid, initial_setup, recovery;
    uint64_t deadline, retry_at;
    char ip[48], error[96], ap_ssid[33];
} Network;
bool network_parse(const void *, size_t, const NetworkConfig *, NetworkConfig *, char *, size_t);
bool network_wifi_valid(const WifiNetwork *, char *, size_t);
bool network_country_valid(const char *);
size_t network_json(const NetworkConfig *, bool secrets, char *, size_t);
void network_init(Network *);
void network_bootstrap(Network *, bool setup_enabled);
bool network_online(const Network *);
// Saved/editable settings stay in config; recovery overrides only native runtime.
NetworkMode network_mode(const Network *);
bool network_http_enabled(const Network *);
uint16_t network_http_port(const Network *);
void network_apply(Network *, const NetworkConfig *);
void network_tick(Network *, uint64_t now);
const char *network_state_name(NetworkState);
#endif
