#include "device/settings.h"
#include "device/platform.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
bool settings_revision_matches(uint32_t current,uint32_t expected,char *error,size_t cap) {
    if(current!=expected || current==UINT32_MAX){snprintf(error,cap,"configuration revision conflict");return false;}
    return true;
}
void network_settings_init(NetworkSettings *s,Network *network,Store *store) {
    memset(s,0,sizeof(*s));s->network=network;s->store=store;
}
void network_settings_load(NetworkSettings *s) {
    const StoreEntry *entry=store_find(s->store,STORE_NETWORK_SETTINGS,STORE_NETWORK);
    if(!entry)return;
    uint8_t *bytes=platform_calloc(1,entry->size);NetworkConfig candidate;
    if(!bytes || !store_read(s->store,entry,0,bytes,entry->size) ||
       !network_parse(bytes,entry->size,NULL,&candidate,s->error,sizeof(s->error))) {
        if(!s->error[0])snprintf(s->error,sizeof(s->error),"network storage read failed");
    } else {s->network->config=candidate;s->network->saved_valid=true;}
    if(bytes)memset(bytes,0,entry->size);
    memset(&candidate,0,sizeof(candidate));platform_free(bytes);
}
enum Status network_settings_save(NetworkSettings *s,const NetworkConfig *c,uint32_t expected,uint64_t source,char *error,size_t cap) {
    if(s->pending || !settings_revision_matches(s->store->network_revision,expected,error,cap)) {
        snprintf(error,cap,"network revision must match; wait for pending changes");return STATUS_CONFIG;
    }
    char *data=platform_calloc(4096,1);
    if(!data){snprintf(error,cap,"network serialization allocation failed");return STATUS_STORAGE;}
    size_t count=network_json(c,true,data,4096);
    bool ok=count && store_put(s->store,STORE_NETWORK_SETTINGS,STORE_NETWORK,data,(uint32_t)count);
    memset(data,0,4096);platform_free(data);
    if(!ok){snprintf(error,cap,"network save failed; active settings retained");return STATUS_STORAGE;}
    s->network->saved_valid=true;s->error[0]=0;
    if(s->network->recovery)s->network->config=*c;
    else {s->candidate=*c;s->pending=true;s->due=platform_time_us()+1000000u;s->reply_source=source;}
    return STATUS_OK;
}
void network_settings_tick(NetworkSettings *s,uint64_t now) {
    if(s->pending && now>=s->due) {
        s->pending=false;s->network->initial_setup=false;
        network_apply(s->network,&s->candidate);memset(&s->candidate,0,sizeof(s->candidate));
    }
    if(!s->pending)network_tick(s->network,now);
}
void network_settings_reply_delivered(NetworkSettings *s,uint64_t source,uint64_t now) {
    if(s->pending && s->reply_source==source){s->due=now+100000u;s->reply_source=0;}
}
