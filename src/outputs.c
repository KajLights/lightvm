#include "outputs.h"
#include "json.h"
#include "runtime.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
static bool validate_pixels(const Json *, int, PixelConfig *, char *, size_t);
static const OutputType types[] = {
    {"ws2812b",validate_pixels,platform_pixels_open,platform_pixels_close,platform_pixels_show,platform_pixels_submit,platform_pixels_poll,platform_pixels_cancel}
};
const OutputType *output_type(const char *name) {
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
bool transition_spec_parse(const Json *j,int token,TransitionSpec *spec,char *error,size_t capacity) {
    memset(spec,0,sizeof(*spec));
    if(token<0)return true;
    static const char *const fields[]={"program","crc32","settings","seed","duration_ms",NULL};
    if(!json_fields(j,token,fields)||
       !json_string(j,json_get(j,token,"program"),spec->program,sizeof(spec->program))||!object_name_valid(spec->program)||
       !json_u32(j,json_get(j,token,"crc32"),&spec->crc)||
       !json_f64(j,json_get(j,token,"duration_ms"),&spec->duration_ms)||spec->duration_ms<0||!isfinite(spec->duration_ms*1000))
        return failure(error,capacity,"transition requires program, crc32 and finite nonnegative duration_ms");
    int seed=json_get(j,token,"seed"),settings=json_get(j,token,"settings");
    if((seed>=0&&!json_u32(j,seed,&spec->seed))||(settings>=0&&j->tokens[settings].type!=JSON_OBJECT))
        return failure(error,capacity,"transition settings must be an object and seed an unsigned integer");
    spec->offset=(uint32_t)j->tokens[token].start;spec->size=(uint32_t)(j->tokens[token].end-j->tokens[token].start);
    return true;
}
static bool validate_pixels(const Json *j, int c, PixelConfig *cfg, char *error, size_t capacity) {
    static const char *const pixel_fields[] = {"pin", "count", "order", "brightness_limit", NULL};
    if (!json_fields(j, c, pixel_fields)) return failure(error, capacity, "outputs[0].config: missing object or unknown fields");
    char pin[33];
    if (!json_string(j, json_get(j, c, "pin"), pin, sizeof(pin)))
        return failure(error, capacity, "outputs[0].config.pin: expected an advertised pin name");
    size_t count;
    const BoardPin *pins = platform_pins(&count);
    for (size_t i = 0; i < count; ++i) if (!strcmp(pin, pins[i].name)) cfg->pin = pins[i].gpio;
    if (cfg->pin < 0) return failure(error, capacity, "outputs[0].config.pin: unavailable or reserved board pin");
    if (!json_u32(j, json_get(j, c, "count"), &cfg->count) ||
        !cfg->count || cfg->count > PIXEL_CAPACITY)
        return failure(error, capacity, "outputs[0].config.count: outside advertised pixel capacity");
    int brightness = json_get(j, c, "brightness_limit");
    if (brightness >= 0 && (!json_u32(j, brightness, &cfg->brightness) || cfg->brightness > 255))
        return failure(error, capacity, "outputs[0].config.brightness_limit: expected 0..255");
    int order = json_get(j, c, "order");
    if (order >= 0 && (!json_string(j, order, cfg->order, sizeof(cfg->order)) ||
        strlen(cfg->order) != 3 || !strchr(cfg->order, 'R') ||
        !strchr(cfg->order, 'G') || !strchr(cfg->order, 'B')))
        return failure(error, capacity, "outputs[0].config.order: expected a permutation of RGB");
    return true;
}
static bool parse(const void *bytes,size_t size,HardwareConfig *cfg,char *error,size_t capacity){
    Json j;memset(cfg,0,sizeof(*cfg));
    cfg->default_output=-1;
    static const char *const root[]={"schema","outputs",NULL};
    if(size>CONFIG_LIMIT||!json_parse(&j,bytes,size)||!json_fields(&j,0,root))
        return failure(error,capacity,"config: invalid JSON, duplicate/unknown fields or configuration limit");
    if(!json_equal(&j,json_get(&j,0,"schema"),"lightvm.config"))return failure(error,capacity,"schema: expected lightvm.config");
    int list=json_get(&j,0,"outputs");
    if(list<0||j.tokens[list].type!=JSON_ARRAY)
        return failure(error,capacity,"outputs: array exceeds output capacity");
    cfg->outputs=platform_calloc((size_t)j.tokens[list].children,sizeof(*cfg->outputs));
    if(j.tokens[list].children&&!cfg->outputs)return failure(error,capacity,"outputs: allocation failed");
    for(int t=list+1;t<j.tokens[list].next;t=j.tokens[t].next){
        HardwareOutput *d=&cfg->outputs[cfg->count];
        static const char *const fields[]={"id","type","config","startup","home_assistant","on_transition","off_transition",NULL};
        if(!json_fields(&j,t,fields)||!json_string(&j,json_get(&j,t,"id"),d->id,sizeof(d->id))||!object_name_valid(d->id))
            return failure(error,capacity,"outputs.id: missing or invalid identifier/fields");
        if(!json_string(&j,json_get(&j,t,"type"),d->type,sizeof(d->type))||!output_type(d->type))
            return failure(error,capacity,"outputs.type: unsupported output type");
        if(!transition_spec_parse(&j,json_get(&j,t,"on_transition"),&d->on_transition,error,capacity)||
           !transition_spec_parse(&j,json_get(&j,t,"off_transition"),&d->off_transition,error,capacity))return false;
        int ha=json_get(&j,t,"home_assistant");
        if(ha>=0){
            static const char *const ha_fields[]={"default_preset","presets","name",NULL};
            if(!json_fields(&j,ha,ha_fields) ||
               !json_string(&j,json_get(&j,ha,"default_preset"),d->ha_default_preset,sizeof(d->ha_default_preset)) ||
               !object_name_valid(d->ha_default_preset))
                return failure(error,capacity,"home_assistant.default_preset: expected a stored preset name (1..32 bytes)");
            int presets=json_get(&j,ha,"presets");bool has_default=false;
            if(presets<0||j.tokens[presets].type!=JSON_ARRAY||!j.tokens[presets].children||(unsigned)j.tokens[presets].children>HA_PRESET_LIMIT)
                return failure(error,capacity,"home_assistant.presets: expected 1..16 unique preset names");
            for(int p=presets+1;p<j.tokens[presets].next;p=j.tokens[p].next){
                char *name=d->ha_presets[d->ha_preset_count];
                if(!json_string(&j,p,name,OBJECT_NAME_LIMIT+1)||(!object_name_valid(name)||!strcmp(name,"None")))
                    return failure(error,capacity,"home_assistant.presets: invalid preset name");
                for(unsigned n=0;n<d->ha_preset_count;n++)if(!strcmp(name,d->ha_presets[n]))
                    return failure(error,capacity,"home_assistant.presets: duplicate preset name");
                has_default|=!strcmp(name,d->ha_default_preset);d->ha_preset_count++;
            }
            if(!has_default)return failure(error,capacity,"home_assistant.default_preset: must appear in presets");
            int name=json_get(&j,ha,"name");
            if(name>=0 && (!json_string(&j,name,d->ha_name,sizeof(d->ha_name)) || !d->ha_name[0]))
                return failure(error,capacity,"home_assistant.name: expected 1..64 UTF-8 bytes");
            unsigned exposed=1;
            for(unsigned n=0;n<cfg->count;n++)if(cfg->outputs[n].ha_default_preset[0])exposed++;
            if(exposed>8)return failure(error,capacity,"home_assistant: at most eight exposed outputs");
        }
        int startup=json_get(&j,t,"startup");
        if(startup>=0){
            static const char *const startup_fields[]={"program","settings",NULL};
            if(!json_fields(&j,startup,startup_fields)||
               !json_string(&j,json_get(&j,startup,"program"),d->startup,sizeof(d->startup))||!object_name_valid(d->startup)){
                snprintf(error,capacity,"outputs[%s].startup.program: expected a named program and optional settings",d->id);return false;
            }
            int args=json_get(&j,startup,"settings");
            if(args>=0){
                if(j.tokens[args].type!=JSON_OBJECT){
                    snprintf(error,capacity,"outputs[%s].startup.settings: expected an object",d->id);return false;
                }
                d->settings_offset=(uint32_t)j.tokens[args].start;
                d->settings_size=(uint32_t)(j.tokens[args].end-j.tokens[args].start);
            }
        }
        d->pixels.pin=-1;d->pixels.brightness=255;memcpy(d->pixels.order,"GRB",4);
        if(!output_type(d->type)->validate(&j,json_get(&j,t,"config"),&d->pixels,error,capacity))return false;
        for(unsigned n=0;n<cfg->count;n++){
            if(!strcmp(d->id,cfg->outputs[n].id))return failure(error,capacity,"outputs.id: duplicate identifier");
            if(d->pixels.pin>=0&&d->pixels.pin==cfg->outputs[n].pixels.pin)return failure(error,capacity,"outputs.config.pin: duplicate physical pin");
        }
        cfg->count++;
    }
    if(cfg->count)cfg->default_output=0;
    error[0]=0;return true;
}

void hardware_destroy(HardwareConfig *cfg){platform_free(cfg->outputs);memset(cfg,0,sizeof(*cfg));cfg->default_output=-1;}
bool hardware_parse(const void *bytes,size_t size,HardwareConfig *cfg,char *error,size_t capacity){
    if(parse(bytes,size,cfg,error,capacity))return true;
    hardware_destroy(cfg);return false;
}
