/* Exercise actual threads, nested kernels, and complete VM state on 1/2 CPUs. */
#include "animation.h"
#include "render_executor.h"
#include "vm_allocator.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "portable_fixtures.h"
#pragma GCC diagnostic pop

#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct { atomic_uint *arrived;unsigned values[257];bool concurrent,worker; } Nested;
static void fill(void *context,unsigned first,unsigned end){
    unsigned *values=context;for(unsigned i=first;i<end;i++)values[i]=i*i+17;
}
static void nested(void *context){
    Nested *n=context;n->worker=render_executor_is_worker();
    if(n->concurrent){
        atomic_fetch_add(n->arrived,1);
        uint64_t deadline=platform_time_us()+1000000;
        while(atomic_load(n->arrived)<2)CHECK(platform_time_us()<deadline);
    }
    render_ranges(fill,n->values,0,257,64);
}
static uint32_t exercise(unsigned workers){
    render_platform_init(workers);CHECK(render_executor_workers()==workers);
    atomic_uint arrived=0;
    Nested n[2]={{.arrived=&arrived,.concurrent=workers==2},{.arrived=&arrived,.concurrent=workers==2}};
    RenderJob nested_jobs[2]={{nested,&n[0],0},{nested,&n[1],0}};
    render_execute(nested_jobs,2);
    CHECK(n[0].worker==(workers==2)&&!n[1].worker);
    for(unsigned k=0;k<2;k++)for(unsigned i=0;i<257;i++)CHECK(n[k].values[i]==i*i+17);

    PixelBuffer target={.count=131};
    VmInstance vms[3];uint32_t hash=0;
    for(unsigned i=0;i<3;i++){
        vm_init(&vms[i],&target,NULL,NULL);
        vms[i].memory=i==1?pixel_map_lbc:counter_lbc;
        vms[i].expected=i==1?sizeof(pixel_map_lbc):sizeof(counter_lbc);
        CHECK(!vm_load(&vms[i]));vms[i].ready=true;
        vms[i].portable->deterministic_random=true;vms[i].portable->default_seed=123;
        CHECK(!control_prepare(&vms[i]));
    }
    for(unsigned frame=0;frame<20;frame++){
        bool done[3]={false};
        for(unsigned i=0;i<3;i++)CHECK(!animation_begin(&vms[i],17000));
        unsigned rounds=0;
        while(!done[0]||!done[1]||!done[2]){
            CHECK(++rounds<1000);
            AnimationSlice slices[3];RenderJob jobs[3];unsigned count=0,indices[3];
            for(unsigned i=0;i<3;i++)if(!done[i]){
                indices[count]=i;slices[count]=(AnimationSlice){&vms[i],7,0};
                jobs[count]=(RenderJob){animation_execute,&slices[count],0};count++;
            }
            render_execute(jobs,count);
            for(unsigned i=0;i<count;i++)CHECK(!animation_commit(&slices[i],jobs[i].elapsed_us,&done[indices[i]]));
        }
        for(unsigned i=0;i<3;i++){
            CHECK(vms[i].portable->render_count==frame+2);
            CHECK(vms[i].control.program_us==(frame+1)*17000);
            CHECK(!vms[i].control.faulted);
            hash=hash*31+crc32_bytes(control_frame(&vms[i]),control_frame_bytes(&vms[i]));
            hash=hash*31+crc32_bytes(vms[i].portable->values,vms[i].portable->register_bytes);
        }
    }
    for(unsigned i=0;i<3;i++)vm_destroy(&vms[i]);
    CHECK(vm_heap_used()==0);render_executor_shutdown();return hash;
}
int main(void){
    uint32_t serial=exercise(1);
    for(unsigned i=0;i<5;i++)CHECK(exercise(2)==serial);
    CHECK(exercise(1)==serial);
    CHECK(render_executor_stats().offloads>100);
    return 0;
}
