/* Matched C3 experiment: single-worker component scheduling, real async RMT,
 * USB requests/uploads, identical storage and replacement policy for all VMs. */
#include "backend.h"
#include "ws2812_encoder.h"
#include "driver/rmt_tx.h"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>

enum { PIXELS=200, BYTES=600, GPIO=3, PERIOD=16667, MAX_PACKAGE=32768,
       MAX_SAMPLES=1024, MAX_EVENTS=16, PHASES=3 };
typedef struct Package { uint8_t *bytes; size_t size; CrModule *module; unsigned refs, revision; } Package;
typedef struct Effect { Package *package; CrInstance *instance; uint32_t frame, seed, cpu_us; bool ready, rendering; } Effect;
typedef struct { uint32_t frame, seed, hash, cpu_us, submit_us, complete_us; } Sample;
typedef struct { uint32_t begin_us, load_us, prepare_us, activate_us, end_us, retired_us; unsigned revision, pending, received; } Event;
typedef struct { uint32_t calls, maximum_us, over_2ms, over_10ms; uint64_t sum_us; } Gaps;
static Effect *active, *second, *candidate, *retired, *output_owner;
static uint8_t *staging; static size_t staged_size, staged_received;
static uint32_t staged_crc; static unsigned staged_revision;
static bool load_requested, initial_loading, running, measuring;
static unsigned phase, sample_count, event_count, errors, tx_drops;
static Sample samples[MAX_SAMPLES]; static Event events[MAX_EVENTS]; static Event *event;
static Gaps service_gaps[PHASES];
static uint32_t phase_heap_min[PHASES];
static uint32_t heap_min, heap_baseline, heap_total, heap_largest_min, stack_min;
static uint64_t memory_probe_us; static uint32_t memory_probe_max_us, memory_probe_calls;
static uint32_t frames_b, cpu_b, max_step_us, output_total, sample_overflow, hash_failures;
static uint64_t last_service, next_frame, measurement_start;
static rmt_channel_handle_t channel; static rmt_encoder_handle_t encoder;
static volatile bool output_done; static volatile uint32_t output_done_us;
static uint32_t output_hash; static int output_sample=-1;
static char input[1024]; static unsigned input_used;
static uint64_t now(void) { return (uint64_t)esp_timer_get_time(); }
static uint32_t relative_time(void) { return (uint32_t)(now()-measurement_start); }
static uint32_t hash(const uint8_t *p) { uint32_t h=2166136261u; for(unsigned i=0;i<BYTES;i++){h^=p[i];h*=16777619u;} return h; }
static uint32_t oracle(uint32_t seed,uint32_t frame) {
    uint32_t h=2166136261u,x=seed^(frame*0x9e3779b9u);
    for(unsigned i=0;i<PIXELS;i++){x^=x<<13;x^=x>>17;x^=x<<5;x+=i+0x6d2b79f5u;
        for(unsigned c=0;c<3;c++){h^=(x>>(8*c))&255u;h*=16777619u;}}
    return h;
}
static void reply(const char *format, ...) {
    char line[768]; va_list args; va_start(args,format);
    int n=vsnprintf(line,sizeof(line)-2,format,args); va_end(args);
    if(n<0 || n>(int)sizeof(line)-2){tx_drops++;return;}
    line[n++]='\n';
    int written=usb_serial_jtag_write_bytes(line,n,0);
    if(written!=n)tx_drops++;
}
static void memory_sample(bool detail) {
    uint64_t started=now();
    uint32_t free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if(free<heap_min)heap_min=free;
    if(measuring && free<phase_heap_min[phase])phase_heap_min[phase]=free;
    if(detail){uint32_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);if(largest<heap_largest_min)heap_largest_min=largest;}
    if(measuring){uint32_t elapsed=(uint32_t)(now()-started);memory_probe_us+=elapsed;memory_probe_calls++;if(elapsed>memory_probe_max_us)memory_probe_max_us=elapsed;}
}
static void package_release(Package *p) {
    if(p && --p->refs==0){cr_unload(p->module);free(p->bytes);free(p);}
}
static void destroy(Effect *e) { if(e){cr_destroy(e->instance);package_release(e->package);free(e);} }
static Effect *create(Package *p,uint32_t seed) {
    Effect *e=calloc(1,sizeof(*e)); if(!e)return NULL;
    e->instance=cr_create(p->module,seed,PIXELS);
    if(!e->instance){free(e);return NULL;}
    p->refs++; e->package=p; e->seed=seed^(p->revision==2?0x13579bdfu:0); return e;
}
static bool IRAM_ATTR completed(rmt_channel_handle_t c,const rmt_tx_done_event_data_t *d,void *user) {
    (void)c;(void)d;(void)user; output_done_us=(uint32_t)esp_timer_get_time();output_done=true;return false;
}
static void poll_output(void) {
    if(!output_owner || !output_done)return;
    if(hash(cr_pixels(output_owner->instance))!=output_hash){errors++;hash_failures++;}
    cr_seal(output_owner->instance,false);
    if(output_sample>=0)samples[output_sample].complete_us=output_done_us-(uint32_t)measurement_start;
    output_total++; output_owner=NULL;output_sample=-1;
    if(retired){destroy(retired);retired=NULL;if(event)event->retired_us=relative_time();}
}
static void submit(Effect *e,uint32_t rgb_hash) {
    if(output_owner){errors++;return;}
    uint8_t *p=cr_pixels(e->instance);
    /* Native transform in place: RGB -> GRB and brightness 16/255. The exact
     * component backing buffer remains owned until the RMT callback completes. */
    for(unsigned i=0;i<PIXELS;i++){uint8_t r=p[3*i],g=p[3*i+1];p[3*i]=(g*16u+127u)/255u;p[3*i+1]=(r*16u+127u)/255u;p[3*i+2]=(p[3*i+2]*16u+127u)/255u;}
    output_hash=hash(p);cr_seal(e->instance,true);output_owner=e;output_done=false;
    if(measuring){
        if(sample_count<MAX_SAMPLES){output_sample=(int)sample_count;samples[sample_count++]=(Sample){e->frame,e->seed,rgb_hash,e->cpu_us,relative_time(),0};}
        else sample_overflow++;
    }
    rmt_transmit_config_t tx={.loop_count=0};
    if(rmt_transmit(channel,encoder,p,BYTES,&tx)!=ESP_OK){errors++;cr_seal(e->instance,false);output_owner=NULL;}
}
static void advance(Effect *e,bool publish) {
    if(!e || !e->ready || !e->rendering || output_owner==e)return;
    uint64_t start=now();int state=cr_step(e->instance);uint32_t elapsed=(uint32_t)(now()-start);
    e->cpu_us+=elapsed;if(elapsed>max_step_us)max_step_us=elapsed;
    if(state<0){errors++;e->rendering=false;running=false;reply("{\"event\":\"fault\",\"where\":\"step\"}");}
    else if(state==1){
        e->rendering=false;uint32_t h=hash(cr_pixels(e->instance));
        if(h!=oracle(e->seed,e->frame)){errors++;hash_failures++;running=false;}
        if(publish)submit(e,h);else{frames_b++;cpu_b+=e->cpu_us;}
    }
}
static void begin_frame(Effect *e) {
    if(!e || !e->ready || e->rendering || output_owner==e)return;
    uint64_t start=now();int result=cr_begin(e->instance,e->frame+1);
    e->cpu_us=(uint32_t)(now()-start);
    if(result<0){errors++;running=false;}else{e->frame++;e->rendering=true;}
}
static void initialize(void) {
    Effect *e=initial_loading && active && !active->ready?active:
              initial_loading && second && !second->ready?second:candidate;
    if(!e)return;
    uint64_t start=now();int result=e->ready?1:cr_prepare(e->instance);
    if(event)event->prepare_us+=(uint32_t)(now()-start);
    if(result<0){errors++;if(e==candidate){destroy(candidate);candidate=NULL;}else running=false;reply("{\"event\":\"fault\",\"where\":\"prepare\"}");return;}
    if(result==0)return;
    e->ready=true;
    if(initial_loading){
        if(active->ready && second->ready){initial_loading=false;running=true;next_frame=now();reply("{\"event\":\"ready\"}");}
    } else if(candidate && !active->rendering && output_owner==active && !output_done){
        start=now();Effect *old=active;active=candidate;candidate=NULL;active->frame=old->frame;
        if(output_owner==old)retired=old;else destroy(old);
        if(event){event->activate_us=(uint32_t)(now()-start);event->end_us=relative_time();event->pending=retired!=NULL;}
        reply("{\"event\":\"activated\",\"revision\":%u,\"retained\":%s}",active->package->revision,retired?"true":"false");
    }
}
static void load(void) {
    /* Trigger replacement during an actual pending transfer, not a simulated
     * tick. Poll completion after loading to measure the whole load stall. */
    if(!load_requested || (active && (!output_owner || output_done)))return;
    load_requested=false;char error[160]={0};
    if(esp_rom_crc32_le(0,staging,staged_size)!=staged_crc){reply("{\"event\":\"rejected\",\"reason\":\"crc\"}");free(staging);staging=NULL;return;}
    if(event_count<MAX_EVENTS){event=&events[event_count++];*event=(Event){.begin_us=relative_time(),.revision=staged_revision,.received=staged_size};}
    uint64_t start=now();CrModule *module=cr_load(staging,staged_size,error,sizeof(error));
    if(event)event->load_us=(uint32_t)(now()-start);
    if(!module){for(char *q=error;*q;q++)if(*q=='"'||*q=='\\'||(unsigned char)*q<32)*q=' ';reply("{\"event\":\"rejected\",\"reason\":\"loader\",\"detail\":\"%s\"}",error);free(staging);staging=NULL;return;}
    Package *p=calloc(1,sizeof(*p));
    if(!p){cr_unload(module);free(staging);staging=NULL;errors++;return;}
    *p=(Package){staging,staged_size,module,1,staged_revision};staging=NULL;
    if(!active){
        active=create(p,0x12345678u);second=create(p,0x87654321u);
        if(!active || !second){destroy(active);destroy(second);active=second=NULL;reply("{\"event\":\"rejected\",\"reason\":\"initial_instances\"}");}
        else initial_loading=true;
    } else {candidate=create(p,0x12345678u);if(!candidate)reply("{\"event\":\"rejected\",\"reason\":\"candidate_instance\"}");}
    package_release(p);memory_sample(true);
}
static void stats(void) {
    if(output_owner){reply("{\"event\":\"error\",\"reason\":\"output_busy\"}");return;}
    measuring=false;memory_sample(true);stack_min=uxTaskGetStackHighWaterMark(NULL);
    reply("{\"event\":\"stats\",\"backend\":\"%s\",\"samples\":%u,\"overflow\":%"PRIu32",\"errors\":%u,\"hash_failures\":%"PRIu32",\"tx_drops\":%u,\"heap_total\":%"PRIu32",\"heap_baseline_free\":%"PRIu32",\"heap_min_free\":%"PRIu32",\"heap_sdk_min_free\":%u,\"largest_min\":%"PRIu32",\"worker_stack_free_min\":%"PRIu32",\"max_step_us\":%"PRIu32",\"b_frames\":%"PRIu32",\"b_call_elapsed_us\":%"PRIu32",\"outputs\":%"PRIu32",\"events\":%u,\"memory_probe_us\":%"PRIu64",\"memory_probe_max_us\":%"PRIu32",\"memory_probe_calls\":%"PRIu32"}",cr_backend(),sample_count,sample_overflow,errors,hash_failures,tx_drops,heap_total,heap_baseline,heap_min,(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),heap_largest_min,stack_min,max_step_us,frames_b,cpu_b,output_total,event_count,memory_probe_us,memory_probe_max_us,memory_probe_calls);
}
static int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static void command(char *line) {
    unsigned a,b,c;char extra;
    if(!strcmp(line,"hello")){reply("{\"event\":\"hello\",\"backend\":\"%s\",\"pixels\":200,\"gpio\":3,\"period_us\":16667,\"cpu_mhz\":160,\"memprot\":false}",cr_backend());}
    else if(sscanf(line,"ping %u %c",&a,&extra)==1)reply("{\"event\":\"pong\",\"id\":%u,\"device_us\":%"PRIu64"}",a,now());
    else if(sscanf(line,"begin %u %u %u %c",&a,&b,&c,&extra)==3){
        if(staging||candidate||retired||load_requested||!a||a>MAX_PACKAGE||(c!=1&&c!=2)){reply("{\"event\":\"error\",\"reason\":\"admission\"}");return;}
        staging=malloc(a);if(!staging){reply("{\"event\":\"error\",\"reason\":\"oom\"}");return;}
        staged_size=a;staged_crc=b;staged_revision=c;staged_received=0;memory_sample(true);reply("{\"event\":\"ack\",\"offset\":0}");
    } else if(!strncmp(line,"data ",5)){
        char *end;unsigned long offset=strtoul(line+5,&end,10);
        if(*end!=' '||!staging||offset!=staged_received){reply("{\"event\":\"error\",\"reason\":\"offset\"}");return;}end++;
        size_t n=strlen(end);if(n%2||n/2>192||n/2>staged_size-staged_received){reply("{\"event\":\"error\",\"reason\":\"chunk\"}");return;}
        for(size_t i=0;i<n;i++)if(hex(end[i])<0){reply("{\"event\":\"error\",\"reason\":\"hex\"}");return;}
        for(size_t i=0;i<n;i+=2)staging[staged_received++]=(uint8_t)(hex(end[i])*16+hex(end[i+1]));
        reply("{\"event\":\"ack\",\"offset\":%u}",(unsigned)staged_received);
    } else if(!strcmp(line,"load")){
        if(!staging||staged_received!=staged_size){reply("{\"event\":\"error\",\"reason\":\"incomplete\"}");return;}
        load_requested=true;reply("{\"event\":\"queued\"}");
    } else if(!strcmp(line,"measure")){
        if(output_owner){reply("{\"event\":\"error\",\"reason\":\"output_busy\"}");return;}
        sample_count=event_count=0;event=NULL;memset(service_gaps,0,sizeof(service_gaps));
        for(unsigned i=0;i<PHASES;i++)phase_heap_min[i]=UINT32_MAX;
        memory_probe_us=0;memory_probe_max_us=memory_probe_calls=0;
        measurement_start=now();measuring=true;phase=0;last_service=now();reply("{\"event\":\"measuring\"}");
    } else if(sscanf(line,"phase %u %c",&a,&extra)==1 && a<PHASES){phase=a;reply("{\"event\":\"phase\",\"value\":%u,\"at_us\":%"PRIu32"}",a,relative_time());}
    else if(!strcmp(line,"stats"))stats();
    else if(sscanf(line,"sample %u %c",&a,&extra)==1 && a<sample_count){Sample *s=&samples[a];reply("{\"event\":\"sample\",\"index\":%u,\"frame\":%"PRIu32",\"seed\":%"PRIu32",\"hash\":%"PRIu32",\"call_elapsed_us\":%"PRIu32",\"submit_us\":%"PRIu32",\"complete_us\":%"PRIu32"}",a,s->frame,s->seed,s->hash,s->cpu_us,s->submit_us,s->complete_us);}
    else if(sscanf(line,"event %u %c",&a,&extra)==1 && a<event_count){Event *e=&events[a];reply("{\"event\":\"replacement\",\"index\":%u,\"revision\":%u,\"begin_us\":%"PRIu32",\"load_us\":%"PRIu32",\"prepare_us\":%"PRIu32",\"activate_us\":%"PRIu32",\"end_us\":%"PRIu32",\"retired_us\":%"PRIu32",\"pending\":%u,\"bytes\":%u}",a,e->revision,e->begin_us,e->load_us,e->prepare_us,e->activate_us,e->end_us,e->retired_us,e->pending,e->received);}
    else if(sscanf(line,"gap %u %c",&a,&extra)==1 && a<PHASES){Gaps *g=&service_gaps[a];reply("{\"event\":\"gap\",\"phase\":%u,\"calls\":%"PRIu32",\"max_us\":%"PRIu32",\"sum_us\":%"PRIu64",\"over_2ms\":%"PRIu32",\"over_10ms\":%"PRIu32",\"heap_min_free\":%"PRIu32"}",a,g->calls,g->maximum_us,g->sum_us,g->over_2ms,g->over_10ms,phase_heap_min[a]);}
    else if(!strcmp(line,"restart")){esp_restart();}
    else reply("{\"event\":\"error\",\"reason\":\"command\"}");
}
static void service(void) {
    uint64_t t=now();
    if(measuring){uint32_t gap=(uint32_t)(t-last_service);Gaps *g=&service_gaps[phase];g->calls++;g->sum_us+=gap;if(gap>g->maximum_us)g->maximum_us=gap;if(gap>2000)g->over_2ms++;if(gap>10000)g->over_10ms++;}
    last_service=t;uint8_t bytes[128];int n=usb_serial_jtag_read_bytes(bytes,sizeof(bytes),0);
    for(int i=0;i<n;i++){
        if(bytes[i]=='\n'){input[input_used]=0;command(input);input_used=0;}
        else if(input_used<sizeof(input)-1)input[input_used++]=(char)bytes[i];
        else{input_used=0;errors++;}
    }
}
static void *worker(void *argument) {
    (void)argument;
    /* WAMR's ESP port expects a registered POSIX thread. All three images use
     * this same worker; let the short-lived IDF main task retire before setup. */
    vTaskDelay(3);
    heap_total=heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    heap_min=heap_largest_min=stack_min=UINT32_MAX;
    usb_serial_jtag_driver_config_t usb={.tx_buffer_size=4096,.rx_buffer_size=4096};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    if(!cr_runtime_init())abort();
    rmt_tx_channel_config_t tx={.gpio_num=GPIO,.clk_src=RMT_CLK_SRC_DEFAULT,.resolution_hz=20000000,.mem_block_symbols=SOC_RMT_MEM_WORDS_PER_CHANNEL,.trans_queue_depth=1,.flags.with_dma=false};
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx,&channel));
    ESP_ERROR_CHECK(lightvm_new_ws2812_encoder(&encoder));
    rmt_tx_event_callbacks_t callbacks={.on_trans_done=completed};
    ESP_ERROR_CHECK(rmt_tx_register_event_callbacks(channel,&callbacks,NULL));
    ESP_ERROR_CHECK(rmt_enable(channel));
    heap_baseline=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);memory_sample(true);
    measurement_start=now();last_service=now();uint64_t yielded=now(),memory_at=now();
    while(true){
        service();load();poll_output();initialize();
        uint64_t t=now();
        if(running && t>=next_frame){begin_frame(active);begin_frame(second);next_frame+=PERIOD;if(t>next_frame+PERIOD)next_frame=t+PERIOD;}
        if(running){advance(active,true);advance(second,false);}
        if(t-memory_at>=1000){memory_sample(false);memory_at=t;}
        if(t-yielded>=2000){vTaskDelay(1);yielded=now();}
    }
    return NULL;
}
void app_main(void) {
    pthread_attr_t attr;pthread_t thread;
    ESP_ERROR_CHECK(pthread_attr_init(&attr));
    ESP_ERROR_CHECK(pthread_attr_setstacksize(&attr,12288));
    ESP_ERROR_CHECK(pthread_create(&thread,&attr,worker,NULL));
    ESP_ERROR_CHECK(pthread_detach(thread));
    ESP_ERROR_CHECK(pthread_attr_destroy(&attr));
}
