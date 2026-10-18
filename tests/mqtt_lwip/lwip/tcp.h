#ifndef TEST_MQTT_LWIP_TCP_H
#define TEST_MQTT_LWIP_TCP_H
#include <stddef.h>
#include <stdint.h>
typedef int err_t;
typedef uint16_t u16_t;
typedef struct { uint32_t address; } ip_addr_t;
#define ERR_OK 0
#define ERR_MEM -1
#define ERR_INPROGRESS -5
#define ERR_ARG -16
#define IPADDR_TYPE_V4 0
#define TCP_WRITE_FLAG_COPY 1
struct tcp_pcb { void *context; err_t (*connected)(void *,struct tcp_pcb *,err_t); };
struct pbuf { u16_t tot_len; };
int ipaddr_aton(const char *,ip_addr_t *);
struct tcp_pcb *tcp_new_ip_type(unsigned);
void tcp_arg(struct tcp_pcb *,void *);
void tcp_recv(struct tcp_pcb *,err_t (*)(void *,struct tcp_pcb *,struct pbuf *,err_t));
void tcp_err(struct tcp_pcb *,void (*)(void *,err_t));
void tcp_abort(struct tcp_pcb *);
err_t tcp_connect(struct tcp_pcb *,const ip_addr_t *,u16_t,err_t (*)(void *,struct tcp_pcb *,err_t));
void tcp_recved(struct tcp_pcb *,u16_t);
u16_t tcp_sndbuf(const struct tcp_pcb *);
err_t tcp_write(struct tcp_pcb *,const void *,u16_t,unsigned);
err_t tcp_output(struct tcp_pcb *);
u16_t pbuf_free(struct pbuf *);
u16_t pbuf_copy_partial(const struct pbuf *,void *,u16_t,u16_t);
#endif
