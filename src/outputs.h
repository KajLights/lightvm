#ifndef LIGHTVM_OUTPUTS_H
#define LIGHTVM_OUTPUTS_H
#include "platform.h"
#include "config.h"
#include "json.h"
#define EMPTY_CONFIG "{\"schema\":\"lightvm.config\",\"outputs\":[]}"
#define HA_PRESET_LIMIT 16u
typedef struct {
    char id[OBJECT_NAME_LIMIT + 1], type[17];
    char startup[OBJECT_NAME_LIMIT + 1];
    char ha_default_preset[OBJECT_NAME_LIMIT + 1], ha_name[65];
    char ha_presets[HA_PRESET_LIMIT][OBJECT_NAME_LIMIT + 1];
    unsigned ha_preset_count;
    /* Byte span in the owning configuration JSON, never an upload pointer. */
    uint32_t settings_offset, settings_size;
    PixelConfig pixels;
} HardwareOutput;
typedef struct {
    unsigned count;
    HardwareOutput *outputs;
    int default_output;
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
} OutputType;
const OutputType *output_type(const char *name);
bool object_name_valid(const char *name);
void hardware_destroy(HardwareConfig *config);
bool hardware_parse(const void *bytes, size_t size, HardwareConfig *config, char *error, size_t capacity);
#endif
