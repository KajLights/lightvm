#ifndef LIGHTVM_DHCP_H
#define LIGHTVM_DHCP_H
#include <stdint.h>
#include <stddef.h>
#define DHCP_CLIENT_LIMIT 4u
#define DHCP_PACKET_LIMIT 576u
typedef struct { uint8_t mac[6]; uint64_t expires; } DhcpLease;
typedef struct { DhcpLease leases[DHCP_CLIENT_LIMIT]; } DhcpServer;
// Fixed AP subnet: 192.168.4.1/24, leases .2 through .5. Returns zero for
// malformed/irrelevant packets or lease exhaustion. No allocation or native IO.
size_t dhcp_reply(DhcpServer *, const uint8_t *, size_t, uint8_t *, size_t, uint64_t now_us);
#endif
