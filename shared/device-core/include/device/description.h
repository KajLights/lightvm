#pragma once
#include "device/host.h"
typedef void (*DeviceDescriptionWrite)(void *,const char *);
void device_json_write_string(DeviceDescriptionWrite,void *,const char *);
/* Generated once per resolved build; these definitions are shared by all clients. */
extern const char device_firmware_json[],device_features_json[],device_hardware_json[],device_updates_json[];
extern const char device_product_version[];
/* Writes object members without enclosing braces, so applications can stream their own fields. */
void device_description(const DeviceHost *,const char *limits_json,DeviceDescriptionWrite,void *);
