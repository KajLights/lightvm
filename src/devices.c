#include "devices.h"
#include "json.h"
#include "runtime.h"
#include <stdio.h>
#include <string.h>
static bool validate_pixels(const Json *, int, PixelConfig *, char *, size_t);
static const DeviceType types[] = {
    {"ws2812b",validate_pixels,platform_pixels_open,platform_pixels_close,platform_pixels_show,platform_pixels_submit,platform_pixels_poll,platform_pixels_cancel}
};
const DeviceType *device_type(const char *name) {
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
        if (!strcmp(name, types[i].type)) return &types[i];
    return NULL;
}
bool object_name_valid(const char *name) {
    size_t n = strlen(name);
    if (!n || n > OBJECT_NAME_LIMIT) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}
static bool failure(char *error, size_t capacity, const char *message) {
    snprintf(error, capacity, "%s", message); return false;
}
static bool validate_pixels(const Json *j, int c, PixelConfig *cfg, char *error, size_t capacity) {
    static const char *const pixel_fields[] = {"pin", "count", "order", "brightness_limit", NULL};
    if (!json_fields(j, c, pixel_fields)) return failure(error, capacity, "devices[0].config: missing object or unknown fields");
    char pin[33];
    if (!json_string(j, json_get(j, c, "pin"), pin, sizeof(pin)))
        return failure(error, capacity, "devices[0].config.pin: expected an advertised pin name");
    size_t count;
    const BoardPin *pins = platform_pins(&count);
    for (size_t i = 0; i < count; ++i) if (!strcmp(pin, pins[i].name)) cfg->pin = pins[i].gpio;
    if (cfg->pin < 0) return failure(error, capacity, "devices[0].config.pin: unavailable or reserved board pin");
    if (!json_u32(j, json_get(j, c, "count"), &cfg->count) ||
        !cfg->count || cfg->count > PIXEL_CAPACITY)
        return failure(error, capacity, "devices[0].config.count: outside advertised pixel capacity");
    int brightness = json_get(j, c, "brightness_limit");
    if (brightness >= 0 && (!json_u32(j, brightness, &cfg->brightness) || cfg->brightness > 255))
        return failure(error, capacity, "devices[0].config.brightness_limit: expected 0..255");
    int order = json_get(j, c, "order");
    if (order >= 0 && (!json_string(j, order, cfg->order, sizeof(cfg->order)) ||
        strlen(cfg->order) != 3 || !strchr(cfg->order, 'R') ||
        !strchr(cfg->order, 'G') || !strchr(cfg->order, 'B')))
        return failure(error, capacity, "devices[0].config.order: expected a permutation of RGB");
    return true;
}
static bool parse(const void *bytes,size_t size,HardwareConfig *cfg,char *error,size_t capacity){
    Json j;memset(cfg,0,sizeof(*cfg));
    cfg->default_device=-1;
    static const char *const root[]={"schema","devices",NULL};
    if(size>CONFIG_LIMIT||!json_parse(&j,bytes,size)||!json_fields(&j,0,root))
        return failure(error,capacity,"config: invalid JSON, duplicate/unknown fields or configuration limit");
    if(!json_equal(&j,json_get(&j,0,"schema"),"lightvm.config"))return failure(error,capacity,"schema: expected lightvm.config");
    int list=json_get(&j,0,"devices");
    if(list<0||j.tokens[list].type!=JSON_ARRAY)
        return failure(error,capacity,"devices: array exceeds device capacity");
    cfg->devices=platform_calloc((size_t)j.tokens[list].children,sizeof(*cfg->devices));
    if(j.tokens[list].children&&!cfg->devices)return failure(error,capacity,"devices: allocation failed");
    for(int t=list+1;t<j.tokens[list].next;t=j.tokens[t].next){
        HardwareDevice *d=&cfg->devices[cfg->count];
        static const char *const fields[]={"id","type","config","startup",NULL};
        if(!json_fields(&j,t,fields)||!json_string(&j,json_get(&j,t,"id"),d->id,sizeof(d->id))||!object_name_valid(d->id))
            return failure(error,capacity,"devices.id: missing or invalid identifier/fields");
        if(!json_string(&j,json_get(&j,t,"type"),d->type,sizeof(d->type))||!device_type(d->type))
            return failure(error,capacity,"devices.type: unsupported device type");
        int startup=json_get(&j,t,"startup");
        if(startup>=0){
            static const char *const startup_fields[]={"procedure","args",NULL};
            if(!json_fields(&j,startup,startup_fields)||
               !json_string(&j,json_get(&j,startup,"procedure"),d->startup,sizeof(d->startup))||!object_name_valid(d->startup)){
                snprintf(error,capacity,"devices[%s].startup.procedure: expected a named procedure and optional args",d->id);return false;
            }
            int args=json_get(&j,startup,"args");
            if(args>=0){
                if(j.tokens[args].type!=JSON_OBJECT){
                    snprintf(error,capacity,"devices[%s].startup.args: expected an object",d->id);return false;
                }
                d->args_offset=(uint32_t)j.tokens[args].start;
                d->args_size=(uint32_t)(j.tokens[args].end-j.tokens[args].start);
            }
        }
        d->pixels.pin=-1;d->pixels.brightness=255;memcpy(d->pixels.order,"GRB",4);
        if(!device_type(d->type)->validate(&j,json_get(&j,t,"config"),&d->pixels,error,capacity))return false;
        for(unsigned n=0;n<cfg->count;n++){
            if(!strcmp(d->id,cfg->devices[n].id))return failure(error,capacity,"devices.id: duplicate identifier");
            if(d->pixels.pin>=0&&d->pixels.pin==cfg->devices[n].pixels.pin)return failure(error,capacity,"devices.config.pin: duplicate physical pin");
        }
        cfg->count++;
    }
    if(cfg->count)cfg->default_device=0;
    error[0]=0;return true;
}

void hardware_destroy(HardwareConfig *cfg){platform_free(cfg->devices);memset(cfg,0,sizeof(*cfg));cfg->default_device=-1;}
bool hardware_parse(const void *bytes,size_t size,HardwareConfig *cfg,char *error,size_t capacity){
    if(parse(bytes,size,cfg,error,capacity))return true;
    hardware_destroy(cfg);return false;
}
