#ifndef LIGHTVM_OUTPUTS_H
#define LIGHTVM_OUTPUTS_H
#include "platform.h"
#include "config.h"
#include "json.h"
#include "digital_inputs.h"
#define EMPTY_CONFIG "{\"schema\":\"lightvm.config\",\"outputs\":[]}"
#define HA_PRESET_LIMIT 16u
typedef enum { STARTUP_OFF, STARTUP_RESTORE_LAST, STARTUP_PRESET } StartupMode;
typedef struct {
    char program[OBJECT_NAME_LIMIT + 1];
    uint32_t crc, seed, offset, size;
    double duration_ms;
} TransitionSpec;
typedef struct {
    char id[OBJECT_NAME_LIMIT + 1], type[17];
    StartupMode startup_mode;
    char startup_preset[OBJECT_NAME_LIMIT + 1];
    char ha_default_preset[OBJECT_NAME_LIMIT + 1], ha_name[65];
    char ha_presets[HA_PRESET_LIMIT][OBJECT_NAME_LIMIT + 1];
    unsigned ha_preset_count;
    bool indicators;
    PixelConfig pixels;
} HardwareOutput;
typedef struct {
    unsigned count;
    HardwareOutput *outputs;
    int default_output;
    unsigned input_count;
    DigitalInputConfig *inputs;
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
bool transition_spec_parse(const Json *, int, TransitionSpec *, char *, size_t);
void hardware_destroy(HardwareConfig *config);
bool hardware_decode(const Json *,HardwareConfig *,char *,size_t);
bool hardware_parse(const void *bytes, size_t size, HardwareConfig *config, char *error, size_t capacity);
#endif
