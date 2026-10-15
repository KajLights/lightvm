#ifndef LIGHTVM_SERVICE_H
#define LIGHTVM_SERVICE_H
#include "vm.h"
#include "tcp.h"
#define SERVICE_NAMESPACE "lightvm.experimental.v1"
#define SERVICE_DEADLINE_US 2000u
#define SERVICE_SAMPLE_LIMIT 2048u
typedef struct {
    uint8_t *bytes;
#if LIGHTVM_FLASH_EXPERIMENT
    ProgramMapping code_mapping;
#endif
    uint32_t size,crc,handle_sequence,handle,ipv4,port;
    IM3Environment environment;
    IM3Runtime runtime;
    IM3Module module;
    IM3Function init,step;
    TcpConnection tcp;
    VmInstance *animation;
    bool loaded,running,faulted,pending,measuring;
    double pending_speed;
    uint32_t pending_generation,pending_revision,command_result,io_budget;
    uint32_t calls,guest_state,execution_max_us,commands,commands_rejected,command_max_us;
    uint64_t execution_total_us,pending_at,started,ended,last_frame;
    uint32_t frame_gaps[SERVICE_SAMPLE_LIMIT],gap_count,gap_dropped,frames,missed;
    uint32_t command_latencies[256],latency_count,latency_dropped;
    uint32_t free_heap,min_heap,largest_block,window_heap_peak;
    char error[128];
} Service;
void service_init(Service *,VmInstance *);
void service_unload(Service *);
M3Result service_load(Service *,const uint8_t *,uint32_t);
void service_tick(Service *);
void service_frame(Service *,uint64_t);
bool service_manage(Service *,SendMessage,void *,uint16_t,const uint8_t *,size_t);
#endif
