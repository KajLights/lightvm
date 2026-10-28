#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _DEFAULT_SOURCE 1
#include "audio_sources.h"
#include "platform.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {int fd;struct sockaddr_in peer;bool resolved;} HostAudio;
typedef struct {AudioProvider *owner;char name[128];struct in_addr address;bool pending,done,ok;} Lookup;
static Lookup lookups[AUDIO_PROVIDER_LIMIT];
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static bool unicast(struct in_addr ip){uint32_t host=ntohl(ip.s_addr);return host>>24&&(host>>24)<224;}
static void *resolve(void *ctx){
    Lookup *l=ctx;struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_DGRAM},*result=NULL;
    int error=getaddrinfo(l->name,NULL,&hints,&result);struct in_addr address={0};
    if(!error&&result)address=((struct sockaddr_in *)result->ai_addr)->sin_addr;
    if(result)freeaddrinfo(result);
    pthread_mutex_lock(&mutex);l->address=address;l->ok=!error&&unicast(address);l->done=true;pthread_mutex_unlock(&mutex);
    return NULL;
}
static void close_source(void *ctx,AudioProvider *p){
    (void)ctx;pthread_mutex_lock(&mutex);
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++)if(lookups[i].owner==p)lookups[i].owner=NULL;
    pthread_mutex_unlock(&mutex);
    HostAudio *s=p->native;if(s){close(s->fd);platform_free(s);p->native=NULL;}
}
static bool open_source(void *ctx,AudioProvider *p){
    (void)ctx;HostAudio *s=platform_calloc(1,sizeof(*s));if(!s)return false;
    s->fd=socket(AF_INET,SOCK_DGRAM,0);p->native=s;
    struct sockaddr_in local={.sin_family=AF_INET};int ttl=1;
    if(s->fd<0||bind(s->fd,(struct sockaddr *)&local,sizeof(local))||setsockopt(s->fd,IPPROTO_IP,IP_TTL,&ttl,sizeof(ttl))||fcntl(s->fd,F_SETFL,O_NONBLOCK)<0){close_source(NULL,p);return false;}
    s->peer.sin_family=AF_INET;s->peer.sin_port=htons(p->config.port);
    if(inet_pton(AF_INET,p->config.address,&s->peer.sin_addr)==1){s->resolved=unicast(s->peer.sin_addr);return s->resolved;}
    bool started=false;pthread_mutex_lock(&mutex);
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++)if(!lookups[i].pending){
        Lookup *l=&lookups[i];*l=(Lookup){.owner=p,.pending=true};strcpy(l->name,p->config.address);
        pthread_t thread;
        if(!pthread_create(&thread,NULL,resolve,l)){pthread_detach(thread);started=true;}
        else l->pending=false;
        break;
    }
    pthread_mutex_unlock(&mutex);
    if(!started)close_source(NULL,p);
    return started;
}
static bool send_source(void *ctx,AudioProvider *p,const uint8_t *bytes,size_t size){
    (void)ctx;HostAudio *s=p->native;
    return s&&s->resolved&&sendto(s->fd,bytes,size,0,(struct sockaddr *)&s->peer,sizeof(s->peer))==(ssize_t)size;
}
void host_audio_init(AudioSources *sources){sources->transport=(AudioTransport){open_source,close_source,send_source,NULL,sizeof(HostAudio)};}
void host_audio_poll(AudioSources *sources){
    pthread_mutex_lock(&mutex);
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT;i++){
        Lookup *l=&lookups[i];if(!l->pending||!l->done)continue;
        AudioProvider *p=l->owner;
        if(p&&p->native){HostAudio *s=p->native;
            if(l->ok){s->peer.sin_addr=l->address;s->resolved=true;p->error=NULL;}
            else{p->receiver.client=0;p->retry_at=platform_time_us()+1000000;p->error="DNS unavailable";}}
        memset(l,0,sizeof(*l));
    }
    pthread_mutex_unlock(&mutex);
    unsigned budget=16;
    for(unsigned i=0;i<AUDIO_PROVIDER_LIMIT&&budget;i++){
        AudioProvider *p=sources->providers[i];if(!p||p->config.type!=AUDIO_UDP||!p->native)continue;
        HostAudio *s=p->native;
        while(budget){
            uint8_t bytes[KF_WIRE_MAX+1];struct sockaddr_in peer;socklen_t length=sizeof(peer);
            ssize_t n=recvfrom(s->fd,bytes,sizeof(bytes),0,(struct sockaddr *)&peer,&length);if(n<0)break;
            --budget;
            if(s->resolved&&peer.sin_addr.s_addr==s->peer.sin_addr.s_addr&&peer.sin_port==s->peer.sin_port)
                audio_source_receive(p,bytes,(size_t)n,platform_time_us());
        }
    }
}
