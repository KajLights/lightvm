#include "tcp.h"
#include "platform.h"
#include "lwip/tcp.h"
#include <string.h>
typedef struct { TcpConnection *owner; struct tcp_pcb *pcb; uint8_t rx[2048]; size_t used; } NativeTcp;
static void failed(void *context,err_t error) {
    (void)error; NativeTcp *n=context; n->pcb=NULL; n->owner->state=-1;
}
static err_t connected(void *context,struct tcp_pcb *pcb,err_t error) {
    (void)pcb; NativeTcp *n=context; n->owner->state=error==ERR_OK?1:-1; return ERR_OK;
}
static err_t received(void *context,struct tcp_pcb *pcb,struct pbuf *p,err_t error) {
    (void)pcb; NativeTcp *n=context;
    if (!p) { n->owner->state=-1; return ERR_OK; }
    if (error!=ERR_OK || p->tot_len>sizeof(n->rx)-n->used) return ERR_MEM;
    pbuf_copy_partial(p,n->rx+n->used,p->tot_len,0); n->used+=p->tot_len; pbuf_free(p); return ERR_OK;
}
void platform_tcp_close(TcpConnection *c) {
    NativeTcp *n=c->native;
    if (n) {
        if (n->pcb) { tcp_arg(n->pcb,NULL); tcp_recv(n->pcb,NULL); tcp_err(n->pcb,NULL); tcp_abort(n->pcb); }
        platform_free(n);
    }
    c->native=NULL; c->state=-1;
}
int platform_tcp_connect(TcpConnection *c,uint32_t ipv4,uint16_t port) {
    platform_tcp_close(c); NativeTcp *n=platform_calloc(1,sizeof(*n));
    if (!n) return -1;
    c->native=n; n->owner=c; n->pcb=tcp_new_ip_type(IPADDR_TYPE_V4); c->state=0;
    if (!n->pcb) { platform_tcp_close(c); return -1; }
    tcp_arg(n->pcb,n); tcp_recv(n->pcb,received); tcp_err(n->pcb,failed);
    ip_addr_t ip; IP_ADDR4(&ip,ipv4>>24,(ipv4>>16)&255,(ipv4>>8)&255,ipv4&255);
    if (tcp_connect(n->pcb,&ip,port,connected)!=ERR_OK) platform_tcp_close(c);
    return c->state;
}
int platform_tcp_state(TcpConnection *c) { return c->state; }
int platform_tcp_read(TcpConnection *c,uint8_t *p,size_t size) {
    NativeTcp *n=c->native; if (!n || !n->pcb || c->state!=1) return -1;
    if (size>n->used) size=n->used;
    memcpy(p,n->rx,size); n->used-=size; memmove(n->rx,n->rx+size,n->used); tcp_recved(n->pcb,(u16_t)size); return (int)size;
}
int platform_tcp_write(TcpConnection *c,const uint8_t *p,size_t size) {
    NativeTcp *n=c->native; if (!n || !n->pcb || c->state!=1) return -1;
    if (size>tcp_sndbuf(n->pcb)) size=tcp_sndbuf(n->pcb);
    if (!size) return 0;
    err_t e=tcp_write(n->pcb,p,(u16_t)size,TCP_WRITE_FLAG_COPY);
    if (e==ERR_MEM) return 0;
    if (e!=ERR_OK) { platform_tcp_close(c); return -1; }
    (void)tcp_output(n->pcb); return (int)size;
}
void platform_native_memory(uint32_t *free_bytes,uint32_t *minimum,uint32_t *largest) {
    *free_bytes=*minimum=*largest=0; // No portable allocator statistics in Pico SDK 1.5.1.
}
