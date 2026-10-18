#ifndef LIGHTVM_DIGITAL_INPUTS_H
#define LIGHTVM_DIGITAL_INPUTS_H
#include "platform.h"
// Native digital acquisition and events; animation signal bindings live in inputs.h.
#define DIGITAL_INPUT_LIMIT 4u
#define DIGITAL_INPUT_CHANNELS 16u
#define DIGITAL_INPUT_EVENT_LIMIT 64u
#define DIGITAL_INPUT_EVENT_TTL_US 250000u
typedef struct {
    char id[OBJECT_NAME_LIMIT+1], indicators[OBJECT_NAME_LIMIT+1];
    char names[DIGITAL_INPUT_CHANNELS][33];
    int32_t sda, scl;
    uint32_t address, debounce_ms;
    bool active_low, home_assistant;
} DigitalInputConfig;
typedef struct {
    DigitalInputConfig config;
    void *bus;
    uint16_t stable, candidate;
    uint64_t changed[DIGITAL_INPUT_CHANNELS], next_sample;
    bool initialized, available, configured;
} DigitalInput;
typedef struct {
    uint64_t time;
    unsigned input, channel;
    bool pressed;
} DigitalInputEvent;
typedef struct {
    DigitalInput devices[DIGITAL_INPUT_LIMIT];
    unsigned count, head, queued;
    uint32_t overflow;
    DigitalInputEvent events[DIGITAL_INPUT_EVENT_LIMIT];
} DigitalInputs;
void digital_inputs_destroy(DigitalInputs *);
void digital_inputs_apply(DigitalInputs *, const DigitalInputConfig *, unsigned);
void digital_inputs_tick(DigitalInputs *,uint64_t now);
bool digital_inputs_event(DigitalInputs *,DigitalInputEvent *);
void digital_inputs_discard_events(DigitalInputs *);
#endif
