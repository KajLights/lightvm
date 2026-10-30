#include "assets.h"
#include "outputs.h"
#include "runtime.h"
#include "device/metadata.h"
#include <string.h>
const char *store_type_name(StoreType type) {
    static const char *const names[]={"","animation","transition","preset","config","network"};
    return (unsigned)type<=STORE_NETWORK_SETTINGS?names[type]:"";
}
StoreType store_type(const char *name) {
    for(StoreType t=STORE_ANIMATION;t<=STORE_NETWORK_SETTINGS;t++)if(!strcmp(name,store_type_name(t)))return t;
    return STORE_ANY;
}
bool store_entry_valid(const StoreEntry *e) {
    if(e->type==STORE_DEVICE_METADATA)return device_metadata_entry_valid(e);
    bool system=e->type==STORE_CONFIGURATION||e->type==STORE_NETWORK_SETTINGS;
    return e->type>=STORE_ANIMATION&&e->type<=STORE_KEY_FEEDBACK&&
        (system? !strcmp(e->name,e->type==STORE_CONFIGURATION?STORE_CONFIG:STORE_NETWORK):object_name_valid(e->name))&&
        e->size&&e->size<=(e->type==STORE_ANIMATION||e->type==STORE_TRANSITION?MODULE_LIMIT:CONFIG_LIMIT);
}

void device_checkpoint(void) { runtime_checkpoint(); }

bool store_transient_valid(StoreType type,uint32_t size) { return type==STORE_ANIMATION && size && size<=MODULE_LIMIT; }
