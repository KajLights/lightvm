#include "tcp.h"
#include <errno.h>
#include <fcntl.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#include "esp_heap_caps.h"
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <unistd.h>
#endif
void platform_tcp_close(TcpConnection *c) {
    if (c->native) close((int)c->fd);
    c->native=NULL; c->fd=-1; c->state=-1;
}
int platform_tcp_connect(TcpConnection *c,uint32_t ip,uint16_t port) {
    platform_tcp_close(c);
    int fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (fd<0) return -1;
    c->fd=fd; c->native=c; c->state=0;
    if (fcntl(fd,F_SETFL,O_NONBLOCK)<0) { platform_tcp_close(c); return -1; }
#ifdef SO_NOSIGPIPE
    int yes=1; (void)setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));
#endif
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr={.s_addr=htonl(ip)}};
    int result=connect(fd,(struct sockaddr *)&address,sizeof(address));
    if (!result) c->state=1;
    else if (errno!=EINPROGRESS && errno!=EWOULDBLOCK) platform_tcp_close(c);
    return c->state;
}
int platform_tcp_state(TcpConnection *c) {
    if (!c->native || c->state) return c->state;
    fd_set output; FD_ZERO(&output); FD_SET((int)c->fd,&output);
    struct timeval zero={0,0};
    int result=select((int)c->fd+1,NULL,&output,NULL,&zero);
    if (result<0) { platform_tcp_close(c); return -1; }
    if (result) {
        int error=0; socklen_t size=sizeof(error);
        if (getsockopt((int)c->fd,SOL_SOCKET,SO_ERROR,&error,&size)<0 || error) platform_tcp_close(c);
        else c->state=1;
    }
    return c->state;
}
static int io_result(TcpConnection *c,int n) {
    if (n>0) return n;
    if (n<0 && (errno==EWOULDBLOCK || errno==EAGAIN || errno==EINTR)) return 0;
    platform_tcp_close(c); return -1;
}
int platform_tcp_read(TcpConnection *c,uint8_t *p,size_t n) {
    if (platform_tcp_state(c)!=1) return -1;
    return io_result(c,(int)recv((int)c->fd,p,n,0));
}
int platform_tcp_write(TcpConnection *c,const uint8_t *p,size_t n) {
    if (platform_tcp_state(c)!=1) return -1;
#ifdef MSG_NOSIGNAL
    return io_result(c,(int)send((int)c->fd,p,n,MSG_NOSIGNAL));
#else
    return io_result(c,(int)send((int)c->fd,p,n,0));
#endif
}
void platform_native_memory(uint32_t *free_bytes,uint32_t *minimum,uint32_t *largest) {
#ifdef ESP_PLATFORM
    *free_bytes=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    *minimum=heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    *largest=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#else
    *free_bytes=*minimum=*largest=0;
#endif
}
