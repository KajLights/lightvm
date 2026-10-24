#ifndef LIGHTVM_COMMAND_H
#define LIGHTVM_COMMAND_H
#include "device/json.h"
#include "assets.h"

/* Owned values only: this request survives release of its transport parser. */
typedef enum { COMMAND_LIGHT_GET, COMMAND_LIGHT_SET, COMMAND_RECALL, COMMAND_POWER,
    COMMAND_INDICATOR_GET,COMMAND_INDICATOR_SET,COMMAND_INDICATOR_SNAPSHOT,
    COMMAND_KEY_GET,COMMAND_KEY_SET } CommandKind;
enum { COMMAND_BRIGHTNESS=1, COMMAND_COLOR=2, COMMAND_POWER_VALUE=4, COMMAND_PRESET=8,COMMAND_EFFECT=16 };
typedef struct {
    CommandKind kind;
    char output[OBJECT_NAME_LIMIT+1],preset[OBJECT_NAME_LIMIT+1];
    unsigned fields;
    uint32_t revision,composition_revision,color,pixel,effect;
    float brightness;
    bool on;
} Command;
/* NULL is success; recognized is false for another management operation. */
const char *command_decode(const Json *,const char *op,Command *,bool *recognized);
bool command_indicator_value(const Json *,int value,Command *);
#endif
