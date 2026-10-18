#include "store_backend.h"
#include "outputs.h"
#include "protocol.h"
#include <string.h>
#include <limits.h>
#ifndef LIGHTVM_STORAGE_BACKEND
#define LIGHTVM_STORAGE_BACKEND store_extent_backend
#endif
extern const StoreBackend LIGHTVM_STORAGE_BACKEND;
const char *store_type_name(StoreType type) {
    static const char *const names[]={"","animation","transition","preset","config","network"};
    return (unsigned)type<=STORE_NETWORK_SETTINGS?names[type]:"";
}
StoreType store_type(const char *name) {
    for(StoreType t=STORE_ANIMATION;t<=STORE_NETWORK_SETTINGS;t++)if(!strcmp(name,store_type_name(t)))return t;
    return STORE_ANY;
}
static bool valid(const StoreEntry *e) {
    bool system=e->type==STORE_CONFIGURATION||e->type==STORE_NETWORK_SETTINGS;
    return e->type>=STORE_ANIMATION&&e->type<=STORE_NETWORK_SETTINGS&&
        (system? !strcmp(e->name,e->type==STORE_CONFIGURATION?STORE_CONFIG:STORE_NETWORK):object_name_valid(e->name))&&
        e->size&&e->size<=(e->type==STORE_ANIMATION||e->type==STORE_TRANSITION?MODULE_LIMIT:CONFIG_LIMIT);
}
bool store_mount_with(Store *s,const StoreBackend *backend,const void *configuration) {
    memset(s,0,sizeof(*s));s->backend=backend;
    if(!backend||!backend->name||!backend->mount||!backend->entry||!backend->read||!backend->info)return false;
    if(backend->begin&&(!backend->chunk||!backend->finish||!backend->cancel||!backend->publish))return false;
    if(backend->mount(s,configuration))return true;
    if(backend->destroy)backend->destroy(s);
    s->available=false;return false;
}
bool store_mount(Store *s) {
#ifdef LIGHTVM_STORAGE_CONFIGURATION
    extern const void *const LIGHTVM_STORAGE_CONFIGURATION;
    return store_mount_with(s,&LIGHTVM_STORAGE_BACKEND,LIGHTVM_STORAGE_CONFIGURATION);
#else
    return store_mount_with(s,&LIGHTVM_STORAGE_BACKEND,NULL);
#endif
}
void store_destroy(Store *s) {
    while(s->pins){StorePin *p=s->pins;s->pins=p->next;if(s->backend->release)s->backend->release(s,&p->entry);platform_free(p);}
    if(s->backend&&s->backend->destroy)s->backend->destroy(s);
    memset(s,0,sizeof(*s));
}
const StoreEntry *store_list(const Store *s,StoreType type,uint32_t *cursor) {
    if(!s->available)return NULL;
    const StoreEntry *e;
    while((e=s->backend->entry(s,*cursor))){++*cursor;if(type==STORE_ANY||e->type==type)return e;}
    return NULL;
}
const StoreEntry *store_find(const Store *s,StoreType type,const char *name) {
    uint32_t cursor=0;const StoreEntry *found=NULL,*e;
    while((e=store_list(s,type,&cursor)))if(!strcmp(name,e->name)){if(found)return NULL;found=e;}
    return found;
}
StoreInfo store_info(const Store *s) { return s->available?s->backend->info(s):(StoreInfo){0}; }
const char *store_backend_name(const Store *s) { return s->backend?s->backend->name:""; }
bool store_read(const Store *s,const StoreEntry *e,uint32_t at,void *bytes,size_t size) {
    return s&&s->available&&e&&at<=e->size&&size<=e->size-at&&s->backend->read(s,e,at,bytes,size);
}
static bool same(const StoreEntry *a,const StoreEntry *b) { return a->id==b->id&&a->size==b->size&&a->crc==b->crc; }
int store_pin(Store *s,const StoreEntry *e) {
    if(!s->available||!e)return -1;
    int id=0;
    for(StorePin *p=s->pins;p;p=p->next){
        if(same(e,&p->entry)){if(p->references==UINT_MAX)return -1;p->references++;return p->id;}
        if(p->id==INT_MAX)return -1;
        if(id<=p->id)id=p->id+1;
    }
    StorePin *p=platform_calloc(1,sizeof(*p));if(!p)return -1;
    if(s->backend->retain&&!s->backend->retain(s,e)){platform_free(p);return -1;}
    p->entry=*e;p->references=1;p->id=id;p->next=s->pins;s->pins=p;return id;
}
void store_unpin(Store *s,int id) {
    if(!s||id<0)return;
    StorePin **link=&s->pins;
    while(*link){StorePin *p=*link;if(p->id==id){if(!--p->references){*link=p->next;if(s->backend->release)s->backend->release(s,&p->entry);platform_free(p);}return;}link=&p->next;}
}
void store_upload_cancel(StoreUpload *u) {
    if(u->store){if(u->store->backend->cancel)u->store->backend->cancel(u);store_unpin(u->store,u->pin);}
    memset(u,0,sizeof(*u));u->pin=-1;
}
bool store_upload_begin(Store *s,StoreUpload *u,StoreType type,const char *name,uint32_t size,uint32_t crc) {
    StoreEntry e={.type=type,.size=size,.crc=crc};
    if(strlen(name)>OBJECT_NAME_LIMIT)return false;
    strcpy(e.name,name);
    /* Empty names are private transient uploads, never publishable. */
    if(!s->available||!s->backend->begin||(!valid(&e)&&(name[0]||type!=STORE_ANIMATION||!size||size>MODULE_LIMIT)))return false;
    store_upload_cancel(u);u->store=s;u->entry=e;
    if(!s->backend->begin(s,u)){store_upload_cancel(u);return false;}
    u->pin=store_pin(s,&u->entry);
    if(u->pin<0){store_upload_cancel(u);return false;}
    u->active=true;return true;
}
bool store_upload_chunk(StoreUpload *u,uint32_t at,const void *data,size_t size) {
    return u->store&&u->active&&size&&at<=u->received&&at<=u->entry.size&&size<=u->entry.size-at&&u->store->backend->chunk(u,at,data,size);
}
bool store_upload_finish(StoreUpload *u) {
    if(!u->store||!u->active||u->received!=u->entry.size||!u->store->backend->finish(u))return false;
    u->active=false;u->complete=true;return true;
}
bool store_publish(Store *s,const StoreEntry *e) {
    if(!s->available||!valid(e)||!s->backend->publish)return false;
    int pin=store_pin(s,e);if(pin<0)return false;
    bool ok=s->backend->publish(s,e);store_unpin(s,pin);return ok;
}
bool store_put(Store *s,StoreType type,const char *name,const void *bytes,uint32_t size) {
    StoreUpload u={.pin=-1};bool ok=store_upload_begin(s,&u,type,name,size,crc32_bytes(bytes,size));
    if(ok)ok=store_upload_chunk(&u,0,bytes,size)&&store_upload_finish(&u)&&store_publish(s,&u.entry);
    store_upload_cancel(&u);return ok;
}
bool store_put_config(Store *s,const void *bytes,uint32_t size) { return store_put(s,STORE_CONFIGURATION,STORE_CONFIG,bytes,size); }
bool store_delete(Store *s,StoreType type,const char *name) {
    const StoreEntry *e=store_find(s,type,name);
    return e&&s->backend->remove&&s->backend->remove(s,e);
}
