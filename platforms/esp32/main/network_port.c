#include "network_port.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "dhcpserver/dhcpserver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#if LIGHTVM_HTTP
#include "http.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdatomic.h>
#endif
static App *engine;
static bool initialized,wifi_created,scanning,scan_done,scan_failed,ap_started;
static NetworkMode mode;
static esp_event_handler_instance_t wifi_events,ip_events;
static esp_netif_t *station,*access_point;
static QueueHandle_t events;
static bool observed[NETWORK_LIMIT];
static int failure;
#if LIGHTVM_HTTP
static void stop_discovery(void);
static bool stop_http(void);
#endif
static char hostname[sizeof("lightvm-ffffff")];
static void event(void *ctx,esp_event_base_t base,int32_t id,void *data) {
    (void)ctx; int value=0;
    if (base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE) value=1;
    else if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d=data; value=d->reason==WIFI_REASON_ASSOC_LEAVE?0:-(int)d->reason;
    } else if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) value=2;
    else if (base==WIFI_EVENT && id==WIFI_EVENT_AP_START) value=3;
    else if (base==WIFI_EVENT && id==WIFI_EVENT_AP_STOP) value=4;
    if (value) xQueueSend(events,&value,0);
}
static bool configure(void *ctx,const NetworkConfig *c) {
    (void)ctx;
#if LIGHTVM_HTTP
    if (!stop_http()) return false;
#endif
    if (!c->wifi_enabled && !initialized) return true;
    if (!initialized) {
        // LightVM's snapshot is the sole credential store. NVS is for PHY calibration.
        if (nvs_flash_init()!=ESP_OK || esp_netif_init()!=ESP_OK) return false;
        esp_err_t loop=esp_event_loop_create_default(); if (loop!=ESP_OK && loop!=ESP_ERR_INVALID_STATE) return false;
        if (!events) events=xQueueCreate(16,sizeof(int));
        if (!events) return false;
        if (!station) station=esp_netif_create_default_wifi_sta();
        if (!station) return false;
        uint8_t mac[6];
        if (esp_read_mac(mac,ESP_MAC_WIFI_STA)!=ESP_OK) return false;
        snprintf(hostname,sizeof(hostname),"lightvm-%02x%02x%02x",mac[3],mac[4],mac[5]);
        if (esp_netif_set_hostname(station,hostname)!=ESP_OK) return false;
        wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT(); init.nvs_enable=0;
        if (!wifi_created) { if (esp_wifi_init(&init)!=ESP_OK) return false; wifi_created=true; }
        if (esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK) return false;
        if (!wifi_events && esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,NULL,&wifi_events)!=ESP_OK) return false;
        if (!ip_events && esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL,&ip_events)!=ESP_OK) return false;
        initialized=true;
    }
    esp_wifi_stop(); xQueueReset(events); scan_done=false; scanning=false; failure=0; ap_started=false;
    if (access_point) esp_netif_dhcps_stop(access_point);
    if (!c->wifi_enabled) return true;
    mode=c->mode;
    if (esp_wifi_set_mode(mode==NET_MODE_AP?WIFI_MODE_AP:WIFI_MODE_STA)!=ESP_OK) return false;
    if (esp_wifi_set_country_code(!strcmp(c->country,"XX")?"01":c->country,true)!=ESP_OK) return false;
    if (mode==NET_MODE_AP) {
        if (!access_point) access_point=esp_netif_create_default_wifi_ap();
        if (!access_point) return false;
        esp_netif_dhcps_stop(access_point);
        esp_netif_ip_info_t ip={0};
        IP4_ADDR(&ip.ip,192,168,4,1); IP4_ADDR(&ip.gw,192,168,4,1); IP4_ADDR(&ip.netmask,255,255,255,0);
        if (esp_netif_set_ip_info(access_point,&ip)!=ESP_OK || esp_netif_set_hostname(access_point,hostname)!=ESP_OK) return false;
        dhcps_lease_t leases={.enable=true}; IP4_ADDR(&leases.start_ip,192,168,4,2); IP4_ADDR(&leases.end_ip,192,168,4,5);
        if (esp_netif_dhcps_option(access_point,ESP_NETIF_OP_SET,ESP_NETIF_REQUESTED_IP_ADDRESS,&leases,sizeof(leases))!=ESP_OK) return false;
        snprintf(engine->network.ap_ssid,sizeof(engine->network.ap_ssid),"%s",c->ap_ssid[0]?c->ap_ssid:hostname);
        wifi_config_t config={0};
        memcpy(config.ap.ssid,engine->network.ap_ssid,strlen(engine->network.ap_ssid)); config.ap.ssid_len=strlen(engine->network.ap_ssid);
        memcpy(config.ap.password,c->ap_password,strlen(c->ap_password));
        config.ap.channel=1; config.ap.max_connection=4;
        config.ap.authmode=c->ap_password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
        config.ap.pmf_cfg.required=false;
        if (esp_wifi_set_config(WIFI_IF_AP,&config)!=ESP_OK) return false;
        // Stopping DHCP for reconfiguration leaves it explicitly STOPPED.
        // Re-arm it before AP_START, which brings the netif up asynchronously.
        if (esp_netif_dhcps_start(access_point)!=ESP_OK) return false;
    }
    return esp_wifi_start()==ESP_OK;
}
static bool scan(void *ctx) {
    (void)ctx; failure=0; scan_done=false; scan_failed=false; memset(observed,0,sizeof(observed));
    wifi_scan_config_t config={.show_hidden=true};
    scanning=esp_wifi_scan_start(&config,false)==ESP_OK; return scanning;
}
static int scan_poll(void *ctx,const char *ssid,bool *visible) {
    (void)ctx;
    if (!scan_done) return 0;
    if (scan_failed) return -1;
    if (ssid && visible) { *visible=false; for (unsigned i=0;i<engine->network.config.count;i++) if (!strcmp(ssid,engine->network.config.networks[i].ssid)) *visible=observed[i]; }
    return 1;
}
static bool connect_network(void *ctx,const WifiNetwork *network) {
    (void)ctx; failure=0;
    wifi_config_t config={0}; memcpy(config.sta.ssid,network->ssid,strlen(network->ssid)); memcpy(config.sta.password,network->password,strlen(network->password));
    config.sta.threshold.authmode=network->password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    return esp_wifi_set_config(WIFI_IF_STA,&config)==ESP_OK && esp_wifi_connect()==ESP_OK;
}
static int link_state(void *ctx,char *ip,size_t capacity,char *error,size_t error_capacity) {
    (void)ctx; wifi_ap_record_t ap; esp_netif_ip_info_t info;
    if (mode==NET_MODE_AP) {
        esp_netif_dhcp_status_t dhcp;
        if (initialized && ap_started && esp_netif_is_netif_up(access_point) &&
            esp_netif_dhcps_get_status(access_point,&dhcp)==ESP_OK && dhcp==ESP_NETIF_DHCP_STARTED &&
            esp_netif_get_ip_info(access_point,&info)==ESP_OK && info.ip.addr) {
            snprintf(ip,capacity,IPSTR,IP2STR(&info.ip)); return 1;
        }
        return 0;
    }
    if (initialized && esp_wifi_sta_get_ap_info(&ap)==ESP_OK && esp_netif_get_ip_info(station,&info)==ESP_OK && info.ip.addr) {
        snprintf(ip,capacity,IPSTR,IP2STR(&info.ip)); return 1;
    }
    if (failure) { snprintf(error,error_capacity,"Wi-Fi disconnect reason %d",-failure); return -1; }
    return 0;
}
static void disconnect(void *ctx) {
    (void)ctx;
    if (initialized) {
        if (scanning) esp_wifi_scan_stop();
        if (mode==NET_MODE_AP) {
#if LIGHTVM_HTTP
            stop_discovery();
#endif
            esp_wifi_stop(); ap_started=false; if (access_point) esp_netif_dhcps_stop(access_point);
        }
        else esp_wifi_disconnect();
    }
    scanning=false;
}
static uint32_t random_value(void *ctx) { (void)ctx; return esp_random(); }
#if LIGHTVM_HTTP
static Http http;
static httpd_handle_t handle;
static httpd_handle_t bootstrap_handle;
static Discovery discovery;
static int discovery_socket=-1;
static uint8_t discovery_input[DISCOVERY_PACKET_LIMIT+1];
static bool discovery_send(void *ctx,const uint8_t *bytes,size_t size,const uint8_t ip[4],uint16_t port) {
    (void)ctx; if (discovery_socket<0) return false;
    struct sockaddr_in target={.sin_family=AF_INET,.sin_port=htons(port)};
    memcpy(&target.sin_addr.s_addr,ip,4);
    return sendto(discovery_socket,bytes,size,0,(struct sockaddr *)&target,sizeof(target))==(ssize_t)size;
}
static void stop_discovery(void) {
    discovery_stop(&discovery,platform_time_us());
    if (discovery_socket>=0) close(discovery_socket);
    discovery_socket=-1;
}
static bool start_discovery(uint16_t port) {
    discovery_socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP); if (discovery_socket<0) return false;
    int yes=1,ttl=255; struct in_addr local;
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(DISCOVERY_PORT),.sin_addr={.s_addr=htonl(INADDR_ANY)}};
    struct ip_mreq group={0}; inet_pton(AF_INET,"224.0.0.251",&group.imr_multiaddr);
    if (inet_pton(AF_INET,engine->network.ip,&local)!=1) goto failed;
    group.imr_interface=local;
    if (setsockopt(discovery_socket,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)) ||
        setsockopt(discovery_socket,IPPROTO_IP,IP_MULTICAST_TTL,&ttl,sizeof(ttl)) ||
        setsockopt(discovery_socket,IPPROTO_IP,IP_TTL,&ttl,sizeof(ttl)) ||
        setsockopt(discovery_socket,IPPROTO_IP,IP_MULTICAST_IF,&local,sizeof(local)) ||
        bind(discovery_socket,(struct sockaddr *)&address,sizeof(address)) ||
        setsockopt(discovery_socket,IPPROTO_IP,IP_ADD_MEMBERSHIP,&group,sizeof(group)) ||
        fcntl(discovery_socket,F_SETFL,O_NONBLOCK)) goto failed;
    uint8_t ip[4]; memcpy(ip,&local.s_addr,4);
    discovery_start(&discovery,hostname,engine->device_id,platform_info()->board,ip,port,discovery_send,NULL,platform_time_us());
    if (!discovery.active) goto failed;
    return true;
failed:
    stop_discovery(); return false;
}
static void poll_discovery(uint64_t now,uint16_t port) {
    bool enabled=handle && network_online(&engine->network) && network_http_enabled(&engine->network);
    struct in_addr address={0}; inet_pton(AF_INET,engine->network.ip,&address);
    if (discovery.active && (!enabled || memcmp(&address.s_addr,discovery.devices[0].ip,4) || port!=discovery.devices[0].port)) stop_discovery();
    static uint64_t retry;
    if (enabled && !discovery.active && now>=retry) { start_discovery(port); retry=now+5000000u; }
    if (discovery_socket<0) return;
    // Bound ingress per engine step, including on a busy multicast network.
    for (unsigned i=0;i<4;i++) {
        struct sockaddr_in from; socklen_t length=sizeof(from);
        ssize_t n=recvfrom(discovery_socket,discovery_input,sizeof(discovery_input),0,(struct sockaddr *)&from,&length);
        if (n<0) break;
        if ((size_t)n<=DISCOVERY_PACKET_LIMIT) {
            uint8_t ip[4]; memcpy(ip,&from.sin_addr.s_addr,4);
            discovery_receive(&discovery,discovery_input,(size_t)n,ip,ntohs(from.sin_port),now);
        }
    }
    discovery_tick(&discovery,now);
}
static QueueHandle_t ingress,transmit;
typedef struct { httpd_req_t *request; atomic_bool occupied,failed,done; bool closing; } Client;
static Client clients[6];
static atomic_bool stopping;
typedef struct { Client *client; HttpRequest request; } Incoming;
typedef struct { Client *client; size_t size; uint8_t bytes[512]; } Outgoing;
static void sender(void *ctx) {
    (void)ctx; Outgoing job;
    for (;;) {
        if (!xQueueReceive(transmit,&job,portMAX_DELAY)) continue;
        Client *c=job.client;
        if (!job.size) {
            int fd=httpd_req_to_sockfd(c->request); httpd_handle_t server=c->request->handle;
            httpd_req_async_handler_complete(c->request); httpd_sess_trigger_close(server,fd);
            atomic_store(&c->done,true); continue;
        }
        if (atomic_load(&c->failed)) continue;
        size_t sent=0;
        while (sent<job.size) {
            int n=httpd_socket_send(c->request->handle,httpd_req_to_sockfd(c->request),(const char *)job.bytes+sent,job.size-sent,0);
            if (n<=0) { atomic_store(&c->failed,true); break; } sent+=(size_t)n;
        }
    }
}
static int output(void *ctx,const uint8_t *bytes,size_t size) {
    Client *c=ctx; if (atomic_load(&c->failed)) return -1;
    Outgoing job={.client=c,.size=size}; memcpy(job.bytes,bytes,size);
    return xQueueSend(transmit,&job,0)==pdTRUE?(int)size:0;
}
static bool close_client(void *ctx) {
    Client *c=ctx;
    if (!c->closing) { Outgoing job={.client=c}; if (xQueueSend(transmit,&job,0)!=pdTRUE) return false; c->closing=true; }
    if (!atomic_load(&c->done)) return false;
    atomic_store(&c->occupied,false); return true;
}
static esp_err_t request(httpd_req_t *r) {
    if (atomic_load(&stopping)) { httpd_resp_set_status(r,"503 Service Unavailable"); return httpd_resp_send(r,NULL,0); }
    Client *c=NULL;
    for (unsigned i=0;i<6;i++) { bool empty=false; if (atomic_compare_exchange_strong(&clients[i].occupied,&empty,true)) { c=&clients[i]; break; } }
    if (!c) { httpd_resp_set_status(r,"503 Service Unavailable"); return httpd_resp_send(r,NULL,0); }
    atomic_store(&c->failed,false); atomic_store(&c->done,false); c->closing=false;
    Incoming incoming={.client=c}; HttpRequest *q=&incoming.request;
    q->discovery_only=r->handle==bootstrap_handle;
    snprintf(q->method,sizeof(q->method),"%s",r->method==HTTP_POST?"POST":r->method==HTTP_DELETE?"DELETE":r->method==HTTP_GET?"GET":"OPTIONS");
    if (strlen(r->uri)>=sizeof(q->path)) q->error=400; else strcpy(q->path,r->uri);
    if (r->content_len>sizeof(q->body)) q->error=413;
    if (r->method==HTTP_POST && !strcmp(q->path,"/api/v1/command")) {
        char type[64];
        if (httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK || strcmp(type,"application/octet-stream")) q->error=415;
        if (httpd_req_get_hdr_value_str(r,"X-LightVM-Session",q->token,sizeof(q->token))!=ESP_OK) q->error=400;
    }
    if (!q->error) while (q->size<r->content_len) {
        int n=httpd_req_recv(r,(char *)q->body+q->size,r->content_len-q->size);
        if (n<=0) { q->error=400; break; } q->size+=(size_t)n;
    }
    if (httpd_req_async_handler_begin(r,&c->request)!=ESP_OK) { atomic_store(&c->occupied,false); return ESP_FAIL; }
    q->io=(HttpIO){c,output,close_client};
    if (xQueueSend(ingress,&incoming,0)!=pdTRUE) {
        httpd_resp_set_status(c->request,"503 Service Unavailable"); httpd_resp_send(c->request,NULL,0);
        httpd_req_async_handler_complete(c->request); atomic_store(&c->occupied,false);
    }
    return ESP_OK;
}
static bool start_http(uint16_t port) {
    httpd_config_t config=HTTPD_DEFAULT_CONFIG(); config.server_port=port; config.uri_match_fn=httpd_uri_match_wildcard;
    config.max_open_sockets=6; config.recv_wait_timeout=2; config.send_wait_timeout=2; config.stack_size=6144;
    if (httpd_start(&handle,&config)!=ESP_OK) return false;
    const httpd_method_t methods[]={HTTP_GET,HTTP_POST,HTTP_DELETE,HTTP_OPTIONS};
    for (unsigned i=0;i<4;i++) { httpd_uri_t route={.uri="/api/v1/*",.method=methods[i],.handler=request}; if (httpd_register_uri_handler(handle,&route)!=ESP_OK) { httpd_stop(handle); handle=NULL; return false; } }
    if (port!=80) {
        config.server_port=80; config.ctrl_port++; config.max_open_sockets=2; config.stack_size=4096;
        if (httpd_start(&bootstrap_handle,&config)!=ESP_OK) { httpd_stop(handle); handle=NULL; return false; }
        const httpd_method_t bootstrap_methods[]={HTTP_GET,HTTP_OPTIONS};
        for (unsigned i=0;i<2;i++) {
            httpd_uri_t route={.uri="/api/v1/devices",.method=bootstrap_methods[i],.handler=request};
            if (httpd_register_uri_handler(bootstrap_handle,&route)!=ESP_OK) { httpd_stop(bootstrap_handle); bootstrap_handle=NULL; httpd_stop(handle); handle=NULL; return false; }
        }
    }
    return true;
}
static bool stop_http(void) {
    stop_discovery();
    if (!handle) return true;
    if (!atomic_exchange(&stopping,true)) { http_disconnect_sessions(&http); xQueueReset(ingress); }
    bool busy=false;
    for (unsigned i=0;i<6;i++) if (atomic_load(&clients[i].occupied)) {
        atomic_store(&clients[i].failed,true); http_cancel_io(&http,&clients[i]);
        if (!close_client(&clients[i])) busy=true;
    }
    if (busy) return false;
    if (bootstrap_handle) { httpd_stop(bootstrap_handle); bootstrap_handle=NULL; }
    httpd_stop(handle); handle=NULL; atomic_store(&stopping,false); return true;
}
#endif
void esp32_network_init(App *app) {
    engine=app;
    app->network.driver=(NetworkDriver){NULL,configure,scan,scan_poll,connect_network,link_state,disconnect,random_value};
#if LIGHTVM_HTTP
    http_init(&http,app); ingress=xQueueCreate(HTTP_QUEUE,sizeof(Incoming)); transmit=xQueueCreate(4,sizeof(Outgoing));
    http.discovery=&discovery;
    if (!ingress || !transmit || xTaskCreate(sender,"http-output",4096,NULL,5,NULL)!=pdPASS) { strcpy(app->network_error,"HTTP task allocation failed"); }
#endif
    network_bootstrap(&app->network,LIGHTVM_HTTP);
    NetworkConfig saved=app->network.config; network_apply(&app->network,&saved);
}
void esp32_network_poll(void) {
    if (events) {
        int value;
        while (xQueueReceive(events,&value,0)==pdTRUE) {
            if (value==1) {
                scan_done=true; scanning=false; uint16_t count=0; scan_failed=esp_wifi_scan_get_ap_num(&count)!=ESP_OK;
                for (unsigned k=0;k<count && !scan_failed;k++) {
                    wifi_ap_record_t record; scan_failed=esp_wifi_scan_get_ap_record(&record)!=ESP_OK;
                    if (!scan_failed) for (unsigned i=0;i<engine->network.config.count;i++) if (!strcmp((const char *)record.ssid,engine->network.config.networks[i].ssid)) observed[i]=true;
                }
                esp_wifi_clear_ap_list();
            }
            else if (value==3) ap_started=true;
            else if (value==4) ap_started=false;
            else if (value<0) failure=value; else failure=0;
        }
    }
#if LIGHTVM_HTTP
    static NetworkState previous=NET_DISABLED; static uint16_t listening_port;
    if ((previous==NET_CONNECTED || previous==NET_AP_ACTIVE) && !network_online(&engine->network)) http_disconnect_sessions(&http);
    previous=engine->network.state;
    bool enabled=network_http_enabled(&engine->network) && network_online(&engine->network);
    uint16_t requested_port=network_http_port(&engine->network);
    if (handle && !engine->network_pending && (!enabled || listening_port!=requested_port)) {
        if (!stop_http()) return;
    }
    if (!handle && !engine->network_pending && enabled && ingress && transmit) { if (start_http(requested_port)) listening_port=requested_port; else strcpy(engine->network_error,"HTTP listener could not start"); }
    poll_discovery(platform_time_us(),listening_port);
    if (handle) {
        Incoming incoming;
        // Ingress is the four-command queue. Transfer only when the shared
        // engine can execute it, so the two queues do not double capacity.
        if (!http.sending && !http.count && xQueueReceive(ingress,&incoming,0)==pdTRUE) http_submit(&http,&incoming.request);
        http_tick(&http,platform_time_us());
        for (unsigned i=0;i<6;i++) if (clients[i].closing && atomic_load(&clients[i].done) && (!http.sending || http.active.io.context!=&clients[i])) atomic_store(&clients[i].occupied,false);
    }
#endif
}
