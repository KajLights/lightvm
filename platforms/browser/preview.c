/* Browser adapter for the same checked C VM used by the controllers.
 * One worker owns one instance. Only completed frames become visible. */
#include "portable.h"
#include "animation.h"
#include "platform.h"
#include "vm_allocator.h"
#include <emscripten/emscripten.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static Portable *vm;
static uint8_t *package;
static size_t package_size;
static uint8_t working[512*4], displayed[512*4];
static LbcBuffer surface;
static const char *error;
static char fault_message[160];
static unsigned phase; /* 0 starting, 1 drawing, 2 completed, 3 failed */

uint64_t platform_time_us(void){return (uint64_t)(emscripten_get_now()*1000.0);}
void *platform_calloc(size_t count,size_t size){return calloc(count,size);}
void *platform_realloc(void *pointer,size_t size){return realloc(pointer,size);}
void platform_free(void *pointer){free(pointer);}
void runtime_checkpoint(void){}
EM_JS(bool, platform_random_bytes, (void *destination,size_t size), {
    try { globalThis.crypto.getRandomValues(HEAPU8.subarray(destination,destination+size)); return true; }
    catch { return false; }
});

static bool read_package(void *context,uint32_t offset,void *bytes,size_t size){
    (void)context;if(offset>package_size||size>package_size-offset)return false;
    memcpy(bytes,package+offset,size);return true;
}
EMSCRIPTEN_KEEPALIVE void preview_close(void){
    pbc_destroy(vm);vm=NULL;free(package);package=NULL;package_size=0;
    memset(working,0,sizeof(working));memset(displayed,0,sizeof(displayed));
    error=NULL;phase=3;
}
static bool invoke(unsigned entry,float seconds,float delta){
    PbcValue arguments[3]={{.i=(int32_t)surface.count},{.f=seconds},{.f=delta}};
    vm->now_us=(uint64_t)((double)seconds*1000000.0);
    inputs_capture(vm,vm->now_us,vm->profile==4);
    return pbc_invoke(vm,vm->entries[entry],arguments,vm->profile==4?2:3);
}
EMSCRIPTEN_KEEPALIVE int preview_open(const uint8_t *bytes,unsigned size,unsigned count,unsigned seed,const char *settings){
    preview_close();
    if(size>32768||!size||!count||count>512){error="Choose 1 to 512 pixels and a valid compiled program.";return -1;}
    package=malloc(size);if(!package){error="The preview could not allocate memory.";return -1;}
    memcpy(package,bytes,size);package_size=size;
    error=pbc_open(&vm,read_package,NULL,size);if(error)return -1;
    error=animation_validate(vm);if(error)return -1;
    if(vm->profile==4){error="Try animations here. To test a transition, load A and B in Mixer and use Preview on device.";return -1;}
    if(settings&&*settings){
        Json j;if(!json_parse(&j,settings,strlen(settings))||j.tokens[0].type!=JSON_OBJECT){error="Settings need a saved name and value.";return -1;}
        for(int t=1;t<j.tokens[0].next;t=j.tokens[t+1].next){
            char name[33];unsigned n;if(!json_string(&j,t,name,sizeof(name))){error="This setting name is too long.";return -1;}
            for(n=0;n<vm->nsettings;n++)if(!strcmp(name,vm->setting_names[n]))break;
            if(n==vm->nsettings||!input_value_parse(&j,t+1,pbc_type(vm,pbc_setting(vm,n).reg),&vm->overrides[n])){error="Check this setting name and value.";return -1;}
            vm->override_mask|=1u<<n;
        }
    }
    vm->deterministic_random=true;vm->default_seed=seed;
    surface=(LbcBuffer){.data=working,.count=count,.generation=1};
    error=pbc_prepare(vm,&surface);if(error)return -1;
    if(!invoke(0,0,0)){error="The starting function could not run.";return -1;}
    phase=0;return 0;
}
EMSCRIPTEN_KEEPALIVE int preview_frame(float seconds,float delta){
    if(!vm||phase!=2)return -1;
    if(!isfinite(seconds)||!isfinite(delta)||seconds<0||delta<0||seconds>1e9f){error="Time must be a positive, finite number of seconds.";return -1;}
    memset(working,0,surface.count*4);
    if(!invoke(1,seconds,delta)){error="The frame could not start.";return -1;}
    phase=1;return 0;
}
EMSCRIPTEN_KEEPALIVE int preview_step(unsigned quota){
    if(!vm||error||phase==3)return -1;
    if(phase==2)return 1;
    if(quota>4096)quota=4096;
    int status=pbc_resume(vm,quota);
    if(status<0){error=pbc_fault_message(vm,fault_message,sizeof(fault_message));phase=3;return -1;}
    if(status>0){
        if(phase==0){
            memset(working,0,surface.count*4);
            if(!invoke(1,0,0)){error="The first frame could not start.";phase=3;return -1;}
            phase=1;return 0;
        }
        memcpy(displayed,working,surface.count*4);phase=2;vm->render_count++;return 1;
    }
    return 0;
}
EMSCRIPTEN_KEEPALIVE const char *preview_error(void){return error?error:"";}
EMSCRIPTEN_KEEPALIVE unsigned preview_pc(void){return vm?(phase==3?vm->fault_pc:vm->pc):0;}
EMSCRIPTEN_KEEPALIVE unsigned preview_phase(void){return phase;}
EMSCRIPTEN_KEEPALIVE const uint8_t *preview_pixels(void){return displayed;}
EMSCRIPTEN_KEEPALIVE unsigned preview_word(unsigned reg){return vm&&reg<vm->nregs?pbc_value(vm,reg).u:0;}
EMSCRIPTEN_KEEPALIVE unsigned preview_type(unsigned reg){return vm&&reg<vm->nregs?pbc_type(vm,reg):0;}
EMSCRIPTEN_KEEPALIVE unsigned preview_heap(void){return (unsigned)vm_heap_used();}
EMSCRIPTEN_KEEPALIVE int preview_input(unsigned index,double value){
    if(!vm||(phase!=0&&phase!=2)||index>=vm->ninputs||!isfinite(value))return -1;
    InputPort *port=&vm->inputs[index];
    if((port->bounds&1)&&value<input_value_number(port->minimum,port->type))return -1;
    if((port->bounds&2)&&value>input_value_number(port->maximum,port->type))return -1;
    InputValue v;
    if(port->type==T_FLOAT){v.f=(float)value;if(!isfinite(v.f))return -1;}
    else {if(value<INT32_MIN||value>INT32_MAX||floor(value)!=value)return -1;v.i=(int32_t)value;
        if((port->type==T_COLOR&&(value<0||value>0xffffff))||(port->type==T_BOOL&&(value<0||value>1)))return -1;}
    port->driver.kind=INPUT_CONSTANT;port->driver.value=v;return 0;
}
