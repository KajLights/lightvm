#ifndef LIGHTVM_PICO_DHCP_PORT_H
#define LIGHTVM_PICO_DHCP_PORT_H
#include <stdbool.h>
#include "dhcp.h"
#include "lwip/udp.h"
typedef struct { DhcpServer server; struct udp_pcb *pcb; struct netif *netif; } PicoDhcp;
bool pico_dhcp_start(PicoDhcp *,struct netif *);
void pico_dhcp_stop(PicoDhcp *);
#endif
