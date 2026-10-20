#include "runtime.h"
#include "render_executor.h"
#include <stddef.h>
#include <assert.h>
static void (*service)(void *);
static void *context;
static const void *owner;
static bool servicing;
static uint64_t services;
static unsigned deferred;
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
