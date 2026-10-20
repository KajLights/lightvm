#include "render_executor.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "pico/util/queue.h"
#include <string.h>

static queue_t jobs,completions;
static bool started;
extern uint8_t __StackOneBottom[],__StackOneTop[];
static unsigned stack_peak;
static void measure_stack(void){
    /* RP2040's optional MPU guard denies reads of the first aligned 32 bytes. */
    const uint8_t *p=(const uint8_t *)(((uintptr_t)__StackOneBottom+31u)&~(uintptr_t)31u)+32;
    while(p<__StackOneTop&&*p==0xa5)p++;
    unsigned used=(unsigned)(__StackOneTop-p);
    if(used>stack_peak)stack_peak=used;
}

static void worker_main(void){
    if(!flash_safe_execute_core_init())panic("render worker flash initialization failed");
    measure_stack();
    bool ready=true;queue_add_blocking(&completions,&ready);
    for(;;){
        RenderJob *job;
        queue_remove_blocking(&jobs,&job);
        render_job_execute(job);
        queue_add_blocking(&completions,&ready);
    }
}
void render_platform_init(unsigned workers){
    if(started||workers<2)return;
    if(!queue_init(&jobs,sizeof(RenderJob *),1)||!queue_init(&completions,sizeof(bool),1))
        panic("render worker queues exceed native memory");
    stack_peak=0;memset(__StackOneBottom,0xa5,(size_t)(__StackOneTop-__StackOneBottom));
    multicore_launch_core1(worker_main);
    bool ready;queue_remove_blocking(&completions,&ready);started=true;
}
void render_platform_shutdown(void){
    if(!started)return;
    /* Called only after the coordinator has joined every assignment. */
    multicore_reset_core1();queue_free(&jobs);queue_free(&completions);started=false;
}
unsigned render_platform_workers(void){return started?2:1;}
bool render_platform_is_worker(void){return get_core_num()!=0;}
void render_platform_submit(RenderJob *job){queue_add_blocking(&jobs,&job);}
void render_platform_join(void){bool done;queue_remove_blocking(&completions,&done);}
static void stack_probe(void *context){(void)context;measure_stack();}
/* Scan on request, not in every small pixel job. The queue publishes the peak. */
unsigned render_platform_stack_peak(void){
    if(started){
        RenderJob probe={stack_probe,NULL,0};
        render_platform_submit(&probe);render_platform_join();
    }
    return stack_peak;
}
