/* POSIX host and ESP-IDF/lwIP sockets share the same nonblocking adapter. */
#include "mqtt_posix.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
static void close_stream(void *context) {
    MqttSocket *s=context;if(s->socket>=0)close(s->socket);s->socket=-1;
}
static bool open_stream(void *context,const char *host,uint16_t port) {
    MqttSocket *s=context;close_stream(s);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(port)};
    if(inet_pton(AF_INET,host,&address.sin_addr)!=1)return false;
    s->socket=socket(AF_INET,SOCK_STREAM,0);if(s->socket<0)return false;
#ifdef SO_NOSIGPIPE
    int no_sigpipe=1;setsockopt(s->socket,SOL_SOCKET,SO_NOSIGPIPE,&no_sigpipe,sizeof(no_sigpipe));
#endif
    if(fcntl(s->socket,F_SETFL,O_NONBLOCK)<0){close_stream(s);return false;}
    if(connect(s->socket,(struct sockaddr *)&address,sizeof(address))<0 && errno!=EINPROGRESS){close_stream(s);return false;}
    return true;
}
static int poll_stream(void *context) {
    MqttSocket *s=context;if(s->socket<0)return -1;
    fd_set write;FD_ZERO(&write);FD_SET(s->socket,&write);struct timeval timeout={0,0};
    int n=select(s->socket+1,NULL,&write,NULL,&timeout);if(n<=0)return n<0&&errno!=EINTR?-1:0;
    int error=0;socklen_t size=sizeof(error);
    return getsockopt(s->socket,SOL_SOCKET,SO_ERROR,&error,&size)<0||error?-1:1;
}
static int read_stream(void *context,uint8_t *out,size_t size) {
    MqttSocket *s=context;ssize_t n=recv(s->socket,out,size,0);
    if(n<0 && (errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;
    return n<=0?-1:(int)n;
}
static int write_stream(void *context,const uint8_t *data,size_t size) {
    MqttSocket *s=context;
#ifdef MSG_NOSIGNAL
    ssize_t n=send(s->socket,data,size,MSG_NOSIGNAL);
#else
    ssize_t n=send(s->socket,data,size,0);
#endif
    if(n<0 && (errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;
    return n<=0?-1:(int)n;
}
MqttStream mqtt_socket_stream(MqttSocket *s) {
    s->socket=-1;return (MqttStream){s,open_stream,poll_stream,read_stream,write_stream,close_stream};
}
