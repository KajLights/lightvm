#include "network_port.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"
#include "pico/rand.h"
#include "lwip/tcp.h"
#include <stdio.h>
#include <string.h>
#if LIGHTVM_HTTP
#include "http.h"
#include "lwip/udp.h"
#include "lwip/igmp.h"
#endif
_Static_assert(VM_HEAP_LIMIT==64u*1024u, "Pico W network builds require the 64 KiB heap profile used by the RAM gate");
_Static_assert(PIXEL_CAPACITY<=1024u, "Pico W RAM gate reserves at most 1024 physical pixels");
static App *engine;
static bool initialized;
static bool observed[NETWORK_LIMIT];
static char hostname[sizeof("lightvm-ffffff")];
#if LIGHTVM_HTTP
static void stop_http(void);
static void stop_discovery(void);
#endif
static int result(void *ctx,const cyw43_ev_scan_result_t *r) {
    (void)ctx;
    for (unsigned i=0;i<engine->network.config.count;i++) {
        const char *ssid=engine->network.config.networks[i].ssid;
        if (strlen(ssid)==r->ssid_len && !memcmp(ssid,r->ssid,r->ssid_len)) observed[i]=true;
    }
    return 0;
}
static void enable_station(void) {
    cyw43_arch_enable_sta_mode();
    const uint8_t *mac=cyw43_state.netif[CYW43_ITF_STA].hwaddr;
    snprintf(hostname,sizeof(hostname),"lightvm-%02x%02x%02x",mac[3],mac[4],mac[5]);
    // lwIP retains this pointer; DHCP waits for the link before advertising it.
    netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA],hostname);
}
static bool configure(void *ctx,const NetworkConfig *c) {
    (void)ctx;
    uint32_t country=!strcmp(c->country,"XX")?CYW43_COUNTRY_WORLDWIDE:CYW43_COUNTRY(c->country[0],c->country[1],0);
    if (initialized && c->wifi_enabled && cyw43_arch_get_country_code()==country) { enable_station(); return true; }
    if (initialized) {
#if LIGHTVM_HTTP
        stop_http();
#endif
        cyw43_arch_deinit(); initialized=false;
    }
    if (!c->wifi_enabled) return true;
    if (cyw43_arch_init_with_country(country)) return false;
    initialized=true; enable_station(); return true;
}
static bool scan(void *ctx) {
    (void)ctx; memset(observed,0,sizeof(observed)); cyw43_wifi_scan_options_t options={0};
    return initialized && cyw43_wifi_scan(&cyw43_state,&options,NULL,result)==0;
}
static int scan_poll(void *ctx,const char *ssid,bool *visible) {
    (void)ctx; if (!initialized) return -1; if (cyw43_wifi_scan_active(&cyw43_state)) return 0;
    if (ssid && visible) { *visible=false; for (unsigned i=0;i<engine->network.config.count;i++) if (!strcmp(ssid,engine->network.config.networks[i].ssid)) *visible=observed[i]; }
    return 1;
}
static bool connect(void *ctx,const WifiNetwork *n) { (void)ctx; return cyw43_arch_wifi_connect_async(n->ssid,n->password,n->password[0]?CYW43_AUTH_WPA2_AES_PSK:CYW43_AUTH_OPEN)==0; }
static int link(void *ctx,char *ip,size_t capacity,char *error,size_t error_capacity) {
    (void)ctx; if (!initialized) return -1;
    int state=cyw43_tcpip_link_status(&cyw43_state,CYW43_ITF_STA);
    if (state==CYW43_LINK_UP) { snprintf(ip,capacity,"%s",ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]))); return 1; }
    if (state<0) { snprintf(error,error_capacity,"Wi-Fi link failure %d",state); return -1; }
    return 0;
}
static void disconnect(void *ctx) { (void)ctx; if (initialized) cyw43_wifi_leave(&cyw43_state,CYW43_ITF_STA); }
static uint32_t random_value(void *ctx) { (void)ctx; return get_rand_32(); }
#if LIGHTVM_HTTP
static Http http;
static Discovery discovery;
static struct udp_pcb *discovery_pcb;
static uint8_t discovery_input[DISCOVERY_PACKET_LIMIT];
static ip4_addr_t discovery_group;
static bool discovery_send(void *ctx,const uint8_t *bytes,size_t size,const uint8_t ip[4],uint16_t port) {
    (void)ctx; if (!discovery_pcb) return false;
    struct pbuf *p=pbuf_alloc(PBUF_TRANSPORT,(u16_t)size,PBUF_RAM); if (!p) return false;
    ip_addr_t target; IP_ADDR4(&target,ip[0],ip[1],ip[2],ip[3]);
    err_t result=pbuf_take(p,bytes,(u16_t)size);
    if (result==ERR_OK) result=udp_sendto_if(discovery_pcb,p,&target,port,&cyw43_state.netif[CYW43_ITF_STA]);
    pbuf_free(p); return result==ERR_OK;
}
static void discovery_received(void *ctx,struct udp_pcb *pcb,struct pbuf *p,const ip_addr_t *address,u16_t port) {
    (void)ctx; (void)pcb;
    if (p && IP_IS_V4(address) && p->tot_len<=sizeof(discovery_input) && pbuf_copy_partial(p,discovery_input,p->tot_len,0)==p->tot_len) {
        const ip4_addr_t *a=ip_2_ip4(address); uint8_t ip[4]={ip4_addr1(a),ip4_addr2(a),ip4_addr3(a),ip4_addr4(a)};
        discovery_receive(&discovery,discovery_input,p->tot_len,ip,port,platform_time_us());
    }
    if (p) pbuf_free(p);
}
static void stop_discovery(void) {
    discovery_stop(&discovery,platform_time_us());
    if (discovery_pcb) {
        igmp_leavegroup_netif(&cyw43_state.netif[CYW43_ITF_STA],&discovery_group);
        udp_remove(discovery_pcb); discovery_pcb=NULL;
    }
}
static bool start_discovery(uint16_t port) {
    struct netif *netif=&cyw43_state.netif[CYW43_ITF_STA];
    discovery_pcb=udp_new_ip_type(IPADDR_TYPE_V4); if (!discovery_pcb) return false;
    IP4_ADDR(&discovery_group,224,0,0,251);
    if (udp_bind(discovery_pcb,IP_ANY_TYPE,DISCOVERY_PORT)!=ERR_OK || igmp_joingroup_netif(netif,&discovery_group)!=ERR_OK) {
        udp_remove(discovery_pcb); discovery_pcb=NULL; return false;
    }
    discovery_pcb->ttl=255; udp_set_multicast_ttl(discovery_pcb,255);
    udp_recv(discovery_pcb,discovery_received,NULL);
    const ip4_addr_t *a=netif_ip4_addr(netif); uint8_t ip[4]={ip4_addr1(a),ip4_addr2(a),ip4_addr3(a),ip4_addr4(a)};
    discovery_start(&discovery,hostname,engine->device_id,platform_info()->board,ip,port,discovery_send,NULL,platform_time_us());
    if (!discovery.active) { stop_discovery(); return false; }
    return true;
}
static struct tcp_pcb *listener;
static struct tcp_pcb *bootstrap_listener;
typedef struct { struct tcp_pcb *pcb; HttpInput input; HttpRequest request; uint64_t deadline,close_deadline; bool closing; } Client;
static Client clients[4];
static int output(void *ctx,const uint8_t *bytes,size_t size) {
    Client *c=ctx; if (!c->pcb) return -1;
    if (tcp_sndbuf(c->pcb)<size) return 0;
    err_t error=tcp_write(c->pcb,bytes,(u16_t)size,TCP_WRITE_FLAG_COPY);
    if (error==ERR_MEM) return 0;
    if (error!=ERR_OK) return -1;
    tcp_output(c->pcb); return (int)size;
}
static void failed(void *,err_t);
static err_t receive(void *,struct tcp_pcb *,struct pbuf *,err_t);
static bool close_client(void *ctx) {
    Client *c=ctx; if (!c->pcb) return true;
    struct tcp_pcb *pcb=c->pcb;
    tcp_arg(pcb,NULL); tcp_recv(pcb,NULL); tcp_err(pcb,NULL); tcp_poll(pcb,NULL,0);
    if (tcp_close(pcb)!=ERR_OK) {
        tcp_arg(pcb,c); tcp_recv(pcb,receive); tcp_err(pcb,failed);
        if (!c->closing) { c->closing=true; c->close_deadline=platform_time_us()+2000000u; }
        return false;
    }
    c->pcb=NULL; return true;
}
static void failed(void *ctx,err_t error) { (void)error; Client *c=ctx; c->pcb=NULL; http_cancel_io(&http,c); }
static err_t receive(void *ctx,struct tcp_pcb *pcb,struct pbuf *p,err_t error) {
    Client *c=ctx;
    if (!p && c->input.ready) return ERR_OK; // A complete request can half-close its input.
    if (!p || error!=ERR_OK) { if (p) pbuf_free(p); http_cancel_io(&http,c); close_client(c); return ERR_OK; }
    tcp_recved(pcb,p->tot_len);
    for (struct pbuf *part=p;part;part=part->next) {
        if (c->input.ready) break;
        c->deadline=platform_time_us()+2000000u;
        if (http_input(&c->input,part->payload,part->len,&c->request) && !http_submit(&http,&c->request)) {
            const char *busy="HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
            output(c,(const uint8_t *)busy,strlen(busy)); close_client(c); break;
        }
    }
    pbuf_free(p); return ERR_OK;
}
static err_t accept_client(void *ctx,struct tcp_pcb *pcb,err_t error) {
    (void)ctx; if (error!=ERR_OK) return error;
    Client *c=NULL; for (unsigned i=0;i<4;i++) if (!clients[i].pcb) { c=&clients[i]; break; }
    if (!c) { tcp_abort(pcb); return ERR_ABRT; }
    memset(c,0,sizeof(*c)); c->pcb=pcb; c->deadline=platform_time_us()+2000000u; c->request.io=(HttpIO){c,output,close_client};
    c->request.discovery_only=pcb->local_port==80 && engine->network.config.port!=80;
    tcp_arg(pcb,c); tcp_recv(pcb,receive); tcp_err(pcb,failed); return ERR_OK;
}
static void stop_http(void) {
    stop_discovery();
    for (unsigned i=0;i<4;i++) {
        Client *c=&clients[i]; http_cancel_io(&http,c);
        if (!close_client(c) && c->pcb) { tcp_arg(c->pcb,NULL); tcp_err(c->pcb,NULL); tcp_abort(c->pcb); c->pcb=NULL; }
    }
    if (listener) { tcp_close(listener); listener=NULL; }
    if (bootstrap_listener) { tcp_close(bootstrap_listener); bootstrap_listener=NULL; }
    http_disconnect_sessions(&http);
}
static struct tcp_pcb *listen_http(uint16_t port) {
    struct tcp_pcb *pcb=tcp_new_ip_type(IPADDR_TYPE_V4); if (!pcb) return NULL;
    if (tcp_bind(pcb,IP_ANY_TYPE,port)!=ERR_OK) { tcp_close(pcb); return NULL; }
    struct tcp_pcb *listening=tcp_listen_with_backlog(pcb,4); if (!listening) { tcp_close(pcb); return NULL; }
    tcp_accept(listening,accept_client); return listening;
}
static bool start_http(uint16_t port) {
    listener=listen_http(port); if (!listener) return false;
    if (port!=80) {
        bootstrap_listener=listen_http(80);
        if (!bootstrap_listener) { tcp_close(listener); listener=NULL; return false; }
    }
    return true;
}
#endif
void pico_network_init(App *app) {
    engine=app;
    app->network.driver=(NetworkDriver){NULL,configure,scan,scan_poll,connect,link,disconnect,random_value};
#if LIGHTVM_HTTP
    http_init(&http,app);
    http.discovery=&discovery;
#endif
    NetworkConfig saved=app->network.config; network_apply(&app->network,&saved);
}
void pico_network_poll(void) {
    if (initialized) cyw43_arch_poll();
#if LIGHTVM_HTTP
    static NetworkState previous=NET_DISABLED; static uint16_t port;
    if (previous==NET_CONNECTED && engine->network.state!=NET_CONNECTED) stop_http();
    previous=engine->network.state;
    bool enabled=engine->network.state==NET_CONNECTED && engine->network.config.http_enabled;
    if (!engine->network_pending) {
        if (listener && (!enabled || port!=engine->network.config.port)) stop_http();
        if (!listener && enabled) { if (start_http(engine->network.config.port)) port=engine->network.config.port; else strcpy(engine->network_error,"HTTP listener could not start"); }
    }
    uint64_t now=platform_time_us();
    static uint64_t discovery_retry;
    if (listener && enabled) {
        const ip4_addr_t *a=netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]);
        uint8_t ip[4]={ip4_addr1(a),ip4_addr2(a),ip4_addr3(a),ip4_addr4(a)};
        if (discovery.active && memcmp(ip,discovery.devices[0].ip,4)) stop_discovery();
        if (!discovery.active && now>=discovery_retry) { start_discovery(port); discovery_retry=now+5000000u; }
        discovery_tick(&discovery,now);
    }
    for (unsigned i=0;i<4;i++) {
        Client *c=&clients[i];
        if (c->pcb && !c->input.ready && now>=c->deadline) { http_cancel_io(&http,c); close_client(c); }
        if (c->pcb && c->closing && !close_client(c) && now>=c->close_deadline) { tcp_arg(c->pcb,NULL); tcp_err(c->pcb,NULL); tcp_abort(c->pcb); c->pcb=NULL; }
    }
    if (listener) http_tick(&http,now);
#endif
}
