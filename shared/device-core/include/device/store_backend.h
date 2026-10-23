#ifndef KAJ_DEVICE_STORE_BACKEND_H
#define KAJ_DEVICE_STORE_BACKEND_H
#include "device/store.h"
/* Implementations own state and version tokens. publish/remove must commit
 * atomically, update revisions only on success, and preserve pinned versions.
 * No backend callback may re-enter a storage operation on this Store. */
struct StoreBackend {
    const char *name;
    bool (*mount)(Store *,const void *);
    void (*destroy)(Store *);
    const StoreEntry *(*entry)(const Store *,uint32_t);
    bool (*read)(const Store *,const StoreEntry *,uint32_t,void *,size_t);
    StoreInfo (*info)(const Store *);
    bool (*retain)(Store *,const StoreEntry *);
    void (*release)(Store *,const StoreEntry *);
    bool (*begin)(Store *,StoreUpload *);
    bool (*chunk)(StoreUpload *,uint32_t,const void *,size_t);
    bool (*finish)(StoreUpload *);
    void (*cancel)(StoreUpload *);
    bool (*publish)(Store *,const StoreEntry *);
    bool (*remove)(Store *,const StoreEntry *);
    int (*write_step)(StoreWrite *);
    void (*write_cancel)(StoreWrite *);
};
struct StorePin { StoreEntry entry; unsigned references; int id; StorePin *next; };
extern const StoreBackend store_extent_backend,store_assets_backend;
/* Optional injected block device; NULL selects the platform flash region.
 * Geometry matches the existing 4096-byte erase / 256-byte program contract. */
typedef struct {
    void *context;
    uint32_t capacity;
    bool (*read)(void *,uint32_t,void *,size_t);
    bool (*write)(void *,uint32_t,const void *,size_t);
    bool (*erase)(void *,uint32_t,size_t);
    bool (*sync)(void *);
} StoreBlockDevice;
typedef struct { StoreType type; const char *name; const void *data; uint32_t size; } StoreAsset;
typedef struct { const StoreAsset *assets; uint32_t count; } StoreAssets;
#endif
