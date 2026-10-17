#include "store.h"
#include "runtime.h"
#include "protocol.h"
#include "devices.h"
#include <string.h>
#include <limits.h>
#define DIRECTORY_BYTES 1024u
#define COMMIT_OFFSET 1024u
#define COMMIT_MAGIC 0x54494d43u
#define SECTOR 4096u
#define SECTORS (STORAGE_BYTES / SECTOR)
_Static_assert(32u+STORE_ENTRY_LIMIT*64u<=1020u,"store directory overflow");
bool store_preset_name(const char *name) {
    return !strncmp(name,"$p",2)&&object_name_valid(name+2);
}
static uint32_t aligned(uint32_t n) { return (n+255u)&~255u; }
static bool same(const StoreEntry *a,const StoreEntry *b) { return a->offset==b->offset&&a->size==b->size&&a->crc==b->crc; }
static bool valid_extent(const StoreEntry *e) {
    if(!e->size||e->offset>=STORAGE_BYTES||e->size>STORAGE_BYTES-e->offset)return false;
    uint32_t end=e->offset+e->size;
    return e->offset>=SECTOR && !(e->offset<STORE_BANK_BYTES+SECTOR&&end>STORE_BANK_BYTES);
}
static bool entry_crc(const StoreEntry *e) {
    uint8_t bytes[256];uint32_t crc=0xffffffffu;
    if(!valid_extent(e))return false;
    for(uint32_t at=0;at<e->size;) {
        size_t n=e->size-at;if(n>sizeof(bytes))n=sizeof(bytes);
        if(!platform_storage_read(e->offset+at,bytes,n))return false;
        crc=crc32_update(crc,bytes,n);at+=(uint32_t)n;runtime_checkpoint();
    }
    return (crc^0xffffffffu)==e->crc;
}
static void decode(StoreEntry *e,const uint8_t *r) {
    memcpy(e->name,r,33);e->offset=read_u32(r+36);e->size=read_u32(r+40);e->crc=read_u32(r+44);
}
static void encode(uint8_t *r,const StoreEntry *e) {
    memcpy(r,e->name,33);write_u32(r+36,e->offset);write_u32(r+40,e->size);write_u32(r+44,e->crc);
}
static bool scan(int bank,Store *s) {
    uint8_t d[DIRECTORY_BYTES],commit[256];uint32_t base=(uint32_t)bank*STORE_BANK_BYTES;
    if(!platform_storage_read(base,d,sizeof(d))||!platform_storage_read(base+COMMIT_OFFSET,commit,sizeof(commit)))return false;
    unsigned version=read_u32(d+4);
    if(memcmp(d,"LVS1",4)||version<1||version>4||read_u32(d+1020)!=crc32_bytes(d,1020)||
       read_u32(commit)!=COMMIT_MAGIC||read_u32(commit+4)!=read_u32(d+8)||read_u32(commit+8)!=read_u32(d+1020)||read_u32(commit+12)!=crc32_bytes(commit,12))return false;
    memset(s,0,sizeof(*s));s->bank=bank;s->available=true;s->generation=read_u32(d+8);s->count=read_u32(d+12);
    s->config_revision=read_u32(d+16);s->network_revision=version>=2?read_u32(d+20):0;
    if(!s->generation||s->count>STORE_ENTRY_LIMIT)return false;
    uint32_t expected=STORE_DATA_OFFSET;
    for(unsigned n=0;n<s->count;n++) {
        const uint8_t *r=d+32+n*64;StoreEntry *e=&s->entries[n];
        if(!memchr(r,0,33))return false;
        decode(e,r);
        bool config=!strcmp(e->name,STORE_CONFIG)||!strcmp(e->name,STORE_NETWORK)||store_preset_name(e->name);
        if((!config&&!object_name_valid(e->name))||e->size>(config?CONFIG_LIMIT:MODULE_LIMIT))return false;
        for(unsigned k=0;k<n;k++)if(!strcmp(e->name,s->entries[k].name))return false;
        if(version<3){if(e->offset!=expected||aligned(e->size)>STORE_BANK_BYTES-expected)return false;expected+=aligned(e->size);e->offset+=base;}
        if(!entry_crc(e))return false;
        for(unsigned k=0;k<n;k++){StoreEntry *p=&s->entries[k];if(e->offset<p->offset+p->size&&p->offset<e->offset+e->size)return false;}
    }
    /* Version 4 reuses the obsolete singleton boot-selector space for entries. */
    return true;
}
bool store_mount(Store *s) {
    Store a,b;memset(s,0,sizeof(*s));s->bank=-1;s->available=platform_storage_available();
    if(!s->available)return true;
    bool va=scan(0,&a),vb=scan(1,&b);
    if(va||vb){*s=!vb||(va&&a.generation>b.generation)?a:b;return true;}
    /* Uncommitted payload is harmless. Only committed directory corruption is
       a mount error; interrupted first uploads never constitute activation. */
    uint8_t marker[4];
    for(unsigned bank=0;bank<2;bank++){
        if(!platform_storage_read(bank*STORE_BANK_BYTES+COMMIT_OFFSET,marker,4))return false;
        if(read_u32(marker)==COMMIT_MAGIC)return false;
    }
    return true;
}
const StoreEntry *store_find(const Store *s,const char *name) {
    for(unsigned n=0;n<s->count;n++)if(!strcmp(name,s->entries[n].name))return &s->entries[n];
    return NULL;
}
bool store_read(const Store *s,const StoreEntry *e,uint32_t at,void *bytes,size_t n) {
    return s->available&&e&&at<=e->size&&n<=e->size-at&&platform_storage_read(e->offset+at,bytes,n);
}
static void mark(uint8_t used[SECTORS],const StoreEntry *e) {
    if(!e->size)return;
    for(uint32_t n=e->offset/SECTOR;n<(e->offset+e->size+SECTOR-1)/SECTOR;n++)used[n]=1;
}
static void occupied(const Store *s,uint8_t used[SECTORS]) {
    memset(used,0,SECTORS);used[0]=used[STORE_BANK_BYTES/SECTOR]=1;
    for(unsigned n=0;n<s->count;n++)mark(used,&s->entries[n]);
    for(const StorePin *p=s->pins;p;p=p->next)mark(used,&p->entry);
}
uint32_t store_free_bytes(const Store *s) {
    uint8_t used[SECTORS];occupied(s,used);uint32_t free=0;
    for(unsigned n=0;n<SECTORS;n++)if(!used[n])free+=SECTOR;
    return free;
}
int store_pin(Store *s,const StoreEntry *e) {
    if(!valid_extent(e))return -1;
    int id=0;
    for(StorePin *p=s->pins;p;p=p->next){
        if(same(e,&p->entry)){if(p->references==UINT_MAX)return -1;p->references++;return p->id;}
        if(p->id==INT_MAX)return -1;
        if(id<=p->id)id=p->id+1;
    }
    StorePin *p=platform_calloc(1,sizeof(*p));if(!p)return -1;
    p->entry=*e;p->references=1;p->id=id;p->next=s->pins;s->pins=p;return id;
}
void store_unpin(Store *s,int pin){
    if(!s||pin<0)return;
    StorePin **link=&s->pins;
    while(*link){StorePin *p=*link;if(p->id==pin){if(!--p->references){*link=p->next;platform_free(p);}return;}link=&p->next;}
}
void store_upload_cancel(StoreUpload *u) { if(u->store)store_unpin(u->store,u->pin);memset(u,0,sizeof(*u));u->pin=-1; }
bool store_upload_begin(Store *s,StoreUpload *u,const char *name,uint32_t size,uint32_t crc) {
    if(!s->available||!size||size>MODULE_LIMIT||strlen(name)>OBJECT_NAME_LIMIT)return false;
    store_upload_cancel(u);uint8_t used[SECTORS];occupied(s,used);unsigned need=(size+SECTOR-1)/SECTOR,run=0,start=0;
    for(unsigned n=0;n<SECTORS;n++){run=used[n]?0:run+1;if(run==need){start=n+1-need;break;}}
    if(run!=need)return false;
    u->store=s;u->entry=(StoreEntry){.offset=start*SECTOR,.size=size,.crc=crc};strcpy(u->entry.name,name);
    u->pin=store_pin(s,&u->entry);if(u->pin<0){u->store=NULL;return false;}
    u->crc=0xffffffffu;u->active=true;memset(u->page,255,sizeof(u->page));return true;
}
bool store_upload_chunk(StoreUpload *u,uint32_t at,const void *data,size_t size) {
    if(!u->active||!size||at>u->received||size>u->entry.size-at)return false;
    const uint8_t *p=data;
    if(at<u->received){
        if(size>u->received-at)return false;
        uint8_t bytes[256];
        while(size){size_t n=256-at%256;if(n>size)n=size;
            if(at>=u->received/256*256)memcpy(bytes,u->page+at%256,n);
            else if(!store_read(u->store,&u->entry,at,bytes,n))return false;
            if(memcmp(bytes,p,n))return false;
            at+=(uint32_t)n;p+=n;size-=n;}
        return true;
    }
    while(size){
        if(u->received>=u->erased){if(!platform_storage_erase(u->entry.offset+u->erased,SECTOR)){u->active=false;return false;}u->erased+=SECTOR;runtime_checkpoint();}
        unsigned offset=u->received%256;size_t n=256-offset;if(n>size)n=size;
        memcpy(u->page+offset,p,n);u->crc=crc32_update(u->crc,p,n);u->received+=(uint32_t)n;p+=n;size-=n;
        if(u->received%256==0){if(!platform_storage_write(u->entry.offset+u->received-256,u->page,256)){u->active=false;return false;}memset(u->page,255,256);runtime_checkpoint();}
    }
    return true;
}
bool store_upload_finish(StoreUpload *u) {
    if(!u->active||u->received!=u->entry.size||(u->crc^0xffffffffu)!=u->entry.crc)return false;
    if(u->received%256&&!platform_storage_write(u->entry.offset+u->received/256*256,u->page,256))return false;
    if(!platform_storage_sync()||!entry_crc(&u->entry))return false;
    u->active=false;u->complete=true;return true;
}
static bool commit(Store *s,Store *next) {
    if(!s->available||s->generation==UINT32_MAX)return false;
    next->bank=s->bank==0?1:0;next->generation=s->generation+1;
    uint32_t base=(uint32_t)next->bank*STORE_BANK_BYTES;uint8_t d[1024]={0},page[256];
    memcpy(d,"LVS1",4);write_u32(d+4,4);write_u32(d+8,next->generation);write_u32(d+12,next->count);
    write_u32(d+16,next->config_revision);write_u32(d+20,next->network_revision);
    for(unsigned n=0;n<next->count;n++)encode(d+32+n*64,&next->entries[n]);
    uint32_t crc=crc32_bytes(d,1020);write_u32(d+1020,crc);
    if(!platform_storage_erase(base,SECTOR))return false;
    runtime_checkpoint();
    for(unsigned at=0;at<1024;at+=256){if(!platform_storage_write(base+at,d+at,256))return false;
    runtime_checkpoint();}
    if(!platform_storage_sync())return false;
    for(unsigned at=0;at<1024;at+=256)if(!platform_storage_read(base+at,page,256)||memcmp(page,d+at,256))return false;
    memset(page,255,256);write_u32(page,COMMIT_MAGIC);write_u32(page+4,next->generation);write_u32(page+8,crc);write_u32(page+12,crc32_bytes(page,12));
    if(!platform_storage_write(base+COMMIT_OFFSET,page,256)||!platform_storage_sync())return false;
    /* Read back the durable selector before exposing the new catalogue. */
    uint8_t check[256];if(!platform_storage_read(base+COMMIT_OFFSET,check,256)||memcmp(check,page,256))return false;
    *s=*next;return true;
}
static bool publish(Store *s,const StoreEntry *entry) {
    bool config=!strcmp(entry->name,STORE_CONFIG),network=!strcmp(entry->name,STORE_NETWORK),preset=store_preset_name(entry->name);
    if((!config&&!network&&!preset&&!object_name_valid(entry->name))||entry->size>(config||network||preset?CONFIG_LIMIT:MODULE_LIMIT)||!entry_crc(entry))return false;
    Store next=*s;unsigned n;
    for(n=0;n<next.count;n++)if(!strcmp(next.entries[n].name,entry->name))break;
    if(n==next.count){if(next.count==STORE_ENTRY_LIMIT)return false;next.count++;}
    next.entries[n]=*entry;
    unsigned procedures=0,presets=0;
    for(n=0;n<next.count;n++)if(store_preset_name(next.entries[n].name))presets++;else if(next.entries[n].name[0]!='$')procedures++;
    if(procedures>PROCEDURE_LIMIT||presets>PRESET_LIMIT||(config&&next.config_revision==UINT32_MAX)||(network&&next.network_revision==UINT32_MAX))return false;
    if(config)next.config_revision++;
    if(network)next.network_revision++;
    return commit(s,&next);
}
bool store_publish(Store *s,const StoreEntry *e) { return publish(s,e); }
bool store_put(Store *s,const char *name,const void *bytes,uint32_t size) {
    StoreUpload u={.pin=-1};bool ok=store_upload_begin(s,&u,name,size,crc32_bytes(bytes,size));
    if(ok)ok=store_upload_chunk(&u,0,bytes,size)&&store_upload_finish(&u)&&store_publish(s,&u.entry);
    store_upload_cancel(&u);return ok;
}
bool store_delete(Store *s,const char *name) {
    Store next=*s;unsigned n;for(n=0;n<next.count;n++)if(!strcmp(name,next.entries[n].name))break;
    if(n==next.count)return false;
    memmove(&next.entries[n],&next.entries[n+1],(next.count-n-1)*sizeof(StoreEntry));next.count--;
    return commit(s,&next);
}

bool store_put_config(Store *s,const void *bytes,uint32_t size) {
    StoreUpload u={.pin=-1};bool ok=store_upload_begin(s,&u,STORE_CONFIG,size,crc32_bytes(bytes,size));
    if(ok)ok=store_upload_chunk(&u,0,bytes,size)&&store_upload_finish(&u)&&publish(s,&u.entry);
    store_upload_cancel(&u);return ok;
}
