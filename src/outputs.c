#include "outputs.h"
#include "json.h"
#include "runtime.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
static bool validate_pixels(const Json *, int, PixelConfig *, char *, size_t);
static bool validate_apa102(const Json *, int, PixelConfig *, char *, size_t);
static const OutputType types[] = {
    {"ws2812b",validate_pixels,platform_pixels_open,platform_pixels_close,platform_pixels_show,platform_pixels_submit,platform_pixels_poll,platform_pixels_cancel},
    {"apa102",validate_apa102,platform_apa102_open,platform_apa102_close,platform_apa102_show,platform_apa102_submit,platform_apa102_poll,platform_apa102_cancel}
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
static int32_t pin(const Json *j,int object,const char *field) {
    char name[33];if(!json_string(j,json_get(j,object,field),name,sizeof(name)))return -1;
    size_t count;const BoardPin *pins=platform_pins(&count);
    for(size_t n=0;n<count;n++)if(!strcmp(name,pins[n].name))return pins[n].gpio;
    return -1;
}
static bool validate_apa102(const Json *j,int object,PixelConfig *c,char *error,size_t capacity) {
    static const char *const fields[]={"data_pin","clock_pin","enable_pin","count","order","brightness_limit","frequency",NULL};
    if(!platform_apa102_supported()||!json_fields(j,object,fields))return failure(error,capacity,"apa102: unavailable on this port or unknown config fields");
    c->pin=pin(j,object,"data_pin");c->clock_pin=pin(j,object,"clock_pin");c->enable_pin=-1;
    if(json_get(j,object,"enable_pin")>=0){c->enable_pin=pin(j,object,"enable_pin");if(c->enable_pin<0)return failure(error,capacity,"apa102: invalid enable_pin");}
    if(c->pin<0||c->clock_pin<0||c->pin==c->clock_pin||c->enable_pin==c->pin||c->enable_pin==c->clock_pin)
        return failure(error,capacity,"apa102: requires distinct advertised data, clock and optional active-low enable pins");
    if(!json_u32(j,json_get(j,object,"count"),&c->count)||!c->count||c->count>PIXEL_CAPACITY)
        return failure(error,capacity,"apa102.count: outside pixel capacity");
    c->frequency=1000000;memcpy(c->order,"BGR",4);
    int t=json_get(j,object,"frequency");
    if(t>=0&&(!json_u32(j,t,&c->frequency)||c->frequency<100000||c->frequency>8000000))return failure(error,capacity,"apa102.frequency: expected 100000..8000000 Hz");
    t=json_get(j,object,"brightness_limit");
    if(t>=0&&(!json_u32(j,t,&c->brightness)||c->brightness>255))return failure(error,capacity,"apa102.brightness_limit: expected 0..255");
    t=json_get(j,object,"order");
    if(t>=0&&(!json_string(j,t,c->order,sizeof(c->order))||strlen(c->order)!=3||!strchr(c->order,'R')||!strchr(c->order,'G')||!strchr(c->order,'B')))
        return failure(error,capacity,"apa102.order: expected a permutation of RGB");
    return true;
}
static bool parse_inputs(const Json *j,HardwareConfig *cfg,char *error,size_t capacity) {
    int list=json_get(j,0,"inputs");if(list<0)return true;
    if(j->tokens[list].type!=JSON_ARRAY||(unsigned)j->tokens[list].children>DIGITAL_INPUT_LIMIT)return failure(error,capacity,"inputs: expected at most four inputs");
    for(int t=list+1;t<j->tokens[list].next;t=j->tokens[t].next){
        DigitalInputConfig *d=&cfg->inputs[cfg->input_count];
        static const char *const fields[]={"id","type","config","home_assistant",NULL};
        static const char *const config_fields[]={"sda","scl","address","active_low","debounce_ms","long_press_ms","multi_press_ms",NULL};
        int c=json_get(j,t,"config");
        if(!json_fields(j,t,fields)||!json_equal(j,json_get(j,t,"type"),"tca9555")||
           !json_string(j,json_get(j,t,"id"),d->id,sizeof(d->id))||!object_name_valid(d->id)||!json_fields(j,c,config_fields))
            return failure(error,capacity,"inputs: expected named tca9555 with config");
        d->sda=pin(j,c,"sda");d->scl=pin(j,c,"scl");d->address=32;d->active_low=true;d->debounce_ms=20;
        d->long_press_ms=DIGITAL_INPUT_LONG_PRESS_MS;d->multi_press_ms=DIGITAL_INPUT_MULTI_PRESS_MS;
        int a=json_get(j,c,"address"),b=json_get(j,c,"debounce_ms"),low=json_get(j,c,"active_low");
        if(!platform_i2c_valid(d->sda,d->scl)||(a>=0&&(!json_u32(j,a,&d->address)||d->address<32||d->address>39))||
           (b>=0&&(!json_u32(j,b,&d->debounce_ms)||d->debounce_ms<5||d->debounce_ms>100))||
           (low>=0&&!json_bool(j,low,&d->active_low)))return failure(error,capacity,"tca9555: unsupported I2C pins, address (32..39), debounce_ms (5..100) or polarity");
        int hold=json_get(j,c,"long_press_ms"),multi=json_get(j,c,"multi_press_ms");
        if((hold>=0&&(!json_u32(j,hold,&d->long_press_ms)||d->long_press_ms<100||d->long_press_ms>5000))||
           (multi>=0&&(!json_u32(j,multi,&d->multi_press_ms)||d->multi_press_ms<100||d->multi_press_ms>1000)))
            return failure(error,capacity,"tca9555: long_press_ms (100..5000), multi_press_ms (100..1000)");
        for(unsigned n=0;n<cfg->count;n++){
            const PixelConfig *p=&cfg->outputs[n].pixels;const int32_t pins[]={p->pin,p->clock_pin,p->enable_pin};
            for(unsigned k=0;k<3;k++)if(pins[k]==d->sda||pins[k]==d->scl)return failure(error,capacity,"inputs: I2C pin conflicts with output");
        }
        for(unsigned n=0;n<cfg->input_count;n++){
            const DigitalInputConfig *p=&cfg->inputs[n];
            if(!strcmp(p->id,d->id))return failure(error,capacity,"inputs: duplicate identifier");
            // Pico has two buses; aliases for one bus must use the same pin pair.
            if(((p->sda>>1)&1)==((d->sda>>1)&1)&&
               (p->sda!=d->sda||p->scl!=d->scl||p->address==d->address))return failure(error,capacity,"inputs: conflicting I2C bus pins or address");
        }
        int ha=json_get(j,t,"home_assistant");
        if(ha>=0){
            static const char *const ha_fields[]={"indicators","names",NULL};
            if(!json_fields(j,ha,ha_fields))return failure(error,capacity,"inputs.home_assistant: unknown fields");
            d->home_assistant=true;
            int indicators=json_get(j,ha,"indicators");
            if(indicators>=0){
                if(!json_string(j,indicators,d->indicators,sizeof(d->indicators)))return failure(error,capacity,"inputs.home_assistant.indicators: expected output ID");
                HardwareOutput *output=NULL;
                for(unsigned n=0;n<cfg->count;n++)if(!strcmp(d->indicators,cfg->outputs[n].id))output=&cfg->outputs[n];
                if(!output||!output->indicators||output->pixels.count!=16)return failure(error,capacity,"inputs.home_assistant.indicators: requires a 16-pixel indicator output");
                for(unsigned n=0;n<cfg->input_count;n++)if(!strcmp(d->indicators,cfg->inputs[n].indicators))return failure(error,capacity,"inputs: indicator output already bound");
            }
            int names=json_get(j,ha,"names");
            if(names>=0){
                if(j->tokens[names].type!=JSON_ARRAY||j->tokens[names].children!=16)return failure(error,capacity,"inputs.home_assistant.names: expected sixteen names");
                unsigned n=0;
                for(int k=names+1;k<j->tokens[names].next;k=j->tokens[k].next,n++)if(!json_string(j,k,d->names[n],sizeof(d->names[n]))||!d->names[n][0])return failure(error,capacity,"inputs.home_assistant.names: expected 1..32 UTF-8 bytes");
            }
        }
        cfg->input_count++;
    }
    return true;
}
bool transition_spec_parse(const Json *j,int token,TransitionSpec *spec,char *error,size_t capacity) {
    memset(spec,0,sizeof(*spec));
    if(token<0)return true;
    static const char *const fields[]={"program","crc32","settings","inputs","seed","duration_ms",NULL};
    if(!json_fields(j,token,fields)||
       !json_string(j,json_get(j,token,"program"),spec->program,sizeof(spec->program))||!object_name_valid(spec->program)||
       !json_u32(j,json_get(j,token,"crc32"),&spec->crc)||
       !json_f64(j,json_get(j,token,"duration_ms"),&spec->duration_ms)||spec->duration_ms<0||!isfinite(spec->duration_ms*1000))
        return failure(error,capacity,"transition requires program, crc32 and finite nonnegative duration_ms");
    int seed=json_get(j,token,"seed"),settings=json_get(j,token,"settings");
    int inputs=json_get(j,token,"inputs");
    if(inputs>=0&&j->tokens[inputs].type!=JSON_OBJECT)return failure(error,capacity,"transition inputs must be an object");
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
    static const char *const root[]={"schema","outputs","inputs",NULL};
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
        static const char *const fields[]={"id","type","config","mode","startup","home_assistant",NULL};
        if(!json_fields(&j,t,fields)||!json_string(&j,json_get(&j,t,"id"),d->id,sizeof(d->id))||!object_name_valid(d->id))
            return failure(error,capacity,"outputs.id: missing or invalid identifier/fields");
        if(!json_string(&j,json_get(&j,t,"type"),d->type,sizeof(d->type))||!output_type(d->type))
            return failure(error,capacity,"outputs.type: unsupported output type");
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
                if(!json_string(&j,p,name,OBJECT_NAME_LIMIT+1)||!object_name_valid(name)||!strcmp(name,"off")||!strcmp(name,"Custom"))
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
            static const char *const startup_fields[]={"mode","preset",NULL};
            int mode=json_get(&j,startup,"mode"),preset=json_get(&j,startup,"preset");
            bool valid=json_fields(&j,startup,startup_fields);
            if(json_equal(&j,mode,"off"))d->startup_mode=STARTUP_OFF;
            else if(json_equal(&j,mode,"restore_last"))d->startup_mode=STARTUP_RESTORE_LAST;
            else if(json_equal(&j,mode,"preset"))d->startup_mode=STARTUP_PRESET;
            else valid=false;
            if(d->startup_mode==STARTUP_PRESET)
                valid=valid&&json_string(&j,preset,d->startup_preset,sizeof(d->startup_preset))&&object_name_valid(d->startup_preset);
            else if(preset>=0)valid=false;
            if(!valid){snprintf(error,capacity,"outputs[%s].startup: expected mode off, restore_last, or preset with a preset name",d->id);return false;}
        }
        int mode=json_get(&j,t,"mode");
        d->indicators=json_equal(&j,mode,"indicators");
        if(mode>=0&&!d->indicators&&!json_equal(&j,mode,"animation"))return failure(error,capacity,"outputs.mode: expected animation or indicators");
        if(d->indicators&&(startup>=0||ha>=0))return failure(error,capacity,"indicator outputs cannot have startup or presets");
        d->pixels.pin=-1;d->pixels.clock_pin=-1;d->pixels.enable_pin=-1;d->pixels.brightness=255;memcpy(d->pixels.order,"GRB",4);
        if(!output_type(d->type)->validate(&j,json_get(&j,t,"config"),&d->pixels,error,capacity))return false;
        if(d->indicators&&d->pixels.count>16)return failure(error,capacity,"indicator outputs support at most sixteen pixels");
        for(unsigned n=0;n<cfg->count;n++){
            if(!strcmp(d->id,cfg->outputs[n].id))return failure(error,capacity,"outputs.id: duplicate identifier");
            const PixelConfig *p=&cfg->outputs[n].pixels;
            const int32_t old[]={p->pin,p->clock_pin,p->enable_pin},pins[]={d->pixels.pin,d->pixels.clock_pin,d->pixels.enable_pin};
            for(unsigned x=0;x<3;x++)for(unsigned y=0;y<3;y++)if(pins[x]>=0&&pins[x]==old[y])return failure(error,capacity,"outputs.config.pin: duplicate physical pin");
        }
        cfg->count++;
    }
    if(cfg->count)cfg->default_output=0;
    if(!parse_inputs(&j,cfg,error,capacity))return false;
    error[0]=0;return true;
}

void hardware_destroy(HardwareConfig *cfg){platform_free(cfg->outputs);memset(cfg,0,sizeof(*cfg));cfg->default_output=-1;}
bool hardware_parse(const void *bytes,size_t size,HardwareConfig *cfg,char *error,size_t capacity){
    if(parse(bytes,size,cfg,error,capacity))return true;
    hardware_destroy(cfg);return false;
}
