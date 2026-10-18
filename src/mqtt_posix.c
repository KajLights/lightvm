/* POSIX host and ESP-IDF/lwIP sockets share the same nonblocking adapter. */
#include "mqtt_posix.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
/* DNS never runs on the engine owner. A single detached resolver bounds native
 * resources; cancelling a request drops ownership without waiting for DNS.
 * Reference counting also makes a late result safe after its stream closes. */
typedef struct {
    atomic_uint references, done;
    bool found;
    char host[MQTT_HOST_CAPACITY];
    struct in_addr address;
} Resolution;
static atomic_uint resolver_busy;
static void release_resolution(Resolution *r) {
    if(atomic_fetch_sub(&r->references,1)==1)free(r);
}
static void *resolve_host(void *context) {
    Resolution *r=context;
    struct addrinfo hints={0},*addresses=NULL;
    hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    if(!getaddrinfo(r->host,NULL,&hints,&addresses)){
        if(addresses){r->address=((struct sockaddr_in *)addresses->ai_addr)->sin_addr;r->found=true;}
        freeaddrinfo(addresses);
    }
    atomic_store(&r->done,1);atomic_store(&resolver_busy,0);
    release_resolution(r);return NULL;
}
static Resolution *start_resolution(const char *host) {
    unsigned expected=0;
    if(!atomic_compare_exchange_strong(&resolver_busy,&expected,1))return NULL;
    Resolution *r=calloc(1,sizeof(*r));
    if(!r){atomic_store(&resolver_busy,0);return NULL;}
    atomic_init(&r->references,2);atomic_init(&r->done,0);
    memcpy(r->host,host,strlen(host)+1);
    pthread_attr_t attributes;pthread_t thread;
    int result=pthread_attr_init(&attributes);
    if(!result){
        result=pthread_attr_setdetachstate(&attributes,PTHREAD_CREATE_DETACHED);
        if(!result)result=pthread_create(&thread,&attributes,resolve_host,r);
        pthread_attr_destroy(&attributes);
    }
    if(result){free(r);atomic_store(&resolver_busy,0);return NULL;}
    return r;
}
static void close_stream(void *context) {
    MqttSocket *s=context;if(s->socket>=0)close(s->socket);s->socket=-1;
    if(s->resolver)release_resolution(s->resolver);
    s->resolver=NULL;
}
static bool connect_address(MqttSocket *s,struct in_addr ip) {
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(s->port),.sin_addr=ip};
    s->socket=socket(AF_INET,SOCK_STREAM,0);if(s->socket<0)return false;
#ifdef SO_NOSIGPIPE
    int no_sigpipe=1;setsockopt(s->socket,SOL_SOCKET,SO_NOSIGPIPE,&no_sigpipe,sizeof(no_sigpipe));
#endif
    if(fcntl(s->socket,F_SETFL,O_NONBLOCK)<0){close_stream(s);return false;}
    if(connect(s->socket,(struct sockaddr *)&address,sizeof(address))<0 && errno!=EINPROGRESS){close_stream(s);return false;}
    return true;
}
static bool open_stream(void *context,const char *host,uint16_t port) {
    MqttSocket *s=context;close_stream(s);s->port=port;struct in_addr ip;
    if(inet_pton(AF_INET,host,&ip)==1)return connect_address(s,ip);
    if(!host[0]||strlen(host)>=MQTT_HOST_CAPACITY)return false;
    s->resolver=start_resolution(host);return s->resolver!=NULL;
}
static int poll_stream(void *context) {
    MqttSocket *s=context;
    if(s->resolver){
        Resolution *r=s->resolver;if(!atomic_load(&r->done))return 0;
        bool found=r->found;struct in_addr ip=r->address;
        s->resolver=NULL;release_resolution(r);
        if(!found||!connect_address(s,ip))return -1;
    }
    if(s->socket<0)return -1;
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
    memset(s,0,sizeof(*s));s->socket=-1;return (MqttStream){s,open_stream,poll_stream,read_stream,write_stream,close_stream};
}
