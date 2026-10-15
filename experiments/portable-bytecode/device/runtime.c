/* Shared resident runtime: flash transactions, scheduling and native buffer ownership. */
#include "poc_platform.h"
#include "vm.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
enum { PIXELS=POC_PIXELS, BYTES=PIXELS*3, PERIOD=16667, SAMPLES=1024, QUANTUM=64 };
typedef struct {
    const PocFlash *partition;const uint8_t *mapping;uintptr_t map_handle;
    LbcProgram program;LbcBuffer buffer;LbcInstance instance;uint32_t revision,length,crc;
} Effect;
static Effect *active,*candidate;
static bool output_pending,running=true,native_mode;
static uint32_t held_hash,verified_outputs;
static uint32_t render_elapsed,render_max_slice,render_frames,errors,service_max,output_gap_max,missed_ticks;
static uint32_t render_samples[SAMPLES],gap_samples[SAMPLES],sample_count;
static uint64_t render_sum,last_service,last_submit,next_tick,origin;
static float native_phase;static unsigned native_pixel;static bool rendering,ticking;
static uint32_t logical_tick;static unsigned presentation_divisor=1;
static char input[1024];static unsigned input_used;static bool discard_input;
static const PocFlash *upload;static uint32_t upload_length,received,upload_crc,crc_running,erase_at,erase_end,upload_revision;
static uint8_t upload_chunk[128];static unsigned upload_chunk_length;
static bool erasing,validating,preparing;static uint64_t upload_started;
static unsigned bench_count,bench_at,bench_errors;static uint64_t bench_vm,bench_native;static uint32_t bench_vm_max,bench_native_max;
static float bench_phase;static uint8_t bench_pixels[BYTES],bench_ref[BYTES];static LbcBuffer bench_buffer;static LbcInstance bench_instance;
static uint64_t now(void){return (uint64_t)poc_now();}
static void reply(const char *format,...){
    char line[1000];va_list args;va_start(args,format);int n=vsnprintf(line,sizeof(line)-2,format,args);va_end(args);
    if(n<0||n>(int)sizeof(line)-2)return;
    line[n++]='\n';
    int sent=poc_write(line,(size_t)n);if(sent!=n)errors++;
}
static void put32(uint8_t *p,uint32_t v){for(unsigned k=0;k<4;k++)p[k]=(uint8_t)(v>>(k*8));}
static void destroy(Effect *e){if(e){if(e->mapping)poc_flash_unmap(e->map_handle);free(e->buffer.data);free(e);}}
static Effect *open_effect(const PocFlash *partition,uint32_t length,uint32_t crc,uint32_t revision){
    if(!length||length>LBC_MAX_BYTES||length>partition->size-4096)return NULL;
    Effect *e=calloc(1,sizeof(*e));if(!e)return NULL;e->partition=partition;e->length=length;e->crc=crc;e->revision=revision;
    const void *mapped=poc_flash_map(partition,4096,length,&e->map_handle);
    if(!mapped){destroy(e);return NULL;}
    e->mapping=mapped;
    if(!lbc_open(&e->program,mapped,length)){destroy(e);return NULL;}
    e->buffer=(LbcBuffer){.data=calloc(PIXELS,3),.count=PIXELS,.generation=revision};
    if(!e->buffer.data){destroy(e);return NULL;}return e;
}
static Effect *boot_effect(const PocFlash *partition){
    uint8_t h[24];if(!poc_flash_read(partition,0,h,sizeof(h))||memcmp(h,"ACT1",4)||poc_crc32(0,h,20)!=lbc_u32(h+20)||lbc_u32(h+16)!=1)return NULL;
    Effect *e=open_effect(partition,lbc_u32(h+8),lbc_u32(h+12),lbc_u32(h+4));if(!e)return NULL;
    if(poc_crc32(0,e->mapping,e->length)!=e->crc){destroy(e);return NULL;}
    int status;do{status=lbc_validate(&e->program,32);poc_yield();}while(status==0);
    if(status<0||!lbc_init(&e->instance,&e->program,&e->buffer)||!lbc_begin(&e->instance,0,0)){destroy(e);return NULL;}
    do{status=lbc_resume(&e->instance,QUANTUM);poc_yield();}while(status==0);
    if(status<0){destroy(e);return NULL;}return e;
}
static uint32_t pixel_hash(const uint8_t *p){uint32_t h=2166136261u;for(unsigned k=0;k<BYTES;k++){h^=p[k];h*=16777619u;}return h;}
static void output_poll(void){
    if(output_pending&&poc_output_done()){
        output_pending=false;
        if(active){if(pixel_hash(active->buffer.data)!=held_hash)errors++;else verified_outputs++;active->buffer.sealed=false;}
    }
}
static void submit(void){
    uint8_t *p=active->buffer.data;
    poc_prepare_pixels(p,BYTES);
    uint64_t t=now();uint32_t gap=last_submit?(uint32_t)(t-last_submit):0;last_submit=t;
    if(gap>output_gap_max)output_gap_max=gap;
    if(sample_count<SAMPLES){render_samples[sample_count]=render_elapsed;gap_samples[sample_count++]=gap;}
    render_frames++;render_sum+=render_elapsed;
    held_hash=pixel_hash(p);active->buffer.sealed=true;output_pending=true;
    if(!poc_output(p,BYTES)){errors++;output_pending=false;active->buffer.sealed=false;}
}
static void render(void){
    if(!active||!running||output_pending||bench_count)return;
    uint64_t t=now();
    if(!rendering){
        if(t<next_tick)return;
        /* One declared 60 Hz logical tick per frame in this first device profile.
         * Missed ticks are dropped explicitly, never converted into a giant dt. */
        if(t>next_tick+PERIOD){uint32_t skip=(uint32_t)((t-next_tick)/PERIOD);missed_ticks+=skip;next_tick+=(uint64_t)skip*PERIOD;}
        next_tick+=PERIOD;render_elapsed=0;native_pixel=0;rendering=true;
        uint64_t start=now();
        if(native_mode){logical_tick++;native_phase+=1.0f/60.0f*0.1f;native_phase-=floorf(native_phase);}
        else if(!lbc_tick(&active->instance,(float)(++logical_tick)/60.0f,1.0f/60.0f)){errors++;running=false;rendering=false;return;}
        ticking=!native_mode;
        render_elapsed+=(uint32_t)(now()-start);
        if(native_mode&&logical_tick%presentation_divisor){rendering=false;return;}
    }
    uint64_t start=now();int status;
    if(native_mode){unsigned end=native_pixel+32;if(end>PIXELS)end=PIXELS;lbc_native_rainbow(active->buffer.data,native_pixel,end,native_phase,1.0f/PIXELS);native_pixel=end;status=end==PIXELS;}
    else status=lbc_resume(&active->instance,QUANTUM);
    uint32_t dt=(uint32_t)(now()-start);render_elapsed+=dt;if(dt>render_max_slice)render_max_slice=dt;
    if(status<0){errors++;running=false;rendering=false;reply("{\"event\":\"fault\"}");}
    else if(status==1){
        if(ticking){
            ticking=false;
            if(logical_tick%presentation_divisor){rendering=false;return;}
            if(!lbc_render(&active->instance,(float)logical_tick/60.0f)){errors++;running=false;rendering=false;}
        }else{rendering=false;submit();}
    }
}
static void install_step(void){
    if(erasing){
        if(output_pending)return; /* Do not stall an in-flight output transfer. */
        if(!poc_flash_erase(upload,erase_at,4096)){erasing=false;upload=NULL;reply("{\"event\":\"error\",\"reason\":\"erase\"}");return;}
        erase_at+=4096;if(erase_at>=erase_end){erasing=false;reply("{\"event\":\"ready\",\"offset\":0}");}return;
    }
    if(upload_chunk_length){
        if(output_pending)return;
        if(!poc_flash_write(upload,4096+received,upload_chunk,upload_chunk_length)){upload_chunk_length=0;upload=NULL;reply("{\"event\":\"error\",\"reason\":\"write\"}");return;}
        crc_running=poc_crc32(crc_running,upload_chunk,upload_chunk_length);received+=upload_chunk_length;upload_chunk_length=0;
        reply("{\"event\":\"ack\",\"offset\":%u}",(unsigned)received);return;
    }
    if(validating){
        int status=lbc_validate(&candidate->program,32);
        if(status<0){destroy(candidate);candidate=NULL;validating=false;upload=NULL;reply("{\"event\":\"rejected\",\"reason\":\"validation\"}");return;}
        if(status==1){validating=false;preparing=lbc_init(&candidate->instance,&candidate->program,&candidate->buffer)&&lbc_begin(&candidate->instance,0,0);
            if(!preparing){destroy(candidate);candidate=NULL;upload=NULL;reply("{\"event\":\"rejected\",\"reason\":\"init\"}");}}
        return;
    }
    if(preparing){
        int status=lbc_resume(&candidate->instance,QUANTUM);
        if(status<0){destroy(candidate);candidate=NULL;preparing=false;upload=NULL;reply("{\"event\":\"rejected\",\"reason\":\"prepare\"}");return;}
        if(status==1)preparing=false;
        return;
    }
    if(candidate&&!output_pending&&!rendering){
        uint8_t record[24]={0};memcpy(record,"ACT1",4);put32(record+4,candidate->revision);put32(record+8,candidate->length);put32(record+12,candidate->crc);put32(record+16,1);put32(record+20,poc_crc32(0,record,20));
        if(!poc_flash_write(candidate->partition,0,record,sizeof(record))){destroy(candidate);candidate=NULL;upload=NULL;reply("{\"event\":\"rejected\",\"reason\":\"commit\"}");return;}
        Effect *old=active;active=candidate;candidate=NULL;upload=NULL;destroy(old);native_mode=false;logical_tick=0;running=true;origin=now();next_tick=origin;
        reply("{\"event\":\"activated\",\"revision\":%u,\"bytes\":%u,\"elapsed_us\":%"PRIu64",\"flash_mapped\":true}",(unsigned)active->revision,(unsigned)active->length,now()-upload_started);
    }
}
static int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static void stats(void){
    reply("{\"event\":\"stats\",\"mode\":\"%s\",\"frames\":%u,\"samples\":%u,\"render_mean_us\":%.3f,\"slice_max_us\":%u,\"output_gap_max_us\":%u,\"service_gap_max_us\":%u,\"missed_ticks\":%u,\"errors\":%u,\"verified_outputs\":%u,\"free_heap\":%u,\"min_free_heap\":%u,\"heap_total\":%u,\"instance_bytes\":%u,\"effect_bytes\":%u,\"pixel_bytes\":%u,\"revision\":%u}",native_mode?"native":"bytecode",(unsigned)render_frames,sample_count,render_frames?(double)render_sum/render_frames:0,(unsigned)render_max_slice,(unsigned)output_gap_max,(unsigned)service_max,(unsigned)missed_ticks,(unsigned)errors,(unsigned)verified_outputs,(unsigned)poc_heap_free(),(unsigned)poc_heap_min(),(unsigned)poc_heap_total(),(unsigned)sizeof(LbcInstance),(unsigned)sizeof(Effect),BYTES,active?(unsigned)active->revision:0);
}
static void command(char *line){
    unsigned a,b;char extra;
    if(!strcmp(line,"hello")){reply("{\"event\":\"hello\",\"platform\":\"%s\",\"pixels\":%u,\"gpio\":%u,\"brightness\":%u,\"cpu_mhz\":%u,\"revision\":%u,\"flash_mapped\":%s,\"package_bytes\":%u}",poc_platform.name,PIXELS,poc_platform.gpio,poc_platform.brightness,poc_platform.cpu_mhz,active?(unsigned)active->revision:0,active?"true":"false",active?(unsigned)active->length:0);}
    else if(!strcmp(line,"stats"))stats();
    else if(!strcmp(line,"measure")){render_sum=0;render_frames=render_max_slice=sample_count=service_max=output_gap_max=missed_ticks=0;last_submit=0;reply("{\"event\":\"measuring\"}");}
    else if(sscanf(line,"sample %u %c",&a,&extra)==1&&a<sample_count){reply("{\"event\":\"sample\",\"index\":%u,\"render_us\":%u,\"gap_us\":%u}",a,(unsigned)render_samples[a],(unsigned)gap_samples[a]);}
    else if(sscanf(line,"ping %u %c",&a,&extra)==1){reply("{\"event\":\"pong\",\"id\":%u}",a);}
    else if(sscanf(line,"begin %u %u %c",&a,&b,&extra)==2){
        if(upload||candidate||bench_count||!a||a>LBC_MAX_BYTES||(active&&active->revision==UINT32_MAX)){reply("{\"event\":\"error\",\"reason\":\"admission\"}");return;}
        upload=poc_slots[active&&active->partition==poc_slots[0]?1:0];upload_length=a;upload_crc=b;received=crc_running=erase_at=0;erase_end=4096+((a+4095)/4096)*4096;upload_revision=active?active->revision+1:1;erasing=true;upload_started=now();reply("{\"event\":\"erasing\"}");
    }
    else if(!strncmp(line,"data ",5)){
        char *end;unsigned long offset=strtoul(line+5,&end,10);if(*end!=' '||!upload||erasing||candidate||upload_chunk_length||offset!=received){reply("{\"event\":\"error\",\"reason\":\"offset\"}");return;}end++;
        size_t n=strlen(end);if(!n||n%2||n/2>128||n/2>upload_length-received){reply("{\"event\":\"error\",\"reason\":\"chunk\"}");return;}
        uint8_t bytes[128];for(size_t k=0;k<n;k+=2){if(hex(end[k])<0||hex(end[k+1])<0){reply("{\"event\":\"error\",\"reason\":\"hex\"}");return;}bytes[k/2]=(uint8_t)(hex(end[k])*16+hex(end[k+1]));}
        memcpy(upload_chunk,bytes,n/2);upload_chunk_length=(unsigned)n/2;
    }
    else if(!strcmp(line,"load")){
        if(upload_chunk_length){reply("{\"event\":\"error\",\"reason\":\"busy\"}");return;}
        if(!upload||erasing||candidate||received!=upload_length||crc_running!=upload_crc){if(!candidate&&!erasing)upload=NULL;reply("{\"event\":\"rejected\",\"reason\":\"integrity\"}");return;}
        candidate=open_effect(upload,upload_length,upload_crc,upload_revision);
        if(!candidate){upload=NULL;reply("{\"event\":\"rejected\",\"reason\":\"header\"}");return;}
        if(poc_crc32(0,candidate->mapping,candidate->length)!=upload_crc){destroy(candidate);candidate=NULL;upload=NULL;reply("{\"event\":\"rejected\",\"reason\":\"flash_crc\"}");return;}
        validating=true;reply("{\"event\":\"validating\"}");
    }
    else if(!strcmp(line,"cancel")){if(!erasing){destroy(candidate);candidate=NULL;upload=NULL;upload_chunk_length=0;validating=preparing=false;reply("{\"event\":\"cancelled\"}");}else reply("{\"event\":\"error\",\"reason\":\"busy\"}");}
    else if(sscanf(line,"rate %u %c",&a,&extra)==1&&(a==30||a==60)){presentation_divisor=60/a;reply("{\"event\":\"rate\",\"fps\":%u}",a);}
    else if(!strcmp(line,"stop")){running=false;rendering=false;reply("{\"event\":\"stopped\"}");}
    else if(!strcmp(line,"play")&&active){if(!rendering&&active->instance.status==0)active->instance.status=1;running=true;next_tick=now();reply("{\"event\":\"playing\"}");}
    else if(!strcmp(line,"mode native")||!strcmp(line,"mode bytecode")){
        if(!active||rendering||output_pending||candidate||bench_count){reply("{\"event\":\"error\",\"reason\":\"busy\"}");return;}
        native_mode=!strcmp(line,"mode native");lbc_init(&active->instance,&active->program,&active->buffer);native_phase=0;logical_tick=0;next_tick=origin=now();reply("{\"event\":\"mode\",\"native\":%s}",native_mode?"true":"false");
    }
    else if(sscanf(line,"parameter %u %u %c",&a,&b,&extra)==2&&active){bool ok=lbc_parameter(&active->instance,a,b);reply("{\"event\":\"%s\"}",ok?"parameter":"busy");}
    else if(sscanf(line,"benchmark %u %c",&a,&extra)==1&&a&&a<=10000){
        if(!active||rendering||output_pending||candidate||upload||bench_count){reply("{\"event\":\"error\",\"reason\":\"busy\"}");return;}
        bench_buffer=(LbcBuffer){bench_pixels,PIXELS,1,false};lbc_init(&bench_instance,&active->program,&bench_buffer);bench_count=a;bench_at=bench_errors=bench_vm_max=bench_native_max=0;bench_vm=bench_native=0;bench_phase=0;reply("{\"event\":\"benchmarking\"}");
    }
    else if(!strcmp(line,"restart")){reply("{\"event\":\"restarting\"}");poc_restart();}
    else reply("{\"event\":\"error\",\"reason\":\"command\"}");
}
static void service(void){
    uint64_t t=now();uint32_t gap=(uint32_t)(t-last_service);last_service=t;if(gap>service_max)service_max=gap;
    uint8_t bytes[128];int n=poc_read(bytes,sizeof(bytes));
    for(int k=0;k<n;k++){
        if(bytes[k]=='\n'){if(!discard_input){input[input_used]=0;command(input);}input_used=0;discard_input=false;}
        else if(!discard_input){if(input_used<sizeof(input)-1)input[input_used++]=(char)bytes[k];else{input_used=0;discard_input=true;}}
    }
}
static int benchmark_vm_frame(void){
    uint64_t start=now();int status=-1;
    if(lbc_begin(&bench_instance,(bench_at+1)/60.0f,1.0f/60.0f)){do{status=lbc_resume(&bench_instance,QUANTUM);}while(status==0);}
    uint32_t dt=(uint32_t)(now()-start);bench_vm+=dt;if(dt>bench_vm_max)bench_vm_max=dt;
    return status;
}
static void benchmark_native_frame(void){
    uint64_t start=now();bench_phase+=1.0f/60.0f*0.1f;bench_phase-=floorf(bench_phase);float stride=1.0f/PIXELS;
    for(unsigned k=0;k<PIXELS;k+=32)lbc_native_rainbow(bench_ref,k,k+32<PIXELS?k+32:PIXELS,bench_phase,stride);
    uint32_t dt=(uint32_t)(now()-start);bench_native+=dt;if(dt>bench_native_max)bench_native_max=dt;
}
static void benchmark_step(void){
    if(!bench_count)return;
    /* Alternate order so one implementation is not always the warmed second run. */
    int status;
    if(bench_at&1){benchmark_native_frame();status=benchmark_vm_frame();}
    else{status=benchmark_vm_frame();benchmark_native_frame();}
    if(status<0||memcmp(bench_pixels,bench_ref,BYTES))bench_errors++;
    if(++bench_at==bench_count){reply("{\"event\":\"benchmark\",\"frames\":%u,\"pixels\":%u,\"bytecode_us\":%.3f,\"native_us\":%.3f,\"ratio\":%.4f,\"bytecode_max_us\":%u,\"native_max_us\":%u,\"mismatches\":%u}",bench_count,PIXELS,(double)bench_vm/bench_count,(double)bench_native/bench_count,(double)bench_vm/bench_native,(unsigned)bench_vm_max,(unsigned)bench_native_max,bench_errors);bench_count=0;next_tick=now();}
}
void poc_run(void){
    poc_init();
    Effect *a=boot_effect(poc_slots[0]),*b=boot_effect(poc_slots[1]);
    if(a&&(!b||a->revision>=b->revision)){active=a;destroy(b);}else{active=b;destroy(a);}
    next_tick=origin=last_service=now();
    for(;;){service();output_poll();install_step();render();benchmark_step();poc_idle();}
}
