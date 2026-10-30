#pragma once
#include "device/host.h"
#include "device/json.h"
#define DEVICE_NICKNAME_BYTES 63u
#define DEVICE_METADATA_BYTES 192u
typedef struct DeviceMetadata {
    Store *store;
    char nickname[DEVICE_NICKNAME_BYTES+1];
    char error[96];
} DeviceMetadata;
void device_metadata_init(DeviceMetadata *,Store *);
uint32_t device_metadata_revision(const DeviceMetadata *);
size_t device_metadata_json(const DeviceMetadata *,char *,size_t);
/* Caller serializes storage work on its management owner and pauses capture if needed. */
enum Status device_metadata_set(DeviceMetadata *,const Json *,const char *boot_id,char *,size_t);
enum Status device_metadata_validate(const DeviceMetadata *,const Json *,const char *boot_id,char *,size_t);
bool device_metadata_entry_valid(const StoreEntry *);
