#ifndef LIGHTVM_DEVICES_H
#define LIGHTVM_DEVICES_H
#include "platform.h"
#include "config.h"
#include "json.h"
#define EMPTY_CONFIG "{\"schema\":\"lightvm.config/1\",\"devices\":[],\"targets\":{}}"
typedef struct {
    bool has_pixels, target_a;
    char device[OBJECT_NAME_LIMIT + 1], startup[OBJECT_NAME_LIMIT + 1];
    PixelConfig pixels;
} HardwareConfig;
typedef struct {
    const char *type;
    bool (*validate)(const Json *, int, PixelConfig *, char *, size_t);
    bool (*open)(const PixelConfig *, void **);
    void (*close)(void *);
    bool (*show)(void *, const uint8_t *, size_t);
} DeviceType;
const DeviceType *device_type(const char *name);
bool object_name_valid(const char *name);
bool hardware_parse(const void *bytes, size_t size, HardwareConfig *config, char *error, size_t capacity);
#endif
