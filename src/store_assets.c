#include "store_backend.h"
#include "protocol.h"
#include "devices.h"
#include <string.h>
typedef struct { StoreAssets assets; StoreEntry entries[]; } AssetStore;
static bool mount(Store *s,const void *configuration){
    const StoreAssets *config=configuration;if(!config||(!config->assets&&config->count))return false;
    size_t count=config->count;
    if(count>(SIZE_MAX-sizeof(AssetStore))/sizeof(StoreEntry))return false;
    AssetStore *state=platform_calloc(1,sizeof(*state)+config->count*sizeof(StoreEntry));if(!state)return false;
    state->assets=*config;s->state=state;
    for(uint32_t n=0;n<config->count;n++){
        const StoreAsset *a=&config->assets[n];StoreEntry *e=&state->entries[n];
        if(!a->name||!a->data||!a->size||strlen(a->name)>OBJECT_NAME_LIMIT||a->type<STORE_ANIMATION||a->type>STORE_NETWORK_SETTINGS)return false;
        if(a->type==STORE_CONFIGURATION){if(strcmp(a->name,STORE_CONFIG))return false;}
        else if(a->type==STORE_NETWORK_SETTINGS){if(strcmp(a->name,STORE_NETWORK))return false;}
        else if(!object_name_valid(a->name))return false;
        if(a->size>(a->type==STORE_ANIMATION||a->type==STORE_TRANSITION?MODULE_LIMIT:CONFIG_LIMIT))return false;
        for(uint32_t k=0;k<n;k++)if(a->type==state->entries[k].type&&!strcmp(a->name,state->entries[k].name))return false;
        strcpy(e->name,a->name);e->type=a->type;e->id=n;e->size=a->size;e->crc=crc32_bytes(a->data,a->size);
    }
    s->available=true;return true;
}
static void destroy(Store *s){platform_free(s->state);s->state=NULL;}
static const StoreEntry *entry(const Store *s,uint32_t index){AssetStore *p=s->state;return index<p->assets.count?&p->entries[index]:NULL;}
static bool read_bytes(const Store *s,const StoreEntry *e,uint32_t at,void *bytes,size_t size){
    AssetStore *p=s->state;if(e->id>=p->assets.count)return false;
    const StoreEntry *expected=&p->entries[e->id];if(e->size!=expected->size||e->crc!=expected->crc)return false;
    memcpy(bytes,(const uint8_t *)p->assets.assets[e->id].data+at,size);return true;
}
static StoreInfo info(const Store *s){AssetStore *p=s->state;StoreInfo i={.allocation_unit=1};for(uint32_t n=0;n<p->assets.count;n++)i.total_bytes+=p->entries[n].size;return i;}
const StoreBackend store_assets_backend={.name="assets",.mount=mount,.destroy=destroy,.entry=entry,.read=read_bytes,.info=info};
