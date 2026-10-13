#include "network.h"
#include "json.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
static bool fail(char *error,size_t capacity,const char *message) { snprintf(error,capacity,"%s",message); return false; }
static bool optional_bool(const Json *j,int object,const char *key,bool *value) {
    int t=json_get(j,object,key); return t<0 || json_bool(j,t,value);
}
bool network_parse(const void *bytes,size_t size,const NetworkConfig *previous,NetworkConfig *out,char *error,size_t capacity) {
    Json j; memset(out,0,sizeof(*out)); strcpy(out->country,"XX"); out->port=80;
    static const char *const root[]={"schema","wifi","http",NULL};
    static const char *const wifi[]={"enabled","country","networks","mode","ap",NULL};
    static const char *const ap_fields[]={"ssid","password",NULL};
    static const char *const http[]={"enabled","port",NULL};
    static const char *const item[]={"ssid","password","hidden",NULL};
    if (size>CONFIG_LIMIT || !json_parse(&j,bytes,size) || !json_fields(&j,0,root) || !json_equal(&j,json_get(&j,0,"schema"),"lightvm.network/1"))
        return fail(error,capacity,"network: expected bounded lightvm.network/1 JSON");
    int w=json_get(&j,0,"wifi"),h=json_get(&j,0,"http");
    if (!json_fields(&j,w,wifi) || !json_fields(&j,h,http) || !optional_bool(&j,w,"enabled",&out->wifi_enabled) || !optional_bool(&j,h,"enabled",&out->http_enabled))
        return fail(error,capacity,"network: invalid wifi/http fields");
    int mode=json_get(&j,w,"mode"),ap=json_get(&j,w,"ap");
    if (mode>=0) {
        if (json_equal(&j,mode,"ap")) out->mode=NET_MODE_AP;
        else if (!json_equal(&j,mode,"station")) return fail(error,capacity,"wifi.mode: expected station or ap");
    }
    if (previous) {
        strcpy(out->ap_ssid,previous->ap_ssid);
        strcpy(out->ap_password,previous->ap_password);
    }
    if (ap>=0) {
        if (!json_fields(&j,ap,ap_fields)) return fail(error,capacity,"wifi.ap: invalid fields");
        int ssid=json_get(&j,ap,"ssid"),password=json_get(&j,ap,"password");
        out->ap_ssid[0]=0; // An explicitly supplied AP object defaults to the device name.
        if (ssid>=0 && !json_string(&j,ssid,out->ap_ssid,sizeof(out->ap_ssid))) return fail(error,capacity,"wifi.ap.ssid: expected at most 32 UTF-8 bytes; empty selects device name");
        if (password>=0 && !json_string(&j,password,out->ap_password,sizeof(out->ap_password))) return fail(error,capacity,"wifi.ap.password: invalid string");
    }
    size_t ap_length=strlen(out->ap_password);
    if (ap_length && ap_length<8) return fail(error,capacity,"wifi.ap.password: expected 8..63 printable ASCII characters or empty for open");
    for (size_t i=0;i<ap_length;i++) if ((unsigned char)out->ap_password[i]<32 || (unsigned char)out->ap_password[i]>126)
        return fail(error,capacity,"wifi.ap.password: expected printable ASCII");
    int country=json_get(&j,w,"country");
    if (country>=0 && (!json_string(&j,country,out->country,sizeof(out->country)) || strlen(out->country)!=2 || out->country[0]<'A' || out->country[0]>'Z' || out->country[1]<'A' || out->country[1]>'Z'))
        return fail(error,capacity,"wifi.country: expected two uppercase letters or XX");
    uint32_t port=80; int p=json_get(&j,h,"port");
    if (p>=0 && (!json_u32(&j,p,&port) || !port || port>65535)) return fail(error,capacity,"http.port: expected 1..65535");
    out->port=(uint16_t)port;
    int list=json_get(&j,w,"networks");
    if ((list<0 && out->mode!=NET_MODE_AP && !previous) || (list>=0 && (j.tokens[list].type!=JSON_ARRAY || j.tokens[list].children>(int)NETWORK_LIMIT))) return fail(error,capacity,"wifi.networks: expected at most eight entries");
    if (list<0 && previous) { out->count=previous->count; memcpy(out->networks,previous->networks,sizeof(out->networks)); }
    for (int t=list<0?0:list+1;list>=0 && t<j.tokens[list].next;t=j.tokens[t].next) {
        WifiNetwork *n=&out->networks[out->count++];
        if (!json_fields(&j,t,item) || !json_string(&j,json_get(&j,t,"ssid"),n->ssid,sizeof(n->ssid)) || !n->ssid[0] || !optional_bool(&j,t,"hidden",&n->hidden))
            return fail(error,capacity,"wifi.networks: SSID must be 1..32 UTF-8 bytes");
        for (unsigned i=0;i+1<out->count;i++) if (!strcmp(n->ssid,out->networks[i].ssid)) return fail(error,capacity,"wifi.networks: duplicate SSID");
        int password=json_get(&j,t,"password");
        if (password>=0) { if (!json_string(&j,password,n->password,sizeof(n->password))) return fail(error,capacity,"wifi.password: invalid string"); }
        else {
            bool found=false;
            if (previous) for (unsigned i=0;i<previous->count;i++) if (!strcmp(n->ssid,previous->networks[i].ssid)) { strcpy(n->password,previous->networks[i].password); found=true; }
            if (!found) return fail(error,capacity,"wifi.password: required for a new SSID; empty selects open");
        }
        size_t len=strlen(n->password);
        if (len && (len<8 || len>64)) return fail(error,capacity,"wifi.password: expected 8..63 printable ASCII characters or 64 hex digits");
        for (size_t i=0;i<len;i++) {
            char c=n->password[i];
            if (c<32 || c>126 || (len==64 && !((c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F')))) return fail(error,capacity,"wifi.password: invalid passphrase");
        }
    }
    if (out->wifi_enabled && out->mode==NET_MODE_STATION && !out->count) return fail(error,capacity,"wifi.networks: enabled station Wi-Fi needs an SSID");
    error[0]=0; return true;
}
typedef struct { char *out; size_t used,capacity; bool failed; } Writer;
static void put(Writer *w,const char *fmt,...) {
    if (w->failed) return;
    va_list args; va_start(args,fmt); int n=vsnprintf(w->out+w->used,w->capacity-w->used,fmt,args); va_end(args);
    if (n<0 || (size_t)n>=w->capacity-w->used) w->failed=true; else w->used+=(size_t)n;
}
static void quote(Writer *w,const char *s) {
    put(w,"\""); for (;*s;s++) { unsigned char c=(unsigned char)*s;
        if (c=='"' || c=='\\') put(w,"\\%c",c); else if (c<32) put(w,"\\u%04x",c); else put(w,"%c",c);
    } put(w,"\"");
}
size_t network_json(const NetworkConfig *c,bool secrets,char *out,size_t capacity) {
    Writer w={out,0,capacity,false};
    put(&w,"{\"schema\":\"lightvm.network/1\",\"wifi\":{\"enabled\":%s,\"country\":\"%s\",\"networks\":[",c->wifi_enabled?"true":"false",c->country);
    for (unsigned i=0;i<c->count;i++) { const WifiNetwork *n=&c->networks[i]; if (i) put(&w,","); put(&w,"{\"ssid\":"); quote(&w,n->ssid);
        if (secrets) { put(&w,",\"password\":"); quote(&w,n->password); }
        put(&w,",\"hidden\":%s}",n->hidden?"true":"false");
    }
    put(&w,"],\"mode\":\"%s\",\"ap\":{\"ssid\":",c->mode==NET_MODE_AP?"ap":"station"); quote(&w,c->ap_ssid);
    if (secrets) { put(&w,",\"password\":"); quote(&w,c->ap_password); }
    put(&w,"}},\"http\":{\"enabled\":%s,\"port\":%u}}",c->http_enabled?"true":"false",c->port);
    return w.failed?0:w.used;
}
const char *network_state_name(NetworkState s) { static const char *const names[]={"disabled","scanning","connecting","connected","backoff","ap_starting","ap_active"}; return names[s]; }
void network_init(Network *n) { memset(n,0,sizeof(*n)); strcpy(n->config.country,"XX"); n->config.port=80; n->connected_index=-1; }
void network_bootstrap(Network *n,bool setup_enabled) {
    if (!setup_enabled || n->saved_valid) return;
    memset(&n->config,0,sizeof(n->config)); strcpy(n->config.country,"XX"); n->config.port=80;
    n->config.wifi_enabled=true; n->config.http_enabled=true; n->config.mode=NET_MODE_AP;
    n->initial_setup=true;
}
bool network_online(const Network *n) { return n->state==NET_CONNECTED || n->state==NET_AP_ACTIVE; }
void network_apply(Network *n,const NetworkConfig *c) {
    if (n->driver.disconnect) n->driver.disconnect(n->driver.context);
    n->config=*c; n->candidate=0; n->failures=0; n->quick_retry=false; n->connected_index=-1; n->ip[0]=0; n->error[0]=0; n->ap_ssid[0]=0;
    n->state=NET_DISABLED;
    n->ready=!n->driver.configure || n->driver.configure(n->driver.context,c);
    if (!n->ready) strcpy(n->error,"radio initialization failed");
    n->deadline=0; n->retry_at=0;
    if (!n->ready) n->state=NET_BACKOFF; // A deferred native teardown must retry even when disabling Wi-Fi.
    else if (c->wifi_enabled && n->driver.configure && n->driver.link && (c->mode==NET_MODE_AP || n->driver.scan)) n->state=NET_BACKOFF;
    else if (c->wifi_enabled) strcpy(n->error,"Wi-Fi is unavailable in this build");
}
static void backoff(Network *n,uint64_t now) {
    if (n->driver.disconnect) n->driver.disconnect(n->driver.context);
    unsigned shift=n->failures<4?n->failures:4;
    uint32_t seconds=5u<<shift; if (seconds>60) seconds=60;
    uint32_t r=n->driver.random?n->driver.random(n->driver.context):50;
    n->retry_at=now+(uint64_t)seconds*10000u*(80u+r%41u); n->failures++; n->state=NET_BACKOFF; n->quick_retry=false;
    if (n->config.mode==NET_MODE_AP) { n->ready=false; n->ip[0]=0; }
}
static void next(Network *n,uint64_t now) {
    if (n->driver.disconnect) n->driver.disconnect(n->driver.context);
    while (n->candidate<n->config.count && !n->config.networks[n->candidate].hidden && !n->visible[n->candidate]) n->candidate++;
    if (n->candidate==n->config.count) { backoff(n,now); return; }
    if (!n->driver.connect || !n->driver.connect(n->driver.context,&n->config.networks[n->candidate])) {
        strcpy(n->error,"connection could not start");
        if (n->quick_retry) { n->quick_retry=false; n->state=NET_BACKOFF; n->retry_at=now; return; }
        n->candidate++; next(n,now); return;
    }
    n->state=NET_CONNECTING; n->deadline=now+15000000u;
}
void network_tick(Network *n,uint64_t now) {
    void *ctx=n->driver.context;
    switch(n->state) {
    case NET_DISABLED: break;
    case NET_BACKOFF:
        if (now<n->retry_at) break;
        if (!n->ready) {
            n->ready=n->driver.configure && n->driver.configure(ctx,&n->config);
            if (!n->ready) { strcpy(n->error,"radio initialization failed"); backoff(n,now); break; }
        }
        if (!n->config.wifi_enabled) { n->state=NET_DISABLED; n->error[0]=0; break; }
        if (!n->driver.link || (n->config.mode==NET_MODE_STATION && !n->driver.scan)) {
            n->state=NET_DISABLED; strcpy(n->error,"Wi-Fi is unavailable in this build"); break;
        }
        if (n->config.mode==NET_MODE_AP) { n->state=NET_AP_STARTING; n->deadline=now+15000000u; break; }
        memset(n->visible,0,sizeof(n->visible)); n->candidate=0;
        if (!n->driver.scan(ctx)) { strcpy(n->error,"scan could not start"); backoff(n,now); break; }
        n->state=NET_SCANNING; n->deadline=now+10000000u; break;
    case NET_SCANNING: {
        int done=n->driver.scan_poll(ctx,NULL,NULL);
        if (!done && now<n->deadline) break;
        if (done<0 || !done) { strcpy(n->error,"scan failed or timed out"); backoff(n,now); break; }
        for (unsigned i=0;i<n->config.count;i++) n->driver.scan_poll(ctx,n->config.networks[i].ssid,&n->visible[i]);
        next(n,now); break;
    }
    case NET_CONNECTING: {
        int link=n->driver.link(ctx,n->ip,sizeof(n->ip),n->error,sizeof(n->error));
        if (link>0) { n->state=NET_CONNECTED; n->connected_index=(int)n->candidate; n->failures=0; n->error[0]=0; n->quick_retry=false; break; }
        if (link<0 || now>=n->deadline) {
            if (!link) strcpy(n->error,"connection/DHCP timed out");
            if (n->quick_retry) { n->quick_retry=false; n->state=NET_BACKOFF; n->retry_at=now; }
            else { n->candidate++; next(n,now); }
        } break;
    }
    case NET_CONNECTED:
        if (n->driver.link(ctx,n->ip,sizeof(n->ip),n->error,sizeof(n->error))>0) break;
        n->ip[0]=0; n->candidate=(unsigned)n->connected_index; n->visible[n->candidate]=true; n->quick_retry=true; next(n,now); break;
    case NET_AP_STARTING: {
        int link=n->driver.link(ctx,n->ip,sizeof(n->ip),n->error,sizeof(n->error));
        if (link>0) { n->state=NET_AP_ACTIVE; n->failures=0; n->error[0]=0; }
        else if (link<0 || now>=n->deadline) { if (!link) strcpy(n->error,"AP startup timed out"); backoff(n,now); }
        break;
    }
    case NET_AP_ACTIVE:
        if (n->driver.link(ctx,n->ip,sizeof(n->ip),n->error,sizeof(n->error))<=0) { strcpy(n->error,"AP interface unavailable"); backoff(n,now); }
        break;
    }
}
