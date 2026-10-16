#ifndef LIGHTVM_DEVICES_H
#define LIGHTVM_DEVICES_H
#include "platform.h"
#include "config.h"
#include "json.h"
#define EMPTY_CONFIG "{\"schema\":\"lightvm.config/1\",\"devices\":[]}"
typedef struct {
    char id[OBJECT_NAME_LIMIT + 1], type[17];
    char startup[OBJECT_NAME_LIMIT + 1];
    /* Byte span in the owning configuration JSON, never an upload pointer. */
    uint32_t args_offset, args_size;
    PixelConfig pixels;
} HardwareDevice;
typedef struct {
    unsigned count;
    HardwareDevice *devices;
    int default_device;
} HardwareConfig;
typedef struct {
    const char *type;
    bool (*validate)(const Json *, int, PixelConfig *, char *, size_t);
    bool (*open)(const PixelConfig *, void **);
    void (*close)(void *);
    bool (*show)(void *, const uint8_t *, size_t);
    bool (*submit)(void *, const uint8_t *, size_t);
    int (*poll)(void *);
    void (*cancel)(void *);
} DeviceType;
const DeviceType *device_type(const char *name);
bool object_name_valid(const char *name);
void hardware_destroy(HardwareConfig *config);
bool hardware_parse(const void *bytes, size_t size, HardwareConfig *config, char *error, size_t capacity);
#endif
