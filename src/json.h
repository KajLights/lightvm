#ifndef LIGHTVM_JSON_H
#define LIGHTVM_JSON_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Decimal JSON integer, including a terminator; returns the byte count. */
size_t json_format_u64(char out[21],uint64_t value);
typedef enum { JSON_OBJECT, JSON_ARRAY, JSON_STRING, JSON_PRIMITIVE } JsonType;
/* Documents contain at most 65,535 bytes and 256 tokens. The next index
 * includes sentinel 256; a container can have at most 255 direct children. */
typedef struct { uint16_t start, end, next; uint8_t children, type; } JsonToken;
typedef struct {
    const char *text;
    size_t size, position;
    JsonToken tokens[256];
    int count;
} Json;
_Static_assert(sizeof(((Json *)0)->tokens)/sizeof(JsonToken)<=UINT8_MAX+1u,"JSON child counts must fit uint8_t");
/* Caller owns both parser and immutable source until step returns +/-1.
 * Each unit consumes one byte, delimiter, or duplicate-key comparison. */
typedef struct {
    Json *json;
    int parents[17], token, key, sibling;
    uint8_t phases[17], depth, lex, escape, unicode, number, literal;
    bool root_done;
    int status;
    char key_name[128];
} JsonParser;
void json_begin(JsonParser *,Json *,const void *,size_t);
int json_step(JsonParser *,unsigned units);
bool json_parse(Json *json, const void *bytes, size_t size);
int json_get(const Json *json, int object, const char *key);
bool json_string(const Json *json, int token, char *out, size_t capacity);
bool json_equal(const Json *json, int token, const char *value);
bool json_u32(const Json *json, int token, uint32_t *value);
bool json_i64(const Json *json, int token, int64_t *value);
bool json_f64(const Json *json, int token, double *value);
bool json_bool(const Json *json, int token, bool *value);
bool json_fields(const Json *json, int object, const char *const *allowed);
#endif
