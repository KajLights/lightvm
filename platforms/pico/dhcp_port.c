#include <stdbool.h>
#include "dhcp_port.h"
#include "platform.h"
#include <string.h>
static void received(void *ctx,struct udp_pcb *pcb,struct pbuf *p,const ip_addr_t *source,u16_t port) {
    (void)source; // IP_IS_V4 is constant in the IPv4-only build.
    PicoDhcp *d=ctx; uint8_t in[DHCP_PACKET_LIMIT],out[300];
    if (p && port==68 && IP_IS_V4(source) && p->tot_len<=sizeof(in) && pbuf_copy_partial(p,in,p->tot_len,0)==p->tot_len) {
        size_t size=dhcp_reply(&d->server,in,p->tot_len,out,sizeof(out),platform_time_us());
        if (size) {
            struct pbuf *reply=pbuf_alloc(PBUF_TRANSPORT,(u16_t)size,PBUF_RAM);
            if (reply) {
                ip_addr_t target; IP_ADDR4(&target,255,255,255,255);
                if (pbuf_take(reply,out,(u16_t)size)==ERR_OK) udp_sendto_if(pcb,reply,&target,68,d->netif);
                pbuf_free(reply);
            }
        }
    }
    if (p) pbuf_free(p);
}
void pico_dhcp_stop(PicoDhcp *d) {
    if (d->pcb) udp_remove(d->pcb);
    memset(d,0,sizeof(*d));
}
bool pico_dhcp_start(PicoDhcp *d,struct netif *netif) {
    pico_dhcp_stop(d); d->netif=netif;
    d->pcb=udp_new_ip_type(IPADDR_TYPE_V4); if (!d->pcb) return false;
    if (udp_bind(d->pcb,IP_ANY_TYPE,67)!=ERR_OK) { pico_dhcp_stop(d); return false; }
    udp_bind_netif(d->pcb,netif); ip_set_option(d->pcb,SOF_BROADCAST);
    udp_recv(d->pcb,received,d); return true;
}
