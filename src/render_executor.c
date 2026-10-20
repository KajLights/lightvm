#include "render_executor.h"
#include "config.h"
#include "platform.h"
#include <assert.h>

static bool occupied;
static RenderStats stats;

void render_executor_init(void){render_platform_init(LIGHTVM_RENDER_WORKERS);}
void render_executor_shutdown(void){assert(!occupied);render_platform_shutdown();}
unsigned render_executor_workers(void){return render_platform_workers();}
bool render_executor_is_worker(void){return render_platform_is_worker();}
RenderStats render_executor_stats(void){
    assert(!render_platform_is_worker()&&!occupied);
    RenderStats result=stats;result.worker_stack_peak=render_platform_stack_peak();return result;
}
void render_job_execute(RenderJob *job){
    uint64_t started=platform_time_us();
    job->run(job->context);
    job->elapsed_us=platform_time_us()-started;
}
void render_execute(RenderJob *jobs,size_t count){
    /* Workers never inspect coordinator state. Nested kernels execute locally
     * while both CPUs are assigned; there is no worker waiting for child work. */
    if(render_platform_is_worker()){
        for(size_t i=0;i<count;i++)render_job_execute(&jobs[i]);
        return;
    }
    for(size_t i=0;i<count;){
        if(i+1<count&&!occupied&&render_platform_workers()>1){
            occupied=true;
            render_platform_submit(&jobs[i]);
            render_job_execute(&jobs[i+1]);
            render_platform_join();
            stats.offloads++;stats.worker_us+=jobs[i].elapsed_us;
            occupied=false;i+=2;
        }else render_job_execute(&jobs[i++]);
    }
}
typedef struct { RenderRange fn;void *context;unsigned first,end; } Range;
static void range_run(void *context){Range *r=context;r->fn(r->context,r->first,r->end);}
void render_ranges(RenderRange fn,void *context,unsigned first,unsigned end,unsigned minimum){
    if(end<=first)return;
    if(render_platform_is_worker()||occupied||render_platform_workers()<2||end-first<minimum){
        fn(context,first,end);return;
    }
    unsigned middle=first+(end-first)/2;
    Range ranges[2]={{fn,context,first,middle},{fn,context,middle,end}};
    RenderJob jobs[2]={{range_run,&ranges[0],0},{range_run,&ranges[1],0}};
    render_execute(jobs,2);
}

/* Serial platform executor, also used by deterministic unit-test fixtures.
 * It executes the same kernels and ownership epochs as concurrent platforms. */
__attribute__((weak)) void render_platform_init(unsigned workers){(void)workers;}
__attribute__((weak)) void render_platform_shutdown(void){}
__attribute__((weak)) unsigned render_platform_workers(void){return 1;}
__attribute__((weak)) bool render_platform_is_worker(void){return false;}
__attribute__((weak)) void render_platform_submit(RenderJob *job){render_job_execute(job);}
__attribute__((weak)) void render_platform_join(void){}
__attribute__((weak)) unsigned render_platform_stack_peak(void){return 0;}
