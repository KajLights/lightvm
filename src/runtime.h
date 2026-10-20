#ifndef LIGHTVM_RUNTIME_H
#define LIGHTVM_RUNTIME_H
#include <stdbool.h>
#include <stdint.h>
/* One serialized management worker borrows the native stack. Checkpoints run
 * other instances and native ingress; they never recursively run the owner. */
void runtime_services(void (*service)(void *),void *context);
void runtime_checkpoint(void);
const void *runtime_enter(const void *owner);
void runtime_leave(const void *previous);
bool runtime_owns(const void *owner);
uint64_t runtime_service_count(void);
void runtime_defer_begin(void);
void runtime_defer_end(void);
#endif
