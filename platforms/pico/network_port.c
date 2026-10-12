#include "network_port.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"
#include "pico/rand.h"
#include "lwip/tcp.h"
#include <stdio.h>
#include <string.h>
#if LIGHTVM_HTTP
#include "http.h"
#endif
_Static_assert(VM_HEAP_LIMIT==64u*1024u, "Pico W network builds require the 64 KiB heap profile used by the RAM gate");
_Static_assert(PIXEL_CAPACITY<=1024u, "Pico W RAM gate reserves at most 1024 physical pixels");
static App *engine;
static bool initialized;
static bool observed[NETWORK_LIMIT];
static char hostname[sizeof("lightvm-ffffff")];
#if LIGHTVM_HTTP
static void stop_http(void);
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
static struct tcp_pcb *listener;
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
    tcp_arg(pcb,c); tcp_recv(pcb,receive); tcp_err(pcb,failed); return ERR_OK;
}
static void stop_http(void) {
    for (unsigned i=0;i<4;i++) {
        Client *c=&clients[i]; http_cancel_io(&http,c);
        if (!close_client(c) && c->pcb) { tcp_arg(c->pcb,NULL); tcp_err(c->pcb,NULL); tcp_abort(c->pcb); c->pcb=NULL; }
    }
    if (listener) { tcp_close(listener); listener=NULL; }
    http_disconnect_sessions(&http);
}
static bool start_http(uint16_t port) {
    struct tcp_pcb *pcb=tcp_new_ip_type(IPADDR_TYPE_V4); if (!pcb) return false;
    if (tcp_bind(pcb,IP_ANY_TYPE,port)!=ERR_OK) { tcp_close(pcb); return false; }
    listener=tcp_listen_with_backlog(pcb,4); if (!listener) { tcp_close(pcb); return false; }
    tcp_accept(listener,accept_client); return true;
}
#endif
void pico_network_init(App *app) {
    engine=app;
    app->network.driver=(NetworkDriver){NULL,configure,scan,scan_poll,connect,link,disconnect,random_value};
#if LIGHTVM_HTTP
    http_init(&http,app);
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
    for (unsigned i=0;i<4;i++) {
        Client *c=&clients[i];
        if (c->pcb && !c->input.ready && now>=c->deadline) { http_cancel_io(&http,c); close_client(c); }
        if (c->pcb && c->closing && !close_client(c) && now>=c->close_deadline) { tcp_arg(c->pcb,NULL); tcp_err(c->pcb,NULL); tcp_abort(c->pcb); c->pcb=NULL; }
    }
    if (listener) http_tick(&http,now);
#endif
}
