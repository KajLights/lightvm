#ifndef LIGHTVM_STORE_H
#define LIGHTVM_STORE_H
#include "config.h"
#include "platform.h"
#define STORE_ENTRY_LIMIT (PROCEDURE_LIMIT + 1u)
#define STORE_CONFIG "$config"
#define STORE_BANK_BYTES (STORAGE_BYTES / 2u)
#define STORE_DATA_OFFSET 4096u
typedef struct {
    char name[OBJECT_NAME_LIMIT + 1];
    uint32_t offset, size, crc;
} StoreEntry;
typedef struct {
    int bank;
    bool available;
    uint32_t generation, config_revision, count;
    StoreEntry entries[STORE_ENTRY_LIMIT];
} Store;
bool store_mount(Store *store);
const StoreEntry *store_find(const Store *store, const char *name);
bool store_read(const Store *store, const StoreEntry *entry, uint32_t offset, void *bytes, size_t size);
uint32_t store_free_bytes(const Store *store);
// Each mutation writes a complete new snapshot, preserving the current bank.
bool store_put(Store *store, const char *name, const void *bytes, uint32_t size);
bool store_delete(Store *store, const char *name);
#endif
