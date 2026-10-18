#ifndef LIGHTVM_INPUTS_H
#define LIGHTVM_INPUTS_H
#include "platform.h"
#define INPUT_LIMIT 4u
#define INPUT_CHANNELS 16u
#define INPUT_EVENT_LIMIT 64u
#define INPUT_EVENT_TTL_US 250000u
typedef struct {
    char id[OBJECT_NAME_LIMIT+1], indicators[OBJECT_NAME_LIMIT+1];
    char names[INPUT_CHANNELS][33];
    int32_t sda, scl;
    uint32_t address, debounce_ms;
    bool active_low, home_assistant;
} InputConfig;
typedef struct {
    InputConfig config;
    void *bus;
    uint16_t stable, candidate;
    uint64_t changed[INPUT_CHANNELS], next_sample;
    bool initialized, available, configured;
} DigitalInput;
typedef struct {
    uint64_t time;
    unsigned input, channel;
    bool pressed;
} InputEvent;
typedef struct {
    DigitalInput devices[INPUT_LIMIT];
    unsigned count, head, queued;
    uint32_t overflow;
    InputEvent events[INPUT_EVENT_LIMIT];
} Inputs;
void inputs_destroy(Inputs *);
void inputs_apply(Inputs *, const InputConfig *, unsigned);
void inputs_tick(Inputs *,uint64_t now);
bool inputs_event(Inputs *,InputEvent *);
void inputs_discard_events(Inputs *);
#endif
