#include "json.h"
#include <string.h>
#include <limits.h>
static void whitespace(Json *j) {
    while (j->position < j->size && strchr(" \t\r\n", j->text[j->position])) ++j->position;
}
static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool json_string(const Json *j, int t, char *out, size_t capacity) {
    if (t < 0 || t >= j->count || j->tokens[t].type != JSON_STRING || !capacity) return false;
    size_t used = 0;
    for (size_t p = j->tokens[t].start; p < j->tokens[t].end; ++p) {
        unsigned char c = (unsigned char)j->text[p];
        if (c == '\\') {
            c = (unsigned char)j->text[++p];
            const char *esc = "\"\\/bfnrt", *decoded = "\"\\/\b\f\n\r\t";
            const char *found = strchr(esc, c);
            if (found) c = (unsigned char)decoded[found - esc];
            else if (c == 'u') {
                unsigned value = 0;
                for (int k = 0; k < 4; ++k) value = value * 16u + (unsigned)hex(j->text[++p]);
                // Config identifiers and field names are ASCII. Reject lossy decoding.
                if (value > 127 || !value) return false;
                c = (unsigned char)value;
            } else return false;
        }
        if (!c || used + 1 >= capacity) return false;
        out[used++] = (char)c;
    }
    out[used] = 0;
    return true;
}
bool json_equal(const Json *j, int t, const char *value) {
    char buffer[128];
    return json_string(j, t, buffer, sizeof(buffer)) && !strcmp(buffer, value);
}
static int parse_value(Json *j, unsigned depth) {
    whitespace(j);
    if (depth > 16 || j->position == j->size || j->count == 256) return -1;
    int index = j->count++;
    JsonToken *t = &j->tokens[index];
    t->start = j->position;
    char c = j->text[j->position++];
    if (c == '{' || c == '[') {
        t->type = c == '{' ? JSON_OBJECT : JSON_ARRAY;
        char close = c == '{' ? '}' : ']';
        whitespace(j);
        if (j->position < j->size && j->text[j->position] == close) ++j->position;
        else for (;;) {
            if (t->type == JSON_OBJECT) {
                int key = parse_value(j, depth + 1);
                char name[128];
                if (key < 0 || !json_string(j, key, name, sizeof(name))) return -1;
                // Reject duplicate keys, including escaped spellings.
                for (int k = index + 1; k < key; k = j->tokens[k + 1].next)
                    if (json_equal(j, k, name)) return -1;
                whitespace(j);
                if (j->position == j->size || j->text[j->position++] != ':') return -1;
            }
            if (parse_value(j, depth + 1) < 0) return -1;
            ++t->children;
            whitespace(j);
            if (j->position == j->size) return -1;
            char delimiter = j->text[j->position++];
            if (delimiter == close) break;
            if (delimiter != ',') return -1;
        }
        t->end = j->position;
    } else if (c == '"') {
        t->type = JSON_STRING;
        t->start = j->position;
        while (j->position < j->size && j->text[j->position] != '"') {
            unsigned char b = (unsigned char)j->text[j->position++];
            if (b < 32) return -1;
            if (b == '\\') {
                if (j->position == j->size) return -1;
                char e = j->text[j->position++];
                if (e == 'u') {
                    for (int k = 0; k < 4; ++k)
                        if (j->position == j->size || hex(j->text[j->position++]) < 0) return -1;
                } else if (!strchr("\"\\/bfnrt", e)) return -1;
            }
        }
        if (j->position == j->size) return -1;
        t->end = j->position++;
    } else {
        t->type = JSON_PRIMITIVE;
        size_t p = t->start;
        const char *literal = c == 't' ? "true" : c == 'f' ? "false" : c == 'n' ? "null" : NULL;
        if (literal) {
            size_t n = strlen(literal);
            if (n > j->size - p || memcmp(j->text + p, literal, n)) return -1;
            p += n;
        } else {
            if (c == '-') ++p;
            if (p == j->size || j->text[p] < '0' || j->text[p] > '9') return -1;
            if (j->text[p++] != '0') while (p < j->size && j->text[p] >= '0' && j->text[p] <= '9') ++p;
            if (p < j->size && j->text[p] == '.') {
                size_t begin = ++p;
                while (p < j->size && j->text[p] >= '0' && j->text[p] <= '9') ++p;
                if (p == begin) return -1;
            }
            if (p < j->size && (j->text[p] == 'e' || j->text[p] == 'E')) {
                ++p;
                if (p < j->size && (j->text[p] == '+' || j->text[p] == '-')) ++p;
                size_t begin = p;
                while (p < j->size && j->text[p] >= '0' && j->text[p] <= '9') ++p;
                if (p == begin) return -1;
            }
        }
        t->end = j->position = p;
    }
    t->next = j->count;
    return index;
}
bool json_parse(Json *j, const void *bytes, size_t size) {
    memset(j, 0, sizeof(*j));
    j->text = bytes; j->size = size;
    if (!size || memchr(bytes, 0, size) || parse_value(j, 0) < 0) return false;
    whitespace(j);
    return j->position == size;
}
int json_get(const Json *j, int object, const char *key) {
    if (object < 0 || j->tokens[object].type != JSON_OBJECT) return -1;
    for (int k = object + 1; k < j->tokens[object].next; k = j->tokens[k + 1].next)
        if (json_equal(j, k, key)) return k + 1;
    return -1;
}
bool json_u32(const Json *j, int t, uint32_t *value) {
    if (t < 0 || j->tokens[t].type != JSON_PRIMITIVE) return false;
    uint32_t n = 0;
    for (size_t p = j->tokens[t].start; p < j->tokens[t].end; ++p) {
        char c = j->text[p];
        if (c < '0' || c > '9' || n > (UINT32_MAX - (unsigned)(c - '0')) / 10u) return false;
        n = n * 10u + (unsigned)(c - '0');
    }
    *value = n; return true;
}
bool json_fields(const Json *j, int object, const char *const *allowed) {
    if (object < 0 || j->tokens[object].type != JSON_OBJECT) return false;
    for (int k = object + 1; k < j->tokens[object].next; k = j->tokens[k + 1].next) {
        bool found = false;
        for (size_t n = 0; allowed[n]; ++n) if (json_equal(j, k, allowed[n])) found = true;
        if (!found) return false;
    }
    return true;
}

// Bounded conversion: JSON grammar is already checked, but conversion must also
// reject fractional integers, overflow and non-finite floating-point values.
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
static bool primitive(const Json *j,int t,char *buffer,size_t capacity) {
    if (t<0 || j->tokens[t].type!=JSON_PRIMITIVE) return false;
    size_t size=j->tokens[t].end-j->tokens[t].start;
    if (!size || size>=capacity) return false;
    memcpy(buffer,j->text+j->tokens[t].start,size); buffer[size]=0; return true;
}
bool json_i64(const Json *j,int t,int64_t *value) {
    char buffer[32],*end;
    if (!primitive(j,t,buffer,sizeof(buffer))) return false;
    errno=0; long long n=strtoll(buffer,&end,10);
    if (errno || *end || end==buffer || n<INT64_MIN || n>INT64_MAX) return false;
    *value=(int64_t)n; return true;
}
bool json_f64(const Json *j,int t,double *value) {
    char buffer[64],*end;
    if (!primitive(j,t,buffer,sizeof(buffer))) return false;
    errno=0; double n=strtod(buffer,&end);
    // strtod may set ERANGE for a finite subnormal (or rounded zero).
    if (*end || end==buffer || !isfinite(n)) return false;
    *value=n; return true;
}
bool json_bool(const Json *j,int t,bool *value) {
    char buffer[8];
    if (!primitive(j,t,buffer,sizeof(buffer))) return false;
    if (!strcmp(buffer,"true")) { *value=true; return true; }
    if (!strcmp(buffer,"false")) { *value=false; return true; }
    return false;
}
