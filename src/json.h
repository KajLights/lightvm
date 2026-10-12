#ifndef LIGHTVM_JSON_H
#define LIGHTVM_JSON_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum { JSON_OBJECT, JSON_ARRAY, JSON_STRING, JSON_PRIMITIVE } JsonType;
typedef struct { size_t start, end; int next, children; JsonType type; } JsonToken;
typedef struct {
    const char *text;
    size_t size, position;
    JsonToken tokens[256];
    int count;
} Json;
bool json_parse(Json *json, const void *bytes, size_t size);
int json_get(const Json *json, int object, const char *key);
bool json_string(const Json *json, int token, char *out, size_t capacity);
bool json_equal(const Json *json, int token, const char *value);
bool json_u32(const Json *json, int token, uint32_t *value);
bool json_fields(const Json *json, int object, const char *const *allowed);
#endif
