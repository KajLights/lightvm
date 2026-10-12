#include "devices.h"
#include "json.h"
#include <stdio.h>
#include <string.h>
static bool validate_pixels(const Json *, int, PixelConfig *, char *, size_t);
static const DeviceType types[] = {
    {"ws2812b", validate_pixels, platform_pixels_open, platform_pixels_close, platform_pixels_show}
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
bool hardware_parse(const void *bytes, size_t size, HardwareConfig *cfg, char *error, size_t capacity) {
    Json j;
    memset(cfg, 0, sizeof(*cfg));
    cfg->pixels.pin = -1; cfg->pixels.brightness = 255;
    memcpy(cfg->pixels.order, "GRB", 4);
    static const char *const root[] = {"schema", "devices", "targets", "startup", NULL};
    if (size > CONFIG_LIMIT || !json_parse(&j, bytes, size) || !json_fields(&j, 0, root))
        return failure(error, capacity, "config: invalid JSON, duplicate/unknown fields or configuration limit");
    if (!json_equal(&j, json_get(&j, 0, "schema"), "lightvm.config/1"))
        return failure(error, capacity, "schema: expected lightvm.config/1");
    int list = json_get(&j, 0, "devices");
    if (list < 0 || j.tokens[list].type != JSON_ARRAY || j.tokens[list].children > 1)
        return failure(error, capacity, "devices: expected an array with at most one ws2812b instance");
    if (j.tokens[list].children) {
        int d = list + 1;
        static const char *const fields[] = {"id", "type", "config", NULL};
        char type[33];
        if (!json_fields(&j, d, fields) || !json_string(&j, json_get(&j, d, "id"), cfg->device, sizeof(cfg->device)) ||
            !object_name_valid(cfg->device)) return failure(error, capacity, "devices[0].id: missing or invalid instance identifier/fields");
        if (!json_string(&j, json_get(&j, d, "type"), type, sizeof(type)) || !device_type(type))
            return failure(error, capacity, "devices[0].type: unsupported device type");
        const DeviceType *descriptor = device_type(type);
        if (!descriptor->validate(&j, json_get(&j, d, "config"), &cfg->pixels, error, capacity)) return false;
        cfg->has_pixels = true;
    }
    int targets = json_get(&j, 0, "targets");
    static const char *const target_fields[] = {"A", NULL};
    if (targets >= 0) {
        if (!json_fields(&j, targets, target_fields)) return failure(error, capacity, "targets: only target A is supported");
        int a = json_get(&j, targets, "A");
        static const char *const binding[] = {"device", NULL};
        if (a >= 0) {
            if (!json_fields(&j, a, binding) || !cfg->has_pixels || !json_equal(&j, json_get(&j, a, "device"), cfg->device))
                return failure(error, capacity, "targets.A.device: missing configured pixel instance");
            cfg->target_a = true;
        }
    }
    int startup = json_get(&j, 0, "startup");
    if (startup >= 0 && !(j.tokens[startup].type == JSON_PRIMITIVE &&
        j.tokens[startup].end - j.tokens[startup].start == 4 && !memcmp(j.text + j.tokens[startup].start, "null", 4))) {
        static const char *const startup_fields[] = {"procedure", "target", NULL};
        if (!json_fields(&j, startup, startup_fields) || !cfg->target_a ||
            !json_equal(&j, json_get(&j, startup, "target"), "A") ||
            !json_string(&j, json_get(&j, startup, "procedure"), cfg->startup, sizeof(cfg->startup)) || !object_name_valid(cfg->startup))
            return failure(error, capacity, "startup: expected a named procedure and configured target A");
    }
    error[0] = 0; return true;
}
