#ifndef LIGHTVM_RENDER_EXECUTOR_H
#define LIGHTVM_RENDER_EXECUTOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A bounded execution epoch. The coordinator captures inputs before dispatch
 * and retains every referenced object until all jobs return. Jobs must not
 * allocate, invoke application services or mutate another job's state. */
typedef struct {
    void (*run)(void *);
    void *context;
    uint64_t elapsed_us;
} RenderJob;
typedef struct { uint64_t offloads, worker_us;unsigned worker_stack_peak; } RenderStats;
typedef void (*RenderRange)(void *context,unsigned first,unsigned end);

void render_executor_init(void);
void render_executor_shutdown(void);
unsigned render_executor_workers(void);
bool render_executor_is_worker(void);
void render_execute(RenderJob *jobs,size_t count);
void render_ranges(RenderRange fn,void *context,unsigned first,unsigned end,unsigned minimum);
RenderStats render_executor_stats(void);
void render_job_execute(RenderJob *);

/* Platform implementation. Submission and join are called only by core 0.
 * Join establishes visibility of all writes by the worker. */
void render_platform_init(unsigned workers);
void render_platform_shutdown(void);
unsigned render_platform_workers(void);
bool render_platform_is_worker(void);
void render_platform_submit(RenderJob *);
void render_platform_join(void);
unsigned render_platform_stack_peak(void);
#endif
