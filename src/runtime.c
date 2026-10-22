#include "runtime.h"
#include "render_executor.h"
#include <stddef.h>
#include <assert.h>
#include "platform.h"
#include "config.h"
static void (*service)(void *);
static void *context;
static const void *owner;
static bool servicing;
static uint64_t services;
static unsigned deferred;
static uint64_t epoch_started;
static bool epoch_active;
static RuntimeEpochStats epoch_stats;
static uint64_t last_foreground,last_ingress;
void runtime_ingress(uint64_t now){
    if(epoch_stats.ingress_calls&&now>=last_ingress){
        uint64_t gap=now-last_ingress;
        if(gap>epoch_stats.max_ingress_gap_us)epoch_stats.max_ingress_gap_us=gap;
        if(gap>10000u)epoch_stats.ingress_over_10ms++;
    }
    last_ingress=now;epoch_stats.ingress_calls++;
}
void runtime_foreground(uint64_t now){
    if(epoch_stats.foreground_calls&&now>=last_foreground){
        uint64_t gap=now-last_foreground;
        if(gap>epoch_stats.max_foreground_gap_us)epoch_stats.max_foreground_gap_us=gap;
        if(gap>10000u)epoch_stats.foreground_over_10ms++;
    }
    last_foreground=now;epoch_stats.foreground_calls++;
}
void runtime_epoch_begin(void){assert(!epoch_active);epoch_active=true;epoch_started=platform_time_us();}
bool runtime_epoch_available(void){return !epoch_active||platform_time_us()-epoch_started<LIGHTVM_RENDER_EPOCH_US;}
bool runtime_epoch_can_collect(void){return !epoch_active||platform_time_us()-epoch_started<LIGHTVM_RENDER_EPOCH_US*7u/8u;}
void runtime_epoch_end(void){
    assert(epoch_active);uint64_t elapsed=platform_time_us()-epoch_started;epoch_active=false;
    epoch_stats.epochs++;epoch_stats.total_us+=elapsed;
    if(elapsed>epoch_stats.max_us)epoch_stats.max_us=elapsed;
    if(elapsed>LIGHTVM_RENDER_EPOCH_US)epoch_stats.overruns++;
}
RuntimeEpochStats runtime_epoch_stats(void){return epoch_stats;}
__attribute__((weak)) unsigned platform_main_stack_peak(void){return 0;}
__attribute__((weak)) unsigned platform_cpu_hz(void){return 0;}
void runtime_defer_begin(void){deferred++;}
void runtime_defer_end(void){assert(deferred);deferred--;}
void runtime_services(void (*fn)(void *),void *ctx){service=fn;context=ctx;}
void runtime_checkpoint(void){
    if(render_executor_is_worker())return;
    if(!service||servicing||deferred)return;
    servicing=true;services++;service(context);servicing=false;
}
const void *runtime_enter(const void *next){const void *previous=owner;owner=next;return previous;}
void runtime_leave(const void *previous){owner=previous;}
bool runtime_owns(const void *value){return owner==value;}
uint64_t runtime_service_count(void){return services;}
