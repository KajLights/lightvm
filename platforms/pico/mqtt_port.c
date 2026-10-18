#include "mqtt_port.h"
#include "lwip/tcp.h"
#include <string.h>
static void close_stream(void *context) {
    PicoMqtt *s=context;
    if(s->pcb){tcp_arg(s->pcb,NULL);tcp_recv(s->pcb,NULL);tcp_err(s->pcb,NULL);tcp_abort(s->pcb);s->pcb=NULL;}
    if(s->input)pbuf_free(s->input);
    s->input=NULL;s->offset=0;s->state=-1;
}
static void error_stream(void *context,err_t error) {
    PicoMqtt *s=context;(void)error;s->pcb=NULL;s->state=-1;
}
static err_t received(void *context,struct tcp_pcb *pcb,struct pbuf *p,err_t error) {
    PicoMqtt *s=context;(void)pcb;
    if(!p){s->state=-1;return ERR_OK;}
    if(error!=ERR_OK || s->input)return ERR_MEM;
    s->input=p;s->offset=0;return ERR_OK;
}
static err_t connected(void *context,struct tcp_pcb *pcb,err_t error) {
    PicoMqtt *s=context;(void)pcb;s->state=error==ERR_OK?1:-1;return ERR_OK;
}
static bool open_stream(void *context,const char *host,uint16_t port) {
    PicoMqtt *s=context;close_stream(s);ip_addr_t ip;
    if(!ipaddr_aton(host,&ip))return false;
    s->pcb=tcp_new_ip_type(IPADDR_TYPE_V4);if(!s->pcb)return false;
    s->state=0;tcp_arg(s->pcb,s);tcp_err(s->pcb,error_stream);tcp_recv(s->pcb,received);
    if(tcp_connect(s->pcb,&ip,port,connected)!=ERR_OK){close_stream(s);return false;}
    return true;
}
static int poll_stream(void *context) {return ((PicoMqtt *)context)->state;}
static int read_stream(void *context,uint8_t *out,size_t capacity) {
    PicoMqtt *s=context;
    if(!s->input)return s->state<0?-1:0;
    size_t n=s->input->tot_len-s->offset;if(n>capacity)n=capacity;
    pbuf_copy_partial(s->input,out,(u16_t)n,(u16_t)s->offset);s->offset+=(unsigned)n;
    if(s->pcb)tcp_recved(s->pcb,(u16_t)n);
    if(s->offset==s->input->tot_len){pbuf_free(s->input);s->input=NULL;s->offset=0;}
    return (int)n;
}
static int write_stream(void *context,const uint8_t *data,size_t size) {
    PicoMqtt *s=context;if(!s->pcb || s->state<0)return -1;
    size_t n=tcp_sndbuf(s->pcb);if(n>size)n=size;if(!n)return 0;
    err_t result=tcp_write(s->pcb,data,(u16_t)n,TCP_WRITE_FLAG_COPY);
    if(result==ERR_MEM)return 0;
    if(result!=ERR_OK)return -1;
    result=tcp_output(s->pcb);return result==ERR_OK||result==ERR_MEM?(int)n:-1;
}
MqttStream pico_mqtt_stream(PicoMqtt *s) {
    memset(s,0,sizeof(*s));s->state=-1;return (MqttStream){s,open_stream,poll_stream,read_stream,write_stream,close_stream};
}
