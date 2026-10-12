#define _POSIX_C_SOURCE 200809L
#include "http_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
static Http server;
static Discovery discovery;
static int listener=-1;
typedef struct { int fd; HttpInput input; HttpRequest request; uint64_t deadline; } Connection;
static Connection clients[8];
static int output(void *ctx,const uint8_t *bytes,size_t size) {
    Connection *c=ctx; ssize_t n=send(c->fd,bytes,size,0);
    if (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)) return 0;
    return (int)n;
}
static bool close_connection(void *ctx) { Connection *c=ctx; if (c->fd>=0) close(c->fd); c->fd=-1; return true; }
void host_http_stop(void) {
    if (listener>=0) close(listener);
    listener=-1;
    for (unsigned i=0;i<8;i++) close_connection(&clients[i]);
    http_disconnect_sessions(&server);
    discovery_stop(&discovery,platform_time_us());
}
bool host_http_start(App *app,const char *address,unsigned port) {
    http_init(&server,app); for (unsigned i=0;i<8;i++) clients[i].fd=-1;
    signal(SIGPIPE,SIG_IGN);
    listener=socket(AF_INET,SOCK_STREAM,0); if (listener<0) return false;
    int yes=1; setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons((uint16_t)port)};
    if (inet_pton(AF_INET,address,&addr.sin_addr)!=1 || bind(listener,(struct sockaddr *)&addr,sizeof(addr)) || listen(listener,8) || fcntl(listener,F_SETFL,O_NONBLOCK)) { close(listener); listener=-1; return false; }
    socklen_t size=sizeof(addr); getsockname(listener,(struct sockaddr *)&addr,&size);
    // The simulator returns itself for browser/integration testing. It never
    // joins multicast or advertises the default loopback-only HTTP listener.
    uint8_t ip[4]; memcpy(ip,&addr.sin_addr.s_addr,4);
    discovery_start(&discovery,"lightvm-host",app->device_id,platform_info()->board,ip,ntohs(addr.sin_port),NULL,NULL,platform_time_us());
    server.discovery=&discovery;
    fprintf(stderr,"HTTP listening on http://%s:%u\n",address,ntohs(addr.sin_port)); fflush(stderr); return true;
}
void host_http_poll(void) {
    if (listener<0) return;
    uint64_t now=platform_time_us();
    int fd=accept(listener,NULL,NULL);
    if (fd>=0) {
        Connection *c=NULL; for (unsigned i=0;i<8;i++) if (clients[i].fd<0) { c=&clients[i]; break; }
        if (!c) close(fd); else { memset(c,0,sizeof(*c)); c->fd=fd; int send_buffer=4096; setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&send_buffer,sizeof(send_buffer)); c->deadline=now+2000000u; fcntl(fd,F_SETFL,O_NONBLOCK); c->request.io=(HttpIO){c,output,close_connection}; }
    }
    for (unsigned i=0;i<8;i++) {
        Connection *c=&clients[i]; if (c->fd<0 || c->input.ready) continue;
        uint8_t bytes[512]; ssize_t n=recv(c->fd,bytes,sizeof(bytes),0);
        if (n>0) {
            c->deadline=now+2000000u;
            if (http_input(&c->input,bytes,(size_t)n,&c->request) && !http_submit(&server,&c->request)) {
                const char *busy="HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
                output(c,(const uint8_t *)busy,strlen(busy)); close_connection(c);
            }
        } else if (!n || (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) || now>=c->deadline) { http_cancel_io(&server,c); close_connection(c); }
    }
    http_tick(&server,now);
}
