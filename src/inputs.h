#ifndef LIGHTVM_INPUTS_H
#define LIGHTVM_INPUTS_H
#include "device/json.h"
#include <stdbool.h>
#include <stdint.h>

#define INPUT_LIMIT 8u
#define INPUT_SOURCE_LIMIT 16u
typedef union { uint32_t u; int32_t i; float f; } InputValue;
typedef struct { uint32_t color; float brightness; } LightControls;
typedef enum { INPUT_CONSTANT, INPUT_RAMP, INPUT_SOURCE, INPUT_CONTROL } InputKind;
typedef enum { LIGHT_CONTROL_NONE, LIGHT_CONTROL_COLOR, LIGHT_CONTROL_BRIGHTNESS } LightControl;
typedef struct {
    char name[33];
    unsigned type;
    uint32_t publisher, sequence;
    uint64_t received;
    InputValue value;
    bool valid;
    float suggested_min, suggested_max;
    const char *label, *unit; // Native source metadata; NULL for publishers.
} InputSignal;
typedef struct {
    InputSignal *signals, *native;
    unsigned count, native_count;
    uint32_t generation;
} InputRegistry;
typedef struct {
    InputKind kind;
    InputValue value;
    double from, to, duration, phase;
    uint64_t started;
    char source[33];
    double input_min, input_max, output_min, output_max;
    uint32_t timeout_ms;
    uint8_t mode, easing, control;
    bool mapped, palette, hold;
} InputDriver;
typedef struct {
    char name[33], unit[17];
    unsigned type;
    InputValue initial, minimum, maximum, snapshot, last;
    uint64_t last_received;
    double default_duration;
    uint8_t bounds, default_mode, default_easing, default_control;
    bool fallback, has_last;
    const LightControls *light;
    InputDriver driver;
} InputPort;
struct Portable;
bool input_value_parse(const Json *,int,unsigned,InputValue *);
double input_value_number(InputValue,unsigned);
const char *inputs_load(struct Portable *,const Json *);
void inputs_attach(struct Portable *,InputRegistry *,const LightControls *);
bool inputs_call_valid(struct Portable *,unsigned,unsigned);
const char *inputs_bind(struct Portable *,const Json *,int,uint64_t);
void inputs_capture(struct Portable *,uint64_t,bool);
void inputs_sample(struct Portable *,uint64_t,uint32_t *,uint8_t *);
bool inputs_changed(struct Portable *,uint64_t);
const char *input_port_bind(InputRegistry *,InputPort *,const Json *,int,uint64_t);
bool input_port_capture(InputRegistry *,InputPort *,uint64_t);
void input_binding_describe(const InputPort *,void (*)(void *,const char *),void *);
void inputs_describe(struct Portable *,void (*write)(void *,const char *),void *);
void input_registry_destroy(InputRegistry *);
const char *input_registry_claim(InputRegistry *,const Json *,int,uint32_t *);
const char *input_registry_publish(InputRegistry *,const Json *,int,uint32_t,uint32_t);
double input_ramp(double,double,double,double,double,unsigned);
#endif
