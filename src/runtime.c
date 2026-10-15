#include "runtime.h"
#include <stddef.h>
static void (*service)(void *);
static void *context;
static const void *owner;
static bool servicing;
static uint64_t services;
void runtime_services(void (*fn)(void *),void *ctx){service=fn;context=ctx;}
void runtime_checkpoint(void){
    if(!service||servicing)return;
    servicing=true;services++;service(context);servicing=false;
}
const void *runtime_enter(const void *next){const void *previous=owner;owner=next;return previous;}
void runtime_leave(const void *previous){owner=previous;}
bool runtime_owns(const void *value){return owner==value;}
uint64_t runtime_service_count(void){return services;}
