#ifndef LIGHTVM_TCP_H
#define LIGHTVM_TCP_H
#include <stddef.h>
#include <stdint.h>
// Engine-owned, one connection. 0=pending/would block, -1=closed/error.
typedef struct { intptr_t fd; void *native; int state; } TcpConnection;
int platform_tcp_connect(TcpConnection *,uint32_t ipv4,uint16_t port);
int platform_tcp_state(TcpConnection *);
int platform_tcp_read(TcpConnection *,uint8_t *,size_t);
int platform_tcp_write(TcpConnection *,const uint8_t *,size_t);
void platform_tcp_close(TcpConnection *);
void platform_native_memory(uint32_t *free_bytes,uint32_t *minimum,uint32_t *largest);
#endif
