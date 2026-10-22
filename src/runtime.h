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
typedef struct { uint64_t epochs,total_us,max_us,overruns,foreground_calls,max_foreground_gap_us,foreground_over_10ms,ingress_calls,max_ingress_gap_us,ingress_over_10ms; } RuntimeEpochStats;
void runtime_foreground(uint64_t now);
/* Port calls this only at an actual bounded control-ingress service point. */
void runtime_ingress(uint64_t now);
void runtime_epoch_begin(void);
void runtime_epoch_end(void);
bool runtime_epoch_available(void);
bool runtime_epoch_can_collect(void);
RuntimeEpochStats runtime_epoch_stats(void);
unsigned platform_main_stack_peak(void);
unsigned platform_cpu_hz(void);
#endif
