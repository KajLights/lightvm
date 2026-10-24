#ifndef KAJ_DEVICE_STORE_H
#define KAJ_DEVICE_STORE_H
#include "device/config.h"
#include "device/platform.h"
#define STORE_CONFIG "$config"
#define STORE_NETWORK "$network"
#define STORE_TYPE_LIMIT 16u
#define STORE_PAGE_BYTES 256u
typedef uint8_t StoreType;
enum { STORE_ANY=0, STORE_CONFIGURATION=4, STORE_NETWORK_SETTINGS=5 };
/* id is an opaque backend version token. Applications must not interpret it. */
typedef struct { char name[DEVICE_NAME_LIMIT+1]; StoreType type; uint32_t id,size,crc; } StoreEntry;
typedef struct StoreBackend StoreBackend;
typedef struct StorePin StorePin;
typedef struct StoreWrite StoreWrite;
typedef struct Store {
    const StoreBackend *backend;
    void *state;
    bool available;
    uint32_t generation,config_revision,network_revision;
    uint32_t type_revision[STORE_TYPE_LIMIT];
    StorePin *pins;
    StoreWrite *writer;
} Store;
typedef struct {
    Store *store;
    StoreEntry entry;
    void *state;
    uint32_t received;
    int pin;
    bool active,complete;
} StoreUpload;
struct StoreWrite {
    Store *store;
    StoreEntry entry;
    const uint8_t *data;
    void *state;
};
/* The caller owns immutable data through completion/cancel. One catalogue writer
 * at a time; readers and existing pinned VMs remain available between steps. */
bool store_write_begin(Store *,StoreWrite *,StoreType,const char *,const void *,uint32_t);
int store_write_step(StoreWrite *); /* 0 pending, 1 published, -1 failed */
void store_write_cancel(StoreWrite *);
typedef struct { uint32_t total_bytes,free_bytes,largest_free_bytes,allocation_unit; bool writable; } StoreInfo;
/* Application schema hooks; byte storage itself has no application types. */
bool store_entry_valid(const StoreEntry *);
bool store_transient_valid(StoreType,uint32_t);
const char *store_type_name(StoreType);
StoreType store_type(const char *);
bool store_mount(Store *);
bool store_mount_with(Store *,const StoreBackend *,const void *configuration);
void store_destroy(Store *);
const StoreEntry *store_find(const Store *,StoreType,const char *);
/* Cursor starts at zero. Mutation invalidates catalogue pointers and cursors. */
const StoreEntry *store_list(const Store *,StoreType,uint32_t *cursor);
StoreInfo store_info(const Store *);
const char *store_backend_name(const Store *);
bool store_read(const Store *,const StoreEntry *,uint32_t,void *,size_t);
/* A retained entry is immutable and readable until the matching unpin. */
int store_pin(Store *,const StoreEntry *);
void store_unpin(Store *,int);
bool store_upload_begin(Store *,StoreUpload *,StoreType,const char *,uint32_t,uint32_t);
bool store_upload_chunk(StoreUpload *,uint32_t,const void *,size_t);
bool store_upload_finish(StoreUpload *);
void store_upload_cancel(StoreUpload *);
bool store_publish(Store *,const StoreEntry *);
bool store_put(Store *,StoreType,const char *,const void *,uint32_t);
bool store_put_config(Store *,const void *,uint32_t);
bool store_delete(Store *,StoreType,const char *);
#endif
