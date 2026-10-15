#define _POSIX_C_SOURCE 200809L
#include "wasm_export.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#define N 300
#define BYTES (N * 3)
#define QUANTUM 32
#define SEED UINT32_C(305419896)
#define REQUIRE(c) do { if (!(c)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while(0)

/* Requested WAMR heap bytes, including linear memory, metadata and exec_env.
 * This is NOT total RSS: mapped executable code and native stacks are separate. */
typedef union { long double align; void *pointer_align; size_t size; } Header;
static size_t live_bytes, peak_bytes, allocations, peak_allocations, linear_allocated, peak_linear_allocated, allocation_cap = 8*1024*1024;
static void *tracked_malloc(mem_alloc_usage_t usage, unsigned size) {
    if ((size_t)size > allocation_cap - live_bytes) return NULL;
    Header *h = malloc(sizeof(*h) + size); if (!h) return NULL;
    h->size = size; live_bytes += size; allocations++;
    if (allocations>peak_allocations) peak_allocations=allocations;
    if (usage==Alloc_For_LinearMemory) { linear_allocated+=size; if(linear_allocated>peak_linear_allocated) peak_linear_allocated=linear_allocated; }
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    return h + 1;
}
static void tracked_free(mem_alloc_usage_t usage, void *p) {
    if (!p) return;
    Header *h = (Header *)p - 1;
    if (usage==Alloc_For_LinearMemory) linear_allocated-=h->size;
    live_bytes -= h->size; allocations--; free(h);
}
static void *tracked_realloc(mem_alloc_usage_t usage, bool full_size_mapped, void *p, unsigned size) {
    (void)full_size_mapped;
    if (!p) return tracked_malloc(usage,size);
    Header *h = (Header *)p - 1;
    if ((size_t)size > allocation_cap - (live_bytes - h->size)) return NULL;
    size_t old = h->size;
    h = realloc(h, sizeof(*h) + size); if (!h) return NULL;
    h->size = size; live_bytes = live_bytes - old + size;
    if (usage==Alloc_For_LinearMemory) { linear_allocated=linear_allocated-old+size; if(linear_allocated>peak_linear_allocated) peak_linear_allocated=linear_allocated; }
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    return h + 1;
}
static uint64_t now_ns(void) {
#ifdef __APPLE__
    static mach_timebase_info_data_t scale;
    if(!scale.denom) mach_timebase_info(&scale);
    uint64_t ticks=mach_absolute_time();
    return ticks / scale.denom * scale.numer + (ticks % scale.denom) * scale.numer / scale.denom;
#else
    struct timespec t;
#ifdef CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
#else
    clock_gettime(CLOCK_MONOTONIC, &t);
#endif
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + t.tv_nsec;
#endif
}
static uint64_t max_dispatch_ns, max_service_gap_ns, last_service_ns, load_ns, instantiate_ns, init_ns, activate_ns;
static unsigned calls, service_count, prep_service_count, max_instances, instances;
static uint32_t next_view_generation=1;
static int sockets[2];
static void service(void) {
    uint64_t now = now_ns();
    if (last_service_ns && now-last_service_ns > max_service_gap_ns) max_service_gap_ns = now-last_service_ns;
    last_service_ns = now;
    uint8_t byte;
    ssize_t n = recv(sockets[1], &byte, 1, 0);
    REQUIRE(n == 1 || (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)));
    if (n == 1) service_count++;
}
static uint32_t hash(const uint8_t *p) {
    uint32_t h = UINT32_C(2166136261);
    for (unsigned i=0;i<BYTES;i++) h=(h ^ p[i])*UINT32_C(16777619);
    return h;
}
static uint32_t oracle(uint32_t seed, uint32_t frame) {
    uint32_t x=seed ^ (frame * UINT32_C(0x9e3779b9)), h=UINT32_C(2166136261);
    for (unsigned i=0;i<N;i++) {
        x^=x<<13; x^=x>>17; x^=x<<5; x+=i+UINT32_C(0x6d2b79f5);
        for(unsigned c=0;c<3;c++) h=(h ^ ((x>>(8*c))&255))*UINT32_C(16777619);
    }
    return h;
}
typedef struct {
    wasm_module_inst_t inst;
    wasm_exec_env_t env;
    uint8_t *rgb;
    uint32_t generation, offset;
    size_t tracked_bytes, memory_bytes;
    bool output_pending, retired, alive;
} Component;
typedef struct { Component *owner; uint32_t generation; } View;
static __attribute__((noinline)) void activate(Component **active, Component *next) {
    (*active)->retired=true; *active=next;
}
static bool invoke(Component *c, const char *name, unsigned argc, uint32_t *args) {
    wasm_function_inst_t f = wasm_runtime_lookup_function(c->inst,name);
    REQUIRE(f);
    uint64_t t=now_ns(); bool ok=wasm_runtime_call_wasm(c->env,f,argc,args);
    uint64_t dt=now_ns()-t; if(dt>max_dispatch_ns) max_dispatch_ns=dt; calls++;
    return ok;
}
static uint32_t call0(Component *c,const char *name) { uint32_t a[1]={0}; REQUIRE(invoke(c,name,0,a)); return a[0]; }
static uint32_t call1(Component *c,const char *name,uint32_t v) { uint32_t a[1]={v}; REQUIRE(invoke(c,name,1,a)); return a[0]; }
static bool construct(Component *c,wasm_module_t module,uint32_t seed) {
    if(instances==3) return false;
    char err[256]; size_t before=live_bytes; uint64_t t=now_ns();
    memset(c,0,sizeof(*c));
    c->inst=wasm_runtime_instantiate(module,2048,0,err,sizeof(err));
    instantiate_ns+=now_ns()-t;
    if(!c->inst) return false;
    c->env=wasm_runtime_create_exec_env(c->inst,2048);
    if(!c->env) { wasm_runtime_deinstantiate(c->inst); c->inst=NULL; return false; }
    REQUIRE(call1(c,"configure",seed)==1);
    c->offset=call0(c,"buffer_offset");
    REQUIRE(wasm_runtime_validate_app_addr(c->inst,c->offset,BYTES));
    c->rgb=wasm_runtime_addr_app_to_native(c->inst,c->offset); REQUIRE(c->rgb);
    REQUIRE(next_view_generation!=0); c->generation=next_view_generation++; c->alive=true;
    wasm_memory_inst_t m=wasm_runtime_get_default_memory(c->inst);
    c->memory_bytes=wasm_memory_get_cur_page_count(m)*wasm_memory_get_bytes_per_page(m);
    c->tracked_bytes=live_bytes-before;
    instances++; if(instances>max_instances) max_instances=instances;
    return true;
}
static bool init_step(Component *c) {
    uint64_t t=now_ns(); bool done=call1(c,"initialize_step",64); init_ns+=now_ns()-t; return done;
}
static void initialize(Component *c) { while(!init_step(c)) service(); }
static void destroy(Component *c) {
    REQUIRE(c->alive && !c->output_pending);
    c->alive=false; REQUIRE(next_view_generation!=0); c->generation=next_view_generation++;
    wasm_runtime_destroy_exec_env(c->env); wasm_runtime_deinstantiate(c->inst); instances--;
}
static bool native_xor(View view,uint32_t offset,uint32_t length,uint8_t mask) {
    Component *c=view.owner;
    if(!c->alive || c->output_pending || c->retired || view.generation!=c->generation || offset>BYTES || length>BYTES-offset) return false;
    for(uint32_t i=offset;i<offset+length;i++) c->rgb[i]^=mask;
    return true;
}
static bool dispatch(Component *c) {
    if(!c->alive || c->retired || c->output_pending) return false;
    return call1(c,"step",QUANTUM)==1;
}
static void render(Component *c,uint32_t frame) {
    REQUIRE(call1(c,"start",frame));
    unsigned steps=0;
    while(!dispatch(c)) { REQUIRE(++steps<100); service(); }
}
static void submit(Component *c) {
    REQUIRE(call0(c,"publish")); c->output_pending=true;
    REQUIRE(next_view_generation!=0); c->generation=next_view_generation++;
}
static void complete(Component *c) {
    REQUIRE(c->output_pending); c->output_pending=false;
    REQUIRE(call0(c,"release")); REQUIRE(next_view_generation!=0); c->generation=next_view_generation++;
    if(c->retired) destroy(c);
}
static wasm_module_t load(uint8_t *bytes,size_t length) {
    char err[256]; uint64_t t=now_ns();
    wasm_module_t m=wasm_runtime_load(bytes,(uint32_t)length,err,sizeof(err));
    load_ns+=now_ns()-t;
    if(!m) fprintf(stderr,"loader rejected: %s\n",err);
    return m;
}
static uint8_t *read_artifact(const char *path,size_t *length) {
    FILE *f=fopen(path,"rb"); REQUIRE(f); REQUIRE(!fseek(f,0,SEEK_END));
    long size=ftell(f); REQUIRE(size>0 && size<1024*1024); rewind(f);
    uint8_t *bytes=malloc((size_t)size); REQUIRE(bytes);
    REQUIRE(fread(bytes,1,(size_t)size,f)==(size_t)size); fclose(f); *length=(size_t)size; return bytes;
}
int main(int argc,char **argv) {
    REQUIRE(argc==3);
    RuntimeInitArgs init={0}; init.mem_alloc_type=Alloc_With_Allocator;
    init.mem_alloc_option.allocator.malloc_func=tracked_malloc;
    init.mem_alloc_option.allocator.realloc_func=tracked_realloc;
    init.mem_alloc_option.allocator.free_func=tracked_free;
    REQUIRE(wasm_runtime_full_init(&init));
    REQUIRE(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    REQUIRE(fcntl(sockets[1],F_SETFL,O_NONBLOCK)==0);
    uint8_t flood[256]={0}; REQUIRE(send(sockets[0],flood,sizeof(flood),0)==sizeof(flood));
    size_t artifact_size; uint8_t *bytes=read_artifact(argv[1],&artifact_size);
    REQUIRE(wasm_runtime_get_file_package_type(bytes,(uint32_t)artifact_size)==Wasm_Module_AoT);
    wasm_module_t module=load(bytes,artifact_size); REQUIRE(module);
    REQUIRE(wasm_runtime_get_module_package_type(module)==Wasm_Module_AoT);
    size_t module_heap=live_bytes;
    Component a,b; REQUIRE(construct(&a,module,SEED));
    REQUIRE(call1(&a,"initialize_step",0)==0 && call1(&a,"start",0)==0);
    initialize(&a);
    REQUIRE(construct(&b,module,SEED+1)); initialize(&b);
    size_t per_instance=a.tracked_bytes, linear_memory=a.memory_bytes;
    REQUIRE(a.rgb!=b.rgb);
    REQUIRE(call1(&a,"start",100)); REQUIRE(call1(&a,"step",0)==0 && call0(&a,"position")==0);
    REQUIRE(call1(&a,"step",UINT32_MAX)==0 && call0(&a,"position")==64);
    uint32_t hashes[3];
    for(unsigned frame=0;frame<3;frame++) {
        REQUIRE(call1(&a,"start",frame)); REQUIRE(call1(&b,"start",frame));
        unsigned steps=0; bool adone=false,bdone=false;
        while(!adone || !bdone) {
            if(!adone) adone=dispatch(&a);
            if(!bdone) bdone=dispatch(&b);
            service(); REQUIRE(++steps<100);
            if(steps==1) { REQUIRE(call0(&a,"position")==QUANTUM); REQUIRE(call0(&b,"position")==QUANTUM); }
        }
        REQUIRE(steps==10); hashes[frame]=hash(a.rgb);
        REQUIRE(hashes[frame]==oracle(SEED,frame)); REQUIRE(hash(b.rgb)==oracle(SEED+1,frame));
    }
    View stale={&a,a.generation}; uint32_t before=hash(a.rgb);
    REQUIRE(native_xor(stale,0,BYTES,0x5a)); REQUIRE(hash(a.rgb)!=before);
    REQUIRE(native_xor(stale,0,BYTES,0x5a)); REQUIRE(hash(a.rgb)==before);
    REQUIRE(!native_xor(stale,BYTES,1,1)); REQUIRE(!native_xor(stale,UINT32_MAX,2,1));
    uint32_t checked[3]={call0(&a,"buffer_epoch"),BYTES,17}; REQUIRE(invoke(&a,"write_checked",3,checked) && checked[0]==0);
    uint32_t invalid[2]={UINT32_MAX,1}; REQUIRE(!invoke(&a,"raw_store",2,invalid));
    const char *exception=wasm_runtime_get_exception(a.inst); REQUIRE(exception && strstr(exception,"out of bounds"));
    wasm_runtime_clear_exception(a.inst); REQUIRE(hash(a.rgb)==before);

    /* Genuine allocator rejection leaves the live instances intact. */
    size_t saved_cap=allocation_cap, saved_live=live_bytes; allocation_cap=live_bytes;
    Component denied; REQUIRE(!construct(&denied,module,SEED)); allocation_cap=saved_cap;
    REQUIRE(live_bytes==saved_live && hash(a.rgb)==before); render(&b,10);
    uint8_t broken[16]={0}; wasm_module_t bad=load(broken,sizeof(broken)); REQUIRE(!bad);
    /* Recognizable AOT magic/version with a truncated body must also be rejected. */
    bad=load(bytes,12); REQUIRE(!bad); REQUIRE(hash(a.rgb)==before);

    /* Prepare replacement while both old effects remain schedulable. */
    size_t replacement_size; uint8_t *replacement_bytes=read_artifact(argv[2],&replacement_size);
    REQUIRE(replacement_size!=artifact_size || memcmp(bytes,replacement_bytes,artifact_size));
    wasm_module_t replacement_module=load(replacement_bytes,replacement_size); REQUIRE(replacement_module);
    Component replacement; REQUIRE(construct(&replacement,replacement_module,SEED+2));
    REQUIRE(!construct(&denied,module,SEED)); /* Three-instance admission cap. */
    unsigned service_before=service_count; uint32_t old_frame=20;
    REQUIRE(call1(&a,"start",old_frame)); REQUIRE(call1(&b,"start",old_frame));
    unsigned preparation_steps=0;
    while(!init_step(&replacement)) {
        if(dispatch(&a)) REQUIRE(call1(&a,"start",++old_frame));
        if(dispatch(&b)) REQUIRE(call1(&b,"start",old_frame));
        service(); preparation_steps++;
    }
    prep_service_count=service_count-service_before; REQUIRE(prep_service_count>=10);
    render(&a,31); submit(&a); uint32_t retained_hash=hash(a.rgb);
    size_t retained_bytes=a.tracked_bytes;
    REQUIRE(!native_xor(stale,0,1,1));
    checked[0]=stale.generation; checked[1]=0; checked[2]=99;
    REQUIRE(invoke(&a,"write_checked",3,checked) && checked[0]==0);
    REQUIRE(call1(&a,"start",32)==0 && !dispatch(&a));
    /* Deliberately bypass our generated ABI: in-range arbitrary stores still
     * write published storage. This experimentally establishes the trust limit. */
    uint8_t original=a.rgb[0]; uint32_t bypass[2]={a.offset,original ^ 255u};
    REQUIRE(invoke(&a,"raw_store",2,bypass) && a.rgb[0]!=(uint8_t)original);
    bypass[0]=a.offset; bypass[1]=original; REQUIRE(invoke(&a,"raw_store",2,bypass));
    REQUIRE(hash(a.rgb)==retained_hash);
    Component *active=&a; uint64_t t=now_ns();
    activate(&active,&replacement); activate_ns=now_ns()-t;
    REQUIRE(active==&replacement && a.alive && a.output_pending);
    REQUIRE(call1(active,"start",40)); REQUIRE(call1(&b,"start",40));
    for(unsigned tick=0;tick<12;tick++) {
        if(tick<10) { dispatch(active); dispatch(&b); }
        service(); REQUIRE(hash(a.rgb)==retained_hash && a.alive);
    }
    REQUIRE(hash(active->rgb)==oracle((SEED+2)^UINT32_C(0x5a5a5a5a),40)); REQUIRE(hash(b.rgb)==oracle(SEED+1,40));
    complete(&a); REQUIRE(!a.alive && instances==2); REQUIRE(!native_xor(stale,0,1,1));
    /* Reuse the exact host component slot; generations must survive ABA reuse. */
    REQUIRE(construct(&a,module,SEED)); initialize(&a);
    REQUIRE(!native_xor(stale,0,1,1)); destroy(&a);

    /* Adversarial work is an endless logical loop lowered into finite quanta. */
    REQUIRE(call1(&b,"start",50)); REQUIRE(call0(&b,"runaway"));
    service_before=service_count;
    for(unsigned tick=0;tick<20;tick++) { REQUIRE(!dispatch(&b)); service(); }
    REQUIRE(service_count>service_before); REQUIRE(call0(&b,"cancel"));
    REQUIRE(call1(&b,"step",QUANTUM)==UINT32_MAX); destroy(&b);

    uint64_t frame_samples[1000], frame_sum=0, frame_max=0;
    for(unsigned frame=0;frame<1000;frame++) {
        t=now_ns(); render(active,frame); frame_samples[frame]=now_ns()-t;
        frame_sum+=frame_samples[frame]; if(frame_samples[frame]>frame_max) frame_max=frame_samples[frame];
    }
    /* Drain the bounded flood one byte per scheduler turn. */
    while(service_count<sizeof(flood)) service();
    submit(active); uint32_t cancel_hash=hash(active->rgb); REQUIRE(call0(active,"cancel"));
    REQUIRE(!dispatch(active) && hash(active->rgb)==cancel_hash); complete(active);
    destroy(active); wasm_runtime_unload(replacement_module); wasm_runtime_unload(module);
    wasm_runtime_destroy(); free(bytes); free(replacement_bytes); close(sockets[0]); close(sockets[1]);
    REQUIRE(live_bytes==0 && allocations==0 && instances==0 && linear_allocated==0);
    printf("{\"schema_version\":1,\"backend\":\"wamr-aot\",\"status\":\"passed\","
           "\"environment\":{\"wamr_version\":\"2.4.5\",\"interpreter_enabled\":false,\"jit_enabled\":false,\"output\":\"host simulated peripheral\",\"service\":\"nonblocking POSIX socketpair, one byte per turn\"},"
           "\"workload\":{\"pixels\":300,\"seed\":305419896,\"frame_hashes\":[\"%08x\",\"%08x\",\"%08x\"]},"
           "\"checks\":{\"independent_instances\":true,\"resumable_execution\":true,\"native_buffer_alias\":true,\"native_bulk_operation\":true,\"async_output_retention\":true,\"service_progress\":true,\"replacement\":true,\"failed_replacement_preserves_old\":true,\"bounds_rejection\":true,\"stale_view_rejection\":true,\"runaway_bounded\":true,\"admission_failure\":true,\"malformed_artifact_rejected\":true,\"cleanup\":true,\"cancellation\":true,\"aot_only\":true},"
           "\"metrics\":{\"timing_unit\":\"ns\",\"load_total_ns\":%" PRIu64 ",\"instantiate_total_ns\":%" PRIu64 ",\"initialization_total_ns\":%" PRIu64 ",\"activation_ns\":%" PRIu64 ",\"max_guest_dispatch_ns\":%" PRIu64 ",\"max_service_gap_ns\":%" PRIu64 ",\"mean_frame_ns\":%" PRIu64 ",\"max_frame_ns\":%" PRIu64 ",\"benchmark_frames\":1000,\"guest_calls\":%u,\"service_messages\":%u,\"service_messages_during_preparation\":%u,\"preparation_steps\":%u,\"peak_tracked_runtime_heap_bytes\":%zu,\"loaded_module_and_runtime_heap_bytes\":%zu,\"tracked_instance_bytes\":%zu,\"linear_memory_bytes_per_instance\":%zu,\"retained_old_instance_bytes\":%zu,\"retained_output_buffer_bytes\":900,\"peak_instance_count\":%u,\"peak_rgb_buffer_bytes\":2700,\"pixel_data_copies\":0,\"heap_bytes_after_cleanup\":%zu,\"aot_artifact_bytes\":%zu,\"quantum_pixels\":32},"
           "\"memory_accounting\":{\"tracked_heap_definition\":\"requested allocator payload including linear memory; excludes headers and libc malloc overhead\",\"peak_linear_allocation_bytes\":%zu,\"linear_allocation_bytes_per_instance\":%zu,\"peak_allocator_header_bytes_upper_bound\":%zu,\"host_page_bytes\":%ld,\"simultaneous_artifact_staging_bytes\":%zu,\"executable_mapping_bytes\":null,\"native_stack_bytes\":null,\"host_component_struct_bytes\":%zu},"
           "\"observations\":{\"arbitrary_guest_store_can_mutate_published_buffer\":true,\"replacement_artifact_changes_seed_algorithm\":true},"
           "\"limitations\":[\"Host experiment: no MCU, physical DMA, Wi-Fi, MQTT, flash writes, multicore or browser compiler tested.\",\"Resumption uses explicit generated continuation state. Arbitrary uploaded AOT with uninstrumented loops is not preemptible or safe.\",\"WAMR protects linear-memory bounds; object bounds and ownership depend on the trusted compiler/scheduler contract. An explicit bypass test mutates published storage using an in-range raw guest store, then restores it.\",\"Native storage aliases WAMR-owned linear memory; retention pins the whole old instance. No general external-buffer mapping is demonstrated.\",\"Loading and instantiation are synchronous native operations. Reported maxima are observations, not proven interruption bounds.\",\"Tracked heap excludes mapped executable code, allocator overhead, host stack, socket buffers and process/SDK memory.\",\"The manifest describes a trusted local artifact; this prototype does not authenticate hostile uploads.\"]}\n",
           hashes[0],hashes[1],hashes[2],load_ns,instantiate_ns,init_ns,activate_ns,max_dispatch_ns,max_service_gap_ns,frame_sum/1000,frame_max,calls,service_count,prep_service_count,preparation_steps,peak_bytes,module_heap,per_instance,linear_memory,retained_bytes,max_instances,live_bytes,artifact_size,
           peak_linear_allocated,peak_linear_allocated/max_instances,peak_allocations*sizeof(Header),sysconf(_SC_PAGESIZE),artifact_size+replacement_size,sizeof(Component));
    return 0;
}
