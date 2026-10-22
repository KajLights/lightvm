#include "network.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
void *platform_calloc(size_t count,size_t size){return calloc(count,size);}
void platform_free(void *p){free(p);}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
typedef struct { unsigned scans,connections,configurations; int link; bool reject_next,reject_config; char tried[64][33]; NetworkConfig applied; } Radio;
static bool configure(void *ctx,const NetworkConfig *config) { Radio *r=ctx; r->applied=*config; r->configurations++; return !r->reject_config; }
static bool scan(void *ctx) { ((Radio *)ctx)->scans++; return true; }
static int scan_poll(void *ctx,const char *ssid,bool *visible) { (void)ctx; if (visible) *visible=strcmp(ssid,"missing")!=0; return 1; }
static bool connect(void *ctx,const WifiNetwork *n) { Radio *r=ctx; strcpy(r->tried[r->connections++],n->ssid); r->link=0; if (r->reject_next) { r->reject_next=false; return false; } return true; }
static int link(void *ctx,char *ip,size_t capacity,char *error,size_t error_capacity) { (void)error; (void)error_capacity; Radio *r=ctx; if (r->link==1) snprintf(ip,capacity,"192.168.1.2"); return r->link; }
static void disconnect(void *ctx) { (void)ctx; }
static uint32_t random_value(void *ctx) { (void)ctx; return 20; } // jitter midpoint = 100%
int main(void) {
    static const struct {const char *host;bool valid;} hosts[]={
        {"mqtt.example.com",true},{"localhost",true},{"192.168.1.10",true},
        {"a-b.example",true},{"",false},{"256.0.0.1",false},{"127.00.0.1",false},
        {"broker.local:1883",false},{"mqtt://broker.local",false},{"a..b",false},
        {"-a.example",false},{"a-.example",false},{"a_b.example",false},{"[::1]",false},
        {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.example",false}
    };
    for(unsigned i=0;i<sizeof(hosts)/sizeof(hosts[0]);i++){
        char document[512],error[160];NetworkConfig candidate;
        int n=snprintf(document,sizeof(document),"{\"schema\":\"lightvm.network\",\"wifi\":{\"networks\":[]},\"http\":{},\"mqtt\":{\"enabled\":true,\"host\":\"%s\"}}",hosts[i].host);
        CHECK(network_parse(document,(size_t)n,NULL,&candidate,error,sizeof(error))==hosts[i].valid);
    }

    const char *json="{\"schema\":\"lightvm.network\",\"wifi\":{\"enabled\":true,\"networks\":[{\"ssid\":\"missing\",\"password\":\"\"},{\"ssid\":\"caf\\u00e9\\ud83c\\udf08\",\"password\":\"secret123\"},{\"ssid\":\"hidden\",\"password\":\"\",\"hidden\":true}]},\"http\":{\"enabled\":true}}";
    NetworkConfig config,copy; char error[160],bytes[2048];
    CHECK(network_parse(json,strlen(json),NULL,&config,error,sizeof(error)));
    CHECK(config.mode==NET_MODE_STATION && !config.ap_password[0]);
    CHECK(!strcmp(config.networks[1].ssid,"caf\xc3\xa9\xf0\x9f\x8c\x88"));
    size_t size=network_json(&config,true,bytes,sizeof(bytes)); CHECK(size && network_parse(bytes,size,NULL,&copy,error,sizeof(error)));
    CHECK(!memcmp(&config,&copy,sizeof(config)));
    size=network_json(&config,false,bytes,sizeof(bytes)); CHECK(!strstr(bytes,"secret123"));
    CHECK(network_parse(bytes,size,&config,&copy,error,sizeof(error)) && !strcmp(copy.networks[1].password,"secret123"));
    CHECK(!network_parse(bytes,size,NULL,&copy,error,sizeof(error)));
    const char *invalid="{\"schema\":\"lightvm.network\",\"wifi\":{\"networks\":[{\"ssid\":\"\\ud800\",\"password\":\"\"}]},\"http\":{}}";
    CHECK(!network_parse(invalid,strlen(invalid),NULL,&copy,error,sizeof(error)));
    Radio radio={0}; Network n; network_init(&n);
    n.driver=(NetworkDriver){&radio,configure,scan,scan_poll,connect,link,disconnect,random_value}; network_apply(&n,&config);
    network_tick(&n,0); CHECK(n.state==NET_SCANNING && radio.scans==1);
    network_tick(&n,1); CHECK(n.state==NET_CONNECTING && radio.connections==1 && !strcmp(radio.tried[0],config.networks[1].ssid));
    radio.link=-1; network_tick(&n,2); CHECK(radio.connections==2 && !strcmp(radio.tried[1],"hidden"));
    network_tick(&n,15000003u); CHECK(n.state==NET_BACKOFF && n.retry_at==20000003u);
    network_tick(&n,20000002u); CHECK(radio.scans==1);
    network_tick(&n,20000003u); network_tick(&n,20000004u); radio.link=1; network_tick(&n,20000005u);
    CHECK(n.state==NET_CONNECTED && n.failures==0);
    network_tick(&n,999999999u); CHECK(radio.scans==2); // healthy connections do not roam
    radio.link=-1; network_tick(&n,1000000000u); CHECK(n.state==NET_CONNECTING && n.quick_retry);
    radio.link=-1; network_tick(&n,1000000001u); CHECK(n.state==NET_BACKOFF && n.retry_at==1000000001u);
    network_tick(&n,1000000002u); CHECK(radio.scans==3);
    // Cap repeated exhaustion at 60 seconds; hidden candidates still get a try.
    uint64_t now=1000000003u;
    for (unsigned i=0;i<8;i++) {
        network_tick(&n,now++); radio.link=-1; network_tick(&n,now++); radio.link=-1; network_tick(&n,now++);
        CHECK(n.state==NET_BACKOFF); uint64_t delay=n.retry_at-(now-1); CHECK(delay<=60000000u);
        if (i>=4) CHECK(delay==60000000u);
        now=n.retry_at; network_tick(&n,now++);
    }
    // A rejected quick reconnect resumes the ordered scan immediately, too.
    Radio rejected={0}; network_init(&n);
    n.driver=(NetworkDriver){&rejected,configure,scan,scan_poll,connect,link,disconnect,random_value};
    network_apply(&n,&config); network_tick(&n,0); network_tick(&n,1);
    rejected.link=1; network_tick(&n,2); CHECK(n.state==NET_CONNECTED);
    rejected.link=-1; rejected.reject_next=true; network_tick(&n,3);
    CHECK(n.state==NET_BACKOFF && n.retry_at==3 && !n.quick_retry && rejected.connections==2);
    network_tick(&n,4); CHECK(n.state==NET_SCANNING && rejected.scans==2);
    // AP configuration can retain both station networks and AP credentials.
    const char *ap="{\"schema\":\"lightvm.network\",\"wifi\":{\"enabled\":true,\"mode\":\"ap\",\"ap\":{\"ssid\":\"caf\\u00e9\\ud83c\\udf08\",\"password\":\"ap-secret\"}},\"http\":{\"enabled\":true}}";
    CHECK(network_parse(ap,strlen(ap),&config,&copy,error,sizeof(error)) && copy.count==config.count && copy.mode==NET_MODE_AP);
    CHECK(!strcmp(copy.networks[1].password,"secret123") && !strcmp(copy.ap_password,"ap-secret"));
    config=copy;
    size=network_json(&config,true,bytes,sizeof(bytes)); CHECK(network_parse(bytes,size,NULL,&copy,error,sizeof(error)) && !memcmp(&config,&copy,sizeof(config)));
    size=network_json(&config,false,bytes,sizeof(bytes)); CHECK(!strstr(bytes,"secret") && network_parse(bytes,size,&config,&copy,error,sizeof(error)) && !strcmp(copy.ap_password,"ap-secret"));
    const char *station="{\"schema\":\"lightvm.network\",\"wifi\":{\"enabled\":true,\"mode\":\"station\"},\"http\":{}}";
    CHECK(network_parse(station,strlen(station),&config,&copy,error,sizeof(error)) && copy.count==3 && !strcmp(copy.ap_password,"ap-secret"));
    const char *default_ssid="{\"schema\":\"lightvm.network\",\"wifi\":{\"mode\":\"ap\",\"ap\":{}},\"http\":{}}";
    CHECK(network_parse(default_ssid,strlen(default_ssid),&config,&copy,error,sizeof(error)) && !copy.ap_ssid[0] && !strcmp(copy.ap_password,"ap-secret"));
    const char *open="{\"schema\":\"lightvm.network\",\"wifi\":{\"enabled\":true,\"mode\":\"ap\",\"ap\":{\"ssid\":\"\",\"password\":\"\"}},\"http\":{}}";
    CHECK(network_parse(open,strlen(open),&config,&copy,error,sizeof(error)) && !copy.ap_password[0] && !copy.ap_ssid[0]);
    const char *bad[]={
        "{\"schema\":\"lightvm.network\",\"wifi\":{\"mode\":\"both\"},\"http\":{}}",
        "{\"schema\":\"lightvm.network\",\"wifi\":{\"mode\":\"ap\",\"ap\":{\"password\":\"short\"}},\"http\":{}}",
        "{\"schema\":\"lightvm.network\",\"wifi\":{\"mode\":\"ap\",\"ap\":{\"password\":\"1234567\\n\"}},\"http\":{}}",
        "{\"schema\":\"lightvm.network\",\"wifi\":{\"mode\":\"ap\",\"ap\":{\"password\":\"1234567890123456789012345678901234567890123456789012345678901234\"}},\"http\":{}}",
        "{\"schema\":\"lightvm.network\",\"wifi\":{\"mode\":\"ap\",\"ap\":{\"ssid\":\"12345678901234567890123456789012345\"}},\"http\":{}}",
        "{\"schema\":\"lightvm.network\",\"wifi\":{\"mode\":\"ap\",\"ap\":null},\"http\":{}}"
    };
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) CHECK(!network_parse(bad[i],strlen(bad[i]),NULL,&copy,error,sizeof(error)));
    // Bootstrap is unsaved and only enabled by embedded HTTP ports.
    network_init(&n); network_bootstrap(&n,false); CHECK(!n.config.wifi_enabled && !n.initial_setup);
    network_bootstrap(&n,true); CHECK(n.initial_setup && n.config.mode==NET_MODE_AP && n.config.http_enabled && n.config.port==80 && !n.config.ap_password[0]);
    network_init(&n); n.saved_valid=true; network_bootstrap(&n,true); CHECK(!n.initial_setup && !n.config.wifi_enabled);
    Radio ap_radio={.reject_config=true}; network_init(&n);
    n.driver=(NetworkDriver){&ap_radio,configure,scan,scan_poll,connect,link,disconnect,random_value};
    network_apply(&n,&config); network_tick(&n,0); CHECK(n.state==NET_BACKOFF && ap_radio.configurations==2 && !ap_radio.scans);
    ap_radio.reject_config=false; network_tick(&n,n.retry_at); CHECK(n.state==NET_AP_STARTING && !ap_radio.connections);
    ap_radio.link=1; network_tick(&n,n.retry_at+1); CHECK(n.state==NET_AP_ACTIVE && network_online(&n));
    network_tick(&n,n.retry_at+2); CHECK(n.state==NET_AP_ACTIVE && !ap_radio.scans);
    ap_radio.link=-1; network_tick(&n,n.retry_at+3); CHECK(n.state==NET_BACKOFF && !n.ready && !n.ip[0]);
    network_tick(&n,n.retry_at); CHECK(n.state==NET_AP_STARTING);
    ap_radio.link=0; network_tick(&n,n.deadline); CHECK(n.state==NET_BACKOFF && strstr(n.error,"timed out"));
    config.wifi_enabled=false; ap_radio.reject_config=true;
    network_apply(&n,&config); network_tick(&n,0); CHECK(n.state==NET_BACKOFF);
    ap_radio.reject_config=false; network_tick(&n,n.retry_at); CHECK(n.state==NET_DISABLED && !n.error[0]);
    // Recovery overrides an unusable saved profile without replacing its values.
    config.mode=NET_MODE_STATION; config.http_enabled=false; config.port=8080; strcpy(config.country,"ZZ");
    Radio recovery={.link=1}; network_init(&n); n.recovery=true; n.saved_valid=true; n.config=config;
    network_bootstrap(&n,true); CHECK(!n.initial_setup && !memcmp(&n.config,&config,sizeof(config)));
    n.driver=(NetworkDriver){&recovery,configure,scan,scan_poll,connect,link,disconnect,random_value};
    network_apply(&n,&config);
    CHECK(recovery.applied.wifi_enabled && recovery.applied.http_enabled && recovery.applied.mode==NET_MODE_AP && recovery.applied.port==80);
    CHECK(!recovery.applied.ap_ssid[0] && !recovery.applied.ap_password[0] && !strcmp(recovery.applied.country,"XX"));
    CHECK(!memcmp(&n.config,&config,sizeof(config)) && network_mode(&n)==NET_MODE_AP && network_http_enabled(&n) && network_http_port(&n)==80);
    network_tick(&n,0); network_tick(&n,1); CHECK(n.state==NET_AP_ACTIVE && !recovery.scans);
    recovery.link=-1; network_tick(&n,2); CHECK(n.state==NET_BACKOFF && n.recovery);
    network_tick(&n,n.retry_at); CHECK(n.state==NET_AP_STARTING && recovery.configurations==2 && recovery.applied.port==80);
    CHECK(!memcmp(&n.config,&config,sizeof(config)));
    return 0;
}
