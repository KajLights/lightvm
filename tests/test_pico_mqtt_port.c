/* Late DNS replies must not reconnect a cancelled or reconfigured stream. */
#include "mqtt_port.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct { dns_found_callback found; void *context; } Request;
static Request requests[16];
static unsigned request_count,connection_count,aborts,pcbs;
static uint32_t address;
static uint16_t port;
static struct tcp_pcb connections[16],*latest;
static err_t connect_result;
int ipaddr_aton(const char *host,ip_addr_t *ip){if(strcmp(host,"127.0.0.1"))return 0;ip->address=1;return 1;}
struct tcp_pcb *tcp_new_ip_type(unsigned type){(void)type;CHECK(pcbs<16);return &connections[pcbs++];}
void tcp_arg(struct tcp_pcb *pcb,void *context){pcb->context=context;}
void tcp_recv(struct tcp_pcb *pcb,err_t (*callback)(void *,struct tcp_pcb *,struct pbuf *,err_t)){(void)pcb;(void)callback;}
void tcp_err(struct tcp_pcb *pcb,void (*callback)(void *,err_t)){(void)pcb;(void)callback;}
void tcp_abort(struct tcp_pcb *pcb){(void)pcb;aborts++;}
err_t tcp_connect(struct tcp_pcb *pcb,const ip_addr_t *ip,u16_t p,err_t (*callback)(void *,struct tcp_pcb *,err_t)){
    connection_count++;address=ip->address;port=p;latest=pcb;pcb->connected=callback;return connect_result;
}
void tcp_recved(struct tcp_pcb *pcb,u16_t n){(void)pcb;(void)n;}
u16_t tcp_sndbuf(const struct tcp_pcb *pcb){(void)pcb;return 128;}
err_t tcp_write(struct tcp_pcb *pcb,const void *bytes,u16_t n,unsigned flags){(void)pcb;(void)bytes;(void)n;(void)flags;return ERR_OK;}
err_t tcp_output(struct tcp_pcb *pcb){(void)pcb;return ERR_OK;}
u16_t pbuf_free(struct pbuf *p){(void)p;return 1;}
u16_t pbuf_copy_partial(const struct pbuf *p,void *bytes,u16_t n,u16_t offset){(void)p;(void)bytes;(void)offset;return n;}
err_t dns_gethostbyname(const char *host,ip_addr_t *ip,dns_found_callback found,void *context){
    if(!strcmp(host,"cached.example")){ip->address=2;return ERR_OK;}
    if(!strcmp(host,"invalid.example"))return ERR_ARG;
    CHECK(request_count<16);requests[request_count++]=(Request){found,context};return ERR_INPROGRESS;
}
static void complete(unsigned i,uint32_t ip){ip_addr_t value={ip};requests[i].found("broker.example",ip?&value:NULL,requests[i].context);}
static void connected(void){CHECK(latest->connected(latest->context,latest,ERR_OK)==ERR_OK);}
int main(void){
    PicoMqtt state;MqttStream stream=pico_mqtt_stream(&state);
    CHECK(stream.open(stream.context,"127.0.0.1",1883));CHECK(connection_count==1&&!request_count&&address==1);
    CHECK(stream.poll(stream.context)==0);connected();CHECK(stream.poll(stream.context)==1);stream.close(stream.context);CHECK(aborts==1);
    CHECK(stream.open(stream.context,"cached.example",1883));CHECK(address==2&&connection_count==2);stream.close(stream.context);
    CHECK(stream.open(stream.context,"old.example",1883));CHECK(request_count==1&&stream.poll(stream.context)==0);
    stream.close(stream.context);
    CHECK(stream.open(stream.context,"new.example",1884));CHECK(request_count==2);
    complete(0,3);CHECK(connection_count==2&&stream.poll(stream.context)==0);
    complete(1,4);CHECK(connection_count==3&&address==4&&port==1884);connected();CHECK(stream.poll(stream.context)==1);stream.close(stream.context);
    CHECK(stream.open(stream.context,"missing.example",1883));complete(2,0);CHECK(stream.poll(stream.context)==-1&&connection_count==3);
    CHECK(!stream.open(stream.context,"invalid.example",1883));CHECK(stream.poll(stream.context)==-1);
    CHECK(stream.open(stream.context,"cancel1.example",1883));stream.close(stream.context);
    CHECK(stream.open(stream.context,"cancel2.example",1883));stream.close(stream.context);
    CHECK(!stream.open(stream.context,"bounded.example",1883));CHECK(request_count==5);
    complete(3,5);complete(4,6);CHECK(connection_count==3);
    CHECK(stream.open(stream.context,"broker.example",1883));connect_result=ERR_MEM;complete(5,7);CHECK(stream.poll(stream.context)==-1&&connection_count==4);
    puts("Pico MQTT DNS lifecycle contracts passed");return 0;
}
