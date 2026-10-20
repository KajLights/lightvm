#include "http_setup.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

bool http_setup_origin(const char *host,const char *origin) {
    if (!host[0]) return false;
    if (!origin[0]) return true; // Non-browser clients still need the form token.
    const char *authority=!strncmp(origin,"http://",7)?origin+7:!strncmp(origin,"https://",8)?origin+8:NULL;
    return authority && !strcmp(authority,host);
}

static void escape(const char *in,char *out) {
    for (;*in;in++) {
        const char *entity=*in=='&'?"&amp;":*in=='<'?"&lt;":*in=='>'?"&gt;":*in=='\"'?"&quot;":*in=='\''?"&#39;":NULL;
        if (entity) { size_t n=strlen(entity); memcpy(out,entity,n); out+=n; }
        else *out++=*in;
    }
    *out=0;
}
static unsigned page(Http *h,unsigned status,const char *message,bool saved) {
    char text[160*6+1],ssid[32*6+1],country[2*6+1];
    escape(message,text);
    const NetworkConfig *c=&h->app->network.config;
    escape(c->count?c->networks[0].ssid:"",ssid); escape(c->country,country);
    int n=snprintf((char *)h->response,sizeof(h->response),
        "<!doctype html><html lang='en'><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>LightVM Wi-Fi setup</title>"
        "<body style='font-family:system-ui;max-width:30em;margin:2em auto;padding:0 1em'>"
        "<h1>Wi-Fi setup</h1><p role='%s'>%s</p>",status>=400?"alert":"status",text);
    if (n<0 || (size_t)n>=sizeof(h->response)) return 500;
    h->used=(size_t)n;
    if (!saved) {
        n=snprintf((char *)h->response+h->used,sizeof(h->response)-h->used,
            "<p>Connect this LightVM board to your Wi-Fi. No internet connection is needed.</p>"
            "<form method='post' action='/setup'>"
            "<input type='hidden' name='revision' value='%u'>"
            "<input type='hidden' name='token' value='%s'>"
            "<p><label>Network name (SSID)<br><input name='ssid' maxlength='32' size='28' required value='%s'></label></p>"
            "<p><label>Password<br><input name='password' type='password' maxlength='64' size='28' autocomplete='new-password'></label>"
            "<br><small>Leave blank to keep the password for an already saved network.</small></p>"
            "<p><label><input name='open' type='checkbox' value='1'> Open network (no password)</label></p>"
            "<p><label><input name='hidden' type='checkbox' value='1'%s> Hidden network</label></p>"
            "<p><label>Country code<br><input name='country' maxlength='2' size='3' pattern='[A-Za-z]{2}' required value='%s'></label>"
            "<br><small>Two letters, for example IT; XX uses worldwide settings.</small></p>"
            "<button type='submit'>Save Wi-Fi</button></form>"
            "<p><small>Saving makes this the preferred network and keeps other saved networks. "
            "After setup, rejoin your Wi-Fi to use the LightVM editor.</small></p>",
            (unsigned)h->app->store.network_revision,h->app->boot_id,ssid,c->count&&c->networks[0].hidden?" checked":"",country);
        if (n<0 || (size_t)n>=sizeof(h->response)-h->used) return 500;
        h->used+=(size_t)n;
    }
    const char end[]="</body></html>";
    if (sizeof(end)-1>sizeof(h->response)-h->used) return 500;
    memcpy(h->response+h->used,end,sizeof(end)-1); h->used+=sizeof(end)-1;
    h->html=true; return status;
}

static int hex(unsigned char c) { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
static bool decode(const uint8_t *bytes,size_t size,char *out,size_t capacity) {
    size_t used=0;
    for (size_t i=0;i<size;i++) {
        unsigned char c=bytes[i];
        if (c=='+') c=' ';
        else if (c=='%') {
            if (i+2>=size || hex(bytes[i+1])<0 || hex(bytes[i+2])<0) return false;
            c=(unsigned char)(hex(bytes[i+1])*16+hex(bytes[i+2])); i+=2;
        }
        if (!c || used+1>=capacity) return false;
        out[used++]=(char)c;
    }
    out[used]=0; return true;
}
typedef struct { WifiNetwork network; char country[3],token[33]; uint32_t revision; bool open; } Form;
static bool parse(const HttpRequest *r,Form *f) {
    unsigned seen=0;
    const char *const names[]={"ssid","password","country","token","revision","open","hidden"};
    memset(f,0,sizeof(*f));
    for (size_t start=0;start<r->size;) {
        size_t end=start; while(end<r->size && r->body[end]!='&')end++;
        size_t equal=start; while(equal<end && r->body[equal]!='=')equal++;
        if (equal==end) return false;
        char key[16],value[65];
        if (!decode(r->body+start,equal-start,key,sizeof(key)) || !decode(r->body+equal+1,end-equal-1,value,sizeof(value))) return false;
        unsigned field=0; while(field<7 && strcmp(key,names[field]))field++;
        if (field==7 || (seen&(1u<<field))) return false;
        seen|=1u<<field;
        char *target=field==0?f->network.ssid:field==1?f->network.password:field==2?f->country:field==3?f->token:NULL;
        size_t limit=field==0?sizeof(f->network.ssid):field==1?sizeof(f->network.password):field==2?sizeof(f->country):sizeof(f->token);
        if (target) { if(strlen(value)>=limit)return false; strcpy(target,value); }
        else if (field==4) {
            if (!value[0]) return false;
            for (const char *s=value;*s;s++) {
                if (*s<'0' || *s>'9' || f->revision>(UINT32_MAX-(unsigned)(*s-'0'))/10u) return false;
                f->revision=f->revision*10u+(unsigned)(*s-'0');
            }
        } else {
            if (strcmp(value,"1")) return false;
            if (field==5) f->open=true; else f->network.hidden=true;
        }
        start=end+1;
    }
    return (seen&31u)==31u;
}

unsigned http_setup(Http *h,const HttpRequest *r) {
    if (!strcmp(r->path,"/")) {
        if (strcmp(r->method,"GET")) return 405;
        return r->size?400:page(h,200,"",false);
    }
    if (strcmp(r->method,"POST")) return 405;
    Form form;
    if (!parse(r,&form)) return page(h,400,"Invalid form. Reload the page and try again.",false);
    if (!r->form_origin_valid || strcmp(form.token,h->app->boot_id)) return page(h,403,"This page has expired or came from another site. Reload this board's setup page.",false);
    for (unsigned i=0;i<2;i++) if(form.country[i]>='a' && form.country[i]<='z')form.country[i]-='a'-'A';
    if (!network_country_valid(form.country)) return page(h,400,"Enter a two-letter country code, or XX for worldwide settings.",false);
    NetworkConfig config=h->app->network.config;
    unsigned index=0; while(index<config.count && strcmp(config.networks[index].ssid,form.network.ssid))index++;
    if (form.open) form.network.password[0]=0;
    else if (!form.network.password[0]) {
        if (index==config.count) return page(h,400,"Enter the Wi-Fi password, or select Open network.",false);
        strcpy(form.network.password,config.networks[index].password);
    }
    char error[160];
    if (!network_wifi_valid(&form.network,error,sizeof(error))) return page(h,400,error,false);
    if (index==config.count) {
        if (config.count==NETWORK_LIMIT) return page(h,409,"Eight networks are already saved. Update an existing network or remove one in the editor.",false);
        config.count++;
    }
    memmove(config.networks+1,config.networks,index*sizeof(*config.networks));
    config.networks[0]=form.network;
    config.wifi_enabled=true; config.http_enabled=true; config.mode=NET_MODE_STATION;
    strcpy(config.country,form.country);
    if (h->app->connection_sequence==UINT64_MAX) return page(h,503,"Restart the board before trying again.",false);
    uint64_t source=++h->app->connection_sequence;
    enum Status status=app_save_network(h->app,&config,form.revision,source,error,sizeof(error));
    memset(&config,0,sizeof(config)); memset(&form,0,sizeof(form));
    if (status!=STATUS_OK) return page(h,status==STATUS_STORAGE?500:409,error,false);
    h->reply_source=source;
    return page(h,200,h->app->network.recovery?
        "Wi-Fi saved. Restart the board, then rejoin your normal Wi-Fi and open the LightVM editor. The recovery access point stays available until restart.":
        "Wi-Fi saved. This setup connection will close while the board joins your network. Rejoin your normal Wi-Fi, then open the LightVM editor.",true);
}
