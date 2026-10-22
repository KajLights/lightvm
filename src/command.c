#include "command.h"
#include "outputs.h"
#include <string.h>

bool command_indicator_value(const Json *j,int value,Command *c){
    static const char *const fields[]={"state","brightness","color","effect",NULL};
    static const char *const rgb[]={"r","g","b",NULL};
    static const char *const effects[]={"steady","slow_blink","rapid_blink","010100"};
    if(!json_fields(j,value,fields)||!j->tokens[value].children)return false;
    int state=json_get(j,value,"state"),brightness=json_get(j,value,"brightness"),color=json_get(j,value,"color"),effect=json_get(j,value,"effect");
    if(state>=0){
        if(!json_equal(j,state,"ON")&&!json_equal(j,state,"OFF"))return false;
        c->fields|=COMMAND_POWER_VALUE;c->on=json_equal(j,state,"ON");
    }
    if(brightness>=0){uint32_t n;if(!json_u32(j,brightness,&n)||n>255)return false;c->brightness=(float)n;c->fields|=COMMAND_BRIGHTNESS;}
    if(color>=0){
        uint32_t r,g,b;
        if(!json_fields(j,color,rgb)||!json_u32(j,json_get(j,color,"r"),&r)||r>255||!json_u32(j,json_get(j,color,"g"),&g)||g>255||!json_u32(j,json_get(j,color,"b"),&b)||b>255)return false;
        c->color=(r<<16)|(g<<8)|b;c->fields|=COMMAND_COLOR;
    }
    if(effect>=0){
        unsigned e=0;while(e<4&&!json_equal(j,effect,effects[e]))e++;
        if(e==4)return false;
        c->effect=e;c->fields|=COMMAND_EFFECT;
    }
    return true;
}

const char *command_decode(const Json *j,const char *op,Command *c,bool *recognized){
    memset(c,0,sizeof(*c));*recognized=true;
    if(!strcmp(op,"output_light_get"))c->kind=COMMAND_LIGHT_GET;
    else if(!strcmp(op,"output_light_set"))c->kind=COMMAND_LIGHT_SET;
    else if(!strcmp(op,"preset_recall"))c->kind=COMMAND_RECALL;
    else if(!strcmp(op,"output_power"))c->kind=COMMAND_POWER;
    else if(!strcmp(op,"indicator_set"))c->kind=COMMAND_INDICATOR_SET;
    else if(!strcmp(op,"indicator_get"))c->kind=COMMAND_INDICATOR_GET;
    else if(!strcmp(op,"indicator_snapshot"))c->kind=COMMAND_INDICATOR_SNAPSHOT;
    else if(!strcmp(op,"key_feedback_set"))c->kind=COMMAND_KEY_SET;
    else if(!strcmp(op,"key_feedback_get"))c->kind=COMMAND_KEY_GET;
    else{*recognized=false;return NULL;}
    if(c->kind==COMMAND_KEY_SET||c->kind==COMMAND_KEY_GET){
        static const char *const fields[]={"op","input","key","enabled",NULL};
        if(!json_fields(j,0,fields)||!json_string(j,json_get(j,0,"input"),c->output,sizeof(c->output))||
           !json_u32(j,json_get(j,0,"key"),&c->pixel)||c->pixel<1||c->pixel>16)return "invalid key feedback target";
        int enabled=json_get(j,0,"enabled");
        if(c->kind==COMMAND_KEY_SET?!json_bool(j,enabled,&c->on):enabled>=0)return "invalid feedback enabled value";
        return NULL;
    }
    if(c->kind>=COMMAND_INDICATOR_GET){
        static const char *const fields[]={"op","output","pixel","value",NULL};
        static const char *const snapshot[]={"op","output",NULL};
        int out=json_get(j,0,"output");
        if(!json_fields(j,0,c->kind==COMMAND_INDICATOR_SNAPSHOT?snapshot:fields)||
           (out>=0&&(!json_string(j,out,c->output,sizeof(c->output))||!object_name_valid(c->output))))return "invalid indicator target";
        if(c->kind==COMMAND_INDICATOR_SNAPSHOT)return NULL;
        if(!json_u32(j,json_get(j,0,"pixel"),&c->pixel))return "invalid indicator pixel";
        int value=json_get(j,0,"value");
        if(c->kind==COMMAND_INDICATOR_SET?!command_indicator_value(j,value,c):value>=0)return "invalid indicator value";
        return NULL;
    }
    static const char *const get[]={"op","output",NULL};
    static const char *const light[]={"op","output","expected_revision","expected_composition_revision","brightness","color","power","preset",NULL};
    static const char *const recall[]={"op","output","name","action","expected_revision",NULL};
    if(!json_fields(j,0,c->kind==COMMAND_LIGHT_GET?get:c->kind==COMMAND_LIGHT_SET?light:recall))return "invalid command fields";
    int output=json_get(j,0,"output");
    if(output>=0&&(!json_string(j,output,c->output,sizeof(c->output))||!object_name_valid(c->output)))return "invalid output";
    if(c->kind==COMMAND_LIGHT_GET)return NULL;
    if(!json_u32(j,json_get(j,0,"expected_revision"),&c->revision))return "expected revision is required";
    if(c->kind==COMMAND_RECALL||c->kind==COMMAND_POWER){
        int name=json_get(j,0,"name");
        if(c->kind==COMMAND_RECALL||name>=0){
            if(!json_string(j,name,c->preset,sizeof(c->preset))||!object_name_valid(c->preset))return "recall requires a valid preset name";
            c->fields|=COMMAND_PRESET;
        }
        int action=json_get(j,0,"action");c->on=true;
        if(c->kind==COMMAND_RECALL){if(action>=0)return "recall does not accept action";}
        else{
            if(!json_equal(j,action,"on")&&!json_equal(j,action,"off"))return "power action must be on or off";
            c->on=json_equal(j,action,"on");
        }
        return NULL;
    }
    int brightness=json_get(j,0,"brightness"),color=json_get(j,0,"color"),power=json_get(j,0,"power"),preset=json_get(j,0,"preset");
    if(brightness>=0){
        double level;
        if(!json_f64(j,brightness,&level)||level<0||level>1)return "brightness must be in 0..1";
        c->brightness=(float)level;c->fields|=COMMAND_BRIGHTNESS;
    }
    if(color>=0){if(!json_u32(j,color,&c->color)||c->color>0xffffff)return "color must be RGB8";c->fields|=COMMAND_COLOR;}
    if(power>=0){if(!json_bool(j,power,&c->on))return "power must be boolean";c->fields|=COMMAND_POWER_VALUE;}
    if(preset>=0){
        if(!json_string(j,preset,c->preset,sizeof(c->preset))||!object_name_valid(c->preset))return "preset requires a valid name";
        c->fields|=COMMAND_PRESET;
    }
    if(!c->fields)return "at least one light property is required";
    if((c->fields&(COMMAND_POWER_VALUE|COMMAND_PRESET))&&!json_u32(j,json_get(j,0,"expected_composition_revision"),&c->composition_revision))return "expected composition revision is required";
    return NULL;
}
