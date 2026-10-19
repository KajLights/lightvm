#include "store_backend.h"
#include "runtime.h"
#include "protocol.h"
#include "outputs.h"
#include <string.h>
#define SECTOR 4096u
#define COMMIT_OFFSET 1024u
#define COMMIT_MAGIC 0x54494d43u
#define RECORD_BYTES 64u
/* Two fixed root sectors select an immutable, variable-size catalogue extent.
 * Payloads and catalogues share the allocator; there is no file-count quota. */
typedef struct {
    StoreBlockDevice device;
    StoreEntry *entries;
    uint32_t count,catalog_offset,catalog_size;
    int bank;
} ExtentStore;
typedef struct { uint32_t crc,erased; uint8_t page[256]; } ExtentUpload;
static bool block_read(void *c,uint32_t o,void *b,size_t n){(void)c;return platform_storage_read(o,b,n);}
static bool block_write(void *c,uint32_t o,const void *b,size_t n){(void)c;return platform_storage_write(o,b,n);}
static bool block_erase(void *c,uint32_t o,size_t n){(void)c;return platform_storage_erase(o,n);}
static bool block_sync(void *c){(void)c;return platform_storage_sync();}
static uint32_t bank_offset(const ExtentStore *s,unsigned bank){return bank?s->device.capacity/2:0;}
static bool valid_extent(const ExtentStore *s,uint32_t offset,uint32_t size){
    uint32_t half=s->device.capacity/2;
    return size&&offset%SECTOR==0&&offset>=SECTOR&&offset<s->device.capacity&&size<=s->device.capacity-offset&&
        !(offset<half+SECTOR&&offset+size>half);
}
static bool extent_crc(const ExtentStore *s,uint32_t offset,uint32_t size,uint32_t expected){
    if(!valid_extent(s,offset,size))return false;
    uint8_t bytes[256];uint32_t crc=0xffffffffu;
    for(uint32_t at=0;at<size;){
        size_t n=size-at;if(n>sizeof(bytes))n=sizeof(bytes);
        if(!s->device.read(s->device.context,offset+at,bytes,n))return false;
        crc=crc32_update(crc,bytes,n);at+=(uint32_t)n;runtime_checkpoint();
    }
    return (crc^0xffffffffu)==expected;
}
static void encode(uint8_t *r,const StoreEntry *e){
    memset(r,0,RECORD_BYTES);memcpy(r,e->name,33);r[33]=(uint8_t)e->type;
    write_u32(r+36,e->id);write_u32(r+40,e->size);write_u32(r+44,e->crc);
}
static bool decode(ExtentStore *s,StoreEntry *e,const uint8_t *r){
    if(!memchr(r,0,33))return false;
    memcpy(e->name,r,33);e->id=read_u32(r+36);e->size=read_u32(r+40);e->crc=read_u32(r+44);
    e->type=(StoreType)r[33];
    if(e->type<STORE_ANIMATION||e->type>STORE_OUTPUT_STATE)return false;
    if(e->type==STORE_CONFIGURATION){if(strcmp(e->name,STORE_CONFIG))return false;}
    else if(e->type==STORE_NETWORK_SETTINGS){if(strcmp(e->name,STORE_NETWORK))return false;}
    else if(!object_name_valid(e->name))return false;
    return e->size<=(e->type==STORE_ANIMATION||e->type==STORE_TRANSITION?MODULE_LIMIT:CONFIG_LIMIT)&&extent_crc(s,e->id,e->size,e->crc);
}
static bool overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn){return an&&bn&&a<b+bn&&b<a+an;}
static bool scan(ExtentStore *s,unsigned bank,uint32_t *generation,uint32_t *config,uint32_t *network){
    uint8_t root[1024],commit[256];uint32_t base=bank_offset(s,bank);
    if(!s->device.read(s->device.context,base,root,sizeof(root))||!s->device.read(s->device.context,base+COMMIT_OFFSET,commit,sizeof(commit)))return false;
    unsigned version=read_u32(root+4);
    if(memcmp(root,"LVS2",4)||version!=1||read_u32(root+1020)!=crc32_bytes(root,1020)||
       read_u32(commit)!=COMMIT_MAGIC||read_u32(commit+4)!=read_u32(root+8)||read_u32(commit+8)!=read_u32(root+1020)||read_u32(commit+12)!=crc32_bytes(commit,12))return false;
    *generation=read_u32(root+8);*config=read_u32(root+16);*network=read_u32(root+20);s->count=read_u32(root+12);
    if(!*generation||s->count>s->device.capacity/SECTOR)return false;
    {
        if(read_u32(root+24)!=s->device.capacity)return false;
        s->catalog_offset=read_u32(root+28);s->catalog_size=read_u32(root+32);
        if(s->catalog_size!=s->count*RECORD_BYTES||
           (s->count&&!extent_crc(s,s->catalog_offset,s->catalog_size,read_u32(root+36))))return false;
    }
    s->entries=platform_calloc(s->count?s->count:1,sizeof(*s->entries));if(!s->entries)return false;
    for(uint32_t n=0;n<s->count;n++){
        uint8_t record[RECORD_BYTES];
        if(!s->device.read(s->device.context,s->catalog_offset+n*RECORD_BYTES,record,sizeof(record)))goto invalid;
        StoreEntry *e=&s->entries[n];if(!decode(s,e,record))goto invalid;
        if(overlap(e->id,e->size,s->catalog_offset,s->catalog_size))goto invalid;
        for(uint32_t k=0;k<n;k++){
            StoreEntry *p=&s->entries[k];
            if((e->type==p->type&&!strcmp(e->name,p->name))||overlap(e->id,e->size,p->id,p->size))goto invalid;
        }
    }
    s->bank=(int)bank;return true;
invalid:
    platform_free(s->entries);s->entries=NULL;return false;
}
static bool mount(Store *s,const void *configuration){
    StoreBlockDevice device;
    if(configuration)device=*(const StoreBlockDevice *)configuration;
    else {
        if(!platform_storage_available())return true;
        device=(StoreBlockDevice){.capacity=platform_storage_capacity(),.read=block_read,.write=block_write,.erase=block_erase,.sync=block_sync};
    }
    if(device.capacity<4*SECTOR||device.capacity%(2*SECTOR)||!device.read||!device.write||!device.erase||!device.sync)return false;
    ExtentStore *state=platform_calloc(1,sizeof(*state));if(!state)return false;
    state->device=device;state->bank=-1;s->state=state;
    ExtentStore a={.device=device},b={.device=device};uint32_t ag=0,ac=0,an=0,bg=0,bc=0,bn=0;
    bool va=scan(&a,0,&ag,&ac,&an),vb=scan(&b,1,&bg,&bc,&bn);
    if(va||vb){
        bool first=!vb||(va&&ag>bg);*state=first?a:b;platform_free(first?b.entries:a.entries);
        s->generation=first?ag:bg;s->config_revision=first?ac:bc;s->network_revision=first?an:bn;s->available=true;return true;
    }
    /* Never format committed data on a mount failure, including a size change. */
    for(unsigned bank=0;bank<2;bank++){
        uint8_t marker[4];if(!device.read(device.context,bank_offset(state,bank)+COMMIT_OFFSET,marker,4)||read_u32(marker)==COMMIT_MAGIC)return false;
    }
    s->available=true;return true;
}
static void destroy(Store *s){ExtentStore *p=s->state;if(p){platform_free(p->entries);platform_free(p);}s->state=NULL;}
static const StoreEntry *entry(const Store *s,uint32_t index){ExtentStore *p=s->state;return index<p->count?&p->entries[index]:NULL;}
static bool read_bytes(const Store *s,const StoreEntry *e,uint32_t at,void *bytes,size_t size){
    ExtentStore *p=s->state;return valid_extent(p,e->id,e->size)&&p->device.read(p->device.context,e->id+at,bytes,size);
}
static void mark(uint8_t *used,uint32_t offset,uint32_t size){
    if(size)for(uint32_t n=offset/SECTOR;n<offset/SECTOR+(size+SECTOR-1)/SECTOR;n++)used[n]=1;
}
static uint8_t *occupied(const Store *s){
    ExtentStore *p=s->state;uint8_t *used=platform_calloc(p->device.capacity/SECTOR,1);if(!used)return NULL;
    used[0]=used[p->device.capacity/(2*SECTOR)]=1;mark(used,p->catalog_offset,p->catalog_size);
    for(uint32_t n=0;n<p->count;n++)mark(used,p->entries[n].id,p->entries[n].size);
    for(const StorePin *pin=s->pins;pin;pin=pin->next)mark(used,pin->entry.id,pin->entry.size);
    return used;
}
static StoreInfo info(const Store *s){
    ExtentStore *p=s->state;StoreInfo result={.total_bytes=p->device.capacity,.allocation_unit=SECTOR,.writable=true};
    uint8_t *used=occupied(s);if(!used)return result;
    uint32_t run=0;for(uint32_t n=0;n<p->device.capacity/SECTOR;n++){run=used[n]?0:run+SECTOR;if(!used[n])result.free_bytes+=SECTOR;if(run>result.largest_free_bytes)result.largest_free_bytes=run;}
    platform_free(used);return result;
}
static uint32_t allocate(const Store *s,uint32_t size){
    ExtentStore *p=s->state;uint8_t *used=occupied(s);if(!used)return 0;
    uint32_t need=(size+SECTOR-1)/SECTOR,run=0,offset=0;
    for(uint32_t n=0;n<p->device.capacity/SECTOR;n++){run=used[n]?0:run+1;if(run==need){offset=(n+1-need)*SECTOR;break;}}
    platform_free(used);return offset;
}
static bool retain(Store *s,const StoreEntry *e){return valid_extent(s->state,e->id,e->size);}
static bool begin(Store *s,StoreUpload *u){
    u->entry.id=allocate(s,u->entry.size);if(!u->entry.id)return false;
    ExtentUpload *state=platform_calloc(1,sizeof(*state));if(!state)return false;
    state->crc=0xffffffffu;memset(state->page,255,sizeof(state->page));u->state=state;return true;
}
static bool chunk(StoreUpload *u,uint32_t at,const void *data,size_t size){
    ExtentStore *s=u->store->state;ExtentUpload *state=u->state;const uint8_t *bytes=data;
    if(at<u->received){
        if(size>u->received-at)return false;
        uint8_t check[256];while(size){size_t n=256-at%256;if(n>size)n=size;
            if(at>=u->received/256*256)memcpy(check,state->page+at%256,n);
            else if(!read_bytes(u->store,&u->entry,at,check,n))return false;
            if(memcmp(check,bytes,n))return false;
            at+=(uint32_t)n;bytes+=n;size-=n;
        }return true;
    }
    while(size){
        if(u->received>=state->erased){if(!s->device.erase(s->device.context,u->entry.id+state->erased,SECTOR)){u->active=false;return false;}state->erased+=SECTOR;runtime_checkpoint();}
        size_t n=256-u->received%256;if(n>size)n=size;
        memcpy(state->page+u->received%256,bytes,n);state->crc=crc32_update(state->crc,bytes,n);u->received+=(uint32_t)n;bytes+=n;size-=n;
        if(u->received%256==0){if(!s->device.write(s->device.context,u->entry.id+u->received-256,state->page,256)){u->active=false;return false;}memset(state->page,255,256);runtime_checkpoint();}
    }return true;
}
static bool finish(StoreUpload *u){
    ExtentStore *s=u->store->state;ExtentUpload *state=u->state;
    if((state->crc^0xffffffffu)!=u->entry.crc)return false;
    if(u->received%256&&!s->device.write(s->device.context,u->entry.id+u->received/256*256,state->page,256))return false;
    return s->device.sync(s->device.context)&&extent_crc(s,u->entry.id,u->entry.size,u->entry.crc);
}
static void cancel(StoreUpload *u){platform_free(u->state);u->state=NULL;}
static bool commit(Store *store,StoreEntry *entries,uint32_t count,StoreType changed){
    ExtentStore *s=store->state;StoreBlockDevice *d=&s->device;
    if(store->generation==UINT32_MAX||(changed==STORE_CONFIGURATION&&store->config_revision==UINT32_MAX)||(changed==STORE_NETWORK_SETTINGS&&store->network_revision==UINT32_MAX))return false;
    uint32_t catalog_size=count*RECORD_BYTES,catalog_offset=count?allocate(store,catalog_size):0,crc=0xffffffffu;
    if(count&&!catalog_offset)return false;
    uint8_t page[256];
    for(uint32_t at=0;at<catalog_size;at+=256){
        if(at%SECTOR==0&&!d->erase(d->context,catalog_offset+at,SECTOR))return false;
        memset(page,0,sizeof(page));uint32_t n=catalog_size-at;if(n>256)n=256;
        for(uint32_t k=0;k<n;k+=RECORD_BYTES)encode(page+k,&entries[(at+k)/RECORD_BYTES]);
        crc=crc32_update(crc,page,n);
        if(!d->write(d->context,catalog_offset+at,page,256))return false;
        runtime_checkpoint();
    }
    crc^=0xffffffffu;
    if(!d->sync(d->context)||(count&&!extent_crc(s,catalog_offset,catalog_size,crc)))return false;
    uint8_t root[1024]={0};unsigned bank=s->bank==0?1:0;uint32_t base=bank_offset(s,bank),generation=store->generation+1;
    uint32_t config=store->config_revision+(changed==STORE_CONFIGURATION),network=store->network_revision+(changed==STORE_NETWORK_SETTINGS);
    memcpy(root,"LVS2",4);write_u32(root+4,1);write_u32(root+8,generation);write_u32(root+12,count);write_u32(root+16,config);write_u32(root+20,network);
    write_u32(root+24,d->capacity);write_u32(root+28,catalog_offset);write_u32(root+32,catalog_size);write_u32(root+36,crc);write_u32(root+1020,crc32_bytes(root,1020));
    if(!d->erase(d->context,base,SECTOR))return false;
    runtime_checkpoint();
    for(unsigned at=0;at<1024;at+=256){if(!d->write(d->context,base+at,root+at,256))return false;runtime_checkpoint();}
    if(!d->sync(d->context))return false;
    for(unsigned at=0;at<1024;at+=256)if(!d->read(d->context,base+at,page,256)||memcmp(page,root+at,256))return false;
    memset(page,255,256);write_u32(page,COMMIT_MAGIC);write_u32(page+4,generation);write_u32(page+8,read_u32(root+1020));write_u32(page+12,crc32_bytes(page,12));
    if(!d->write(d->context,base+COMMIT_OFFSET,page,256)||!d->sync(d->context))return false;
    uint8_t check[256];if(!d->read(d->context,base+COMMIT_OFFSET,check,256)||memcmp(check,page,256))return false;
    platform_free(s->entries);s->entries=entries;s->count=count;s->bank=(int)bank;s->catalog_offset=catalog_offset;s->catalog_size=catalog_size;
    store->generation=generation;store->config_revision=config;store->network_revision=network;return true;
}
static bool publish(Store *store,const StoreEntry *entry){
    ExtentStore *s=store->state;if(!extent_crc(s,entry->id,entry->size,entry->crc))return false;
    uint32_t index=0;while(index<s->count&&(s->entries[index].type!=entry->type||strcmp(s->entries[index].name,entry->name)))index++;
    uint32_t count=s->count+(index==s->count);if(count>s->device.capacity/SECTOR)return false;
    StoreEntry *entries=platform_calloc(count,sizeof(*entries));if(!entries)return false;
    if(s->count)memcpy(entries,s->entries,s->count*sizeof(*entries));
    entries[index]=*entry;
    bool ok=commit(store,entries,count,entry->type);if(!ok)platform_free(entries);return ok;
}
static bool remove_entry(Store *store,const StoreEntry *entry){
    ExtentStore *s=store->state;uint32_t index=(uint32_t)(entry-s->entries),count=s->count-1;StoreType type=entry->type;
    StoreEntry *entries=platform_calloc(count?count:1,sizeof(*entries));if(!entries)return false;
    if(index)memcpy(entries,s->entries,index*sizeof(*entries));
    if(index<count)memcpy(entries+index,s->entries+index+1,(count-index)*sizeof(*entries));
    bool ok=commit(store,entries,count,type);if(!ok)platform_free(entries);return ok;
}
const StoreBackend store_extent_backend={.name="extent",.mount=mount,.destroy=destroy,.entry=entry,.read=read_bytes,.info=info,.retain=retain,
    .begin=begin,.chunk=chunk,.finish=finish,.cancel=cancel,.publish=publish,.remove=remove_entry};
