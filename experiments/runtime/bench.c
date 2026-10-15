#include "app.h"
#include "json.h"
#include "vm_allocator.h"
#include <stdio.h>
#include <string.h>

uint32_t native_bench_integer(uint32_t);
uint32_t native_bench_float(uint32_t);
uint32_t native_bench_memory(uint32_t);
uint32_t native_bench_import(uint32_t);
static uint32_t pixels;
uint32_t bench_count(void) { return pixels; }

// Diagnostic only: isolate initialization from steady disconnected steps.
// This does not change the production service's 2 ms admission/execution policy.
static bool service_probe(App *a,uint16_t id) {
#if LIGHTVM_MQTT_EXPERIMENT
    Service *s=&a->service;
    const char *error=NULL;
    if (!s->loaded || s->running || a->vm.playing) {
        send_status(a->send,a->transport,id,STATUS_RUN,0,"load service and stop service/playback before probing");
        return true;
    }
    uint32_t init_us[3]={0},step_us[21]={0};
    for (unsigned i=0;!error && i<3;++i) {
        s->io_budget=2048;
        uint64_t start=platform_time_us();vm_service_execution(start+RUN_TIMEOUT_US);
        error=m3_CallV(s->init,2130706433u,18883u);
        init_us[i]=(uint32_t)(platform_time_us()-start);vm_service_execution(0);
    }
    for (unsigned i=0;!error && i<21;++i) {
        s->io_budget=2048;
        uint64_t start=platform_time_us();vm_service_execution(start+RUN_TIMEOUT_US);
        error=m3_CallV(s->step);
        step_us[i]=(uint32_t)(platform_time_us()-start);vm_service_execution(0);
    }
    platform_tcp_close(&s->tcp);s->handle=0;s->pending=false;
    if (error) {send_status(a->send,a->transport,id,STATUS_RUN,0,error);return true;}
    char b[256];
    int length=snprintf(b,sizeof(b),"{\"diagnostic_deadline_us\":%u,\"production_deadline_us\":%u,\"init_us\":[%lu,%lu,%lu],\"disconnected_step_us\":[",
        RUN_TIMEOUT_US,SERVICE_DEADLINE_US,(unsigned long)init_us[0],(unsigned long)init_us[1],(unsigned long)init_us[2]);
    a->send(a->transport,RESP_DATA,id,(const uint8_t *)b,(size_t)length);
    for(unsigned i=0;i<21;++i) {
        length=snprintf(b,sizeof(b),"%s%lu",i?",":"",(unsigned long)step_us[i]);
        a->send(a->transport,RESP_DATA,id,(const uint8_t *)b,(size_t)length);
    }
    a->send(a->transport,RESP_DATA,id,(const uint8_t *)"]}",2);
    send_status(a->send,a->transport,id,STATUS_OK,0,"diagnostic probe only; unload service afterward");
#else
    send_status(a->send,a->transport,id,STATUS_RUN,0,"MQTT experiment required");
#endif
    return true;
}

bool runtime_benchmark(App *a,uint16_t id,const uint8_t *payload,size_t size) {
    Json j; char op[40],name[40]; uint32_t n,samples;
    if (!json_parse(&j,payload,size) || !json_string(&j,json_get(&j,0,"op"),op,sizeof(op))) return false;
    if (!strcmp(op,"runtime_service_probe")) {
        static const char *const probe_fields[]={"op",NULL};
        if (!json_fields(&j,0,probe_fields)) send_status(a->send,a->transport,id,STATUS_PROTOCOL,0,"invalid probe fields");
        else return service_probe(a,id);
        return true;
    }
    if (strcmp(op,"runtime_bench")) return false;
    static const char *const fields[]={"op","export","iterations","samples",NULL};
    const char *error=NULL;
    uint32_t (*native)(uint32_t)=NULL;
    if (!json_fields(&j,0,fields) || !json_string(&j,json_get(&j,0,"export"),name,sizeof(name)) ||
        !json_u32(&j,json_get(&j,0,"iterations"),&n) || !n || n>10000 ||
        !json_u32(&j,json_get(&j,0,"samples"),&samples) || !samples || samples>21) error="invalid benchmark request";
    if (!error) {
        if (!strcmp(name,"bench_integer")) native=native_bench_integer;
        if (!strcmp(name,"bench_float")) native=native_bench_float;
        if (!strcmp(name,"bench_memory")) native=native_bench_memory;
        if (!strcmp(name,"bench_import")) native=native_bench_import;
        if (!native || !a->vm.ready || a->vm.playing) error="load the kernel module and stop playback first";
    }
#if LIGHTVM_MQTT_EXPERIMENT
    if (!error && a->service.running) error="stop the service before microbenchmarks";
#endif
    IM3Function fn=NULL;
    if (!error) {
        vm_service_execution(platform_time_us()+RUN_TIMEOUT_US);
        error=m3_FindFunction(&fn,a->vm.runtime,name);
        vm_service_execution(0);
        if (!error && (m3_GetArgCount(fn)!=1 || m3_GetArgType(fn,0)!=c_m3Type_i32 ||
                       m3_GetRetCount(fn)!=1 || m3_GetRetType(fn,0)!=c_m3Type_i32)) error="invalid kernel signature";
    }
    uint32_t guest_us[21],native_us[21],result=0;
    size_t before=vm_heap_used(); vm_heap_window_reset();
    pixels=a->vm.target?a->vm.target->count:0;
    for (uint32_t i=0;!error && i<samples+3;++i) {
        uint64_t start=platform_time_us();
        uint32_t expected=native(n);
        uint32_t nt=(uint32_t)(platform_time_us()-start);
        vm_service_execution(platform_time_us()+RUN_TIMEOUT_US);
        start=platform_time_us();
        error=m3_CallV(fn,n);
        if (!error) error=m3_GetResultsV(fn,&result);
        uint32_t gt=(uint32_t)(platform_time_us()-start);
        vm_service_execution(0);
        if (!error && result!=expected) error="guest/native result mismatch";
        if (i>=3) { guest_us[i-3]=gt; native_us[i-3]=nt; }
    }
    if (error) { send_status(a->send,a->transport,id,STATUS_RUN,0,error); return true; }
    char b[256];
    int length=snprintf(b,sizeof(b),"{\"iterations\":%lu,\"result\":%lu,\"heap_before\":%lu,\"heap_after\":%lu,\"window_peak\":%lu,\"native_us\":[",
        (unsigned long)n,(unsigned long)result,(unsigned long)before,(unsigned long)vm_heap_used(),(unsigned long)vm_heap_window_peak());
    a->send(a->transport,RESP_DATA,id,(const uint8_t *)b,(size_t)length);
    for (unsigned which=0;which<2;++which) {
        for (uint32_t i=0;i<samples;++i) {
            length=snprintf(b,sizeof(b),"%s%lu",i?",":"",(unsigned long)(which?guest_us[i]:native_us[i]));
            a->send(a->transport,RESP_DATA,id,(const uint8_t *)b,(size_t)length);
        }
        const char *end=which?"]}":"],\"guest_us\":[";
        a->send(a->transport,RESP_DATA,id,(const uint8_t *)end,strlen(end));
    }
    send_status(a->send,a->transport,id,STATUS_OK,0,"device-clock samples; native reference is unsandboxed");
    return true;
}
