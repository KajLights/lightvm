#ifndef LIGHTVM_STORE_H
#define LIGHTVM_STORE_H
#include "config.h"
#include "platform.h"
#define PRESET_LIMIT 5u
#define STORE_ENTRY_LIMIT (PROCEDURE_LIMIT + PRESET_LIMIT + 2u)
#define STORE_CONFIG "$config"
#define STORE_NETWORK "$network"
/* Presets use $p + a public name (at most 30 characters). */
bool store_preset_name(const char *name);
#define STORE_BANK_BYTES (STORAGE_BYTES / 2u)
#define STORE_DATA_OFFSET 4096u
#define STORE_PAGE_BYTES 256u
typedef struct {
    char name[OBJECT_NAME_LIMIT + 1];
    uint32_t offset, size, crc;
} StoreEntry;
typedef struct StorePin { StoreEntry entry; unsigned references; int id; struct StorePin *next; } StorePin;
typedef struct {
    int bank;
    bool available;
    uint32_t generation, config_revision, network_revision, count;
    StoreEntry entries[STORE_ENTRY_LIMIT];
    StorePin *pins;
} Store;
typedef struct {
    Store *store;
    StoreEntry entry;
    uint32_t received, crc, erased;
    int pin;
    bool active, complete;
    uint8_t page[STORE_PAGE_BYTES];
} StoreUpload;
bool store_mount(Store *store);
const StoreEntry *store_find(const Store *store, const char *name);
bool store_read(const Store *store, const StoreEntry *entry, uint32_t offset, void *bytes, size_t size);
uint32_t store_free_bytes(const Store *store);
int store_pin(Store *, const StoreEntry *);
void store_unpin(Store *, int);
bool store_upload_begin(Store *, StoreUpload *, const char *, uint32_t, uint32_t);
bool store_upload_chunk(StoreUpload *, uint32_t, const void *, size_t);
bool store_upload_finish(StoreUpload *);
void store_upload_cancel(StoreUpload *);
bool store_publish(Store *, const StoreEntry *);
bool store_put(Store *, const char *, const void *, uint32_t);
bool store_put_config(Store *,const void *,uint32_t);
bool store_delete(Store *, const char *);
#endif
