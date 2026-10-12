#ifndef LIGHTVM_DISCOVERY_H
#define LIGHTVM_DISCOVERY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DISCOVERY_LIMIT 8u
#define DISCOVERY_PACKET_LIMIT 1024u
#define DISCOVERY_PORT 5353u
#define DISCOVERY_TTL 30u
typedef struct {
    char hostname[32],device_id[64],board[32];
    uint8_t ip[4]; uint16_t port;
    uint64_t ptr_until,srv_until,txt_until,ip_until;
} DiscoveryDevice;
// IPv4 bytes are in network order. All callbacks and state run on the engine owner.
typedef bool (*DiscoverySend)(void *,const uint8_t *,size_t,const uint8_t[4],uint16_t);
typedef struct {
    DiscoveryDevice devices[DISCOVERY_LIMIT];
    DiscoverySend send; void *context;
    uint8_t packet[DISCOVERY_PACKET_LIMIT];
    uint64_t next_query,next_announce,reply_at;
    unsigned probes,collision;
    bool active,announced,reply_pending,reply_alias,truncated;
} Discovery;
void discovery_start(Discovery *,const char *hostname,const char *device_id,const char *board,
                     const uint8_t ip[4],uint16_t port,DiscoverySend,void *,uint64_t now);
void discovery_stop(Discovery *,uint64_t now);
void discovery_receive(Discovery *,const uint8_t *,size_t,const uint8_t from[4],uint16_t port,uint64_t now);
void discovery_tick(Discovery *,uint64_t now);
// Includes this device; never exposes partially resolved or expired peers.
size_t discovery_json(Discovery *,char *,size_t,uint64_t now);
#endif
