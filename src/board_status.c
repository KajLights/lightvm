#include "board_status.h"
#include "platform.h"
#include "bytecode_core.h"
#include <limits.h>
#include <float.h>
#include <math.h>
#include <string.h>

/* Unsupported capabilities are absent; supported sensors that fail stay visible
 * but invalid, so the ordinary source timeout/fallback policy applies. */
static const struct { const char *name,*label,*unit; unsigned type; float minimum,maximum; } metrics[]={
    {"board.temperature_c","Chip temperature","°C",T_FLOAT,20,80},
    {"board.supply_voltage_v","Supply voltage (VSYS)","V",T_FLOAT,0,6},
    {"board.cpu_frequency_hz","CPU frequency","Hz",T_INT,0,240000000},
    {"board.heap_free_bytes","Free heap","bytes",T_INT,0,262144},
    {"board.heap_min_bytes","Minimum free heap","bytes",T_INT,0,262144},
    {"board.wifi_rssi_dbm","Wi-Fi signal","dBm",T_INT,-100,-30},
    {"board.uptime_s","Uptime","s",T_FLOAT,0,3600},
    {"board.network_online","Network online","",T_BOOL,0,1},
};
_Static_assert(sizeof(metrics)/sizeof(metrics[0])==BOARD_SOURCE_LIMIT,"board source count");
__attribute__((weak)) bool platform_metric_supported(PlatformMetric metric) { (void)metric;return false; }
__attribute__((weak)) bool platform_metric_read(PlatformMetric metric,double *value) { (void)metric;(void)value;return false; }
void board_status_init(BoardStatus *board,InputRegistry *registry) {
    memset(board,0,sizeof(*board));
    for(unsigned i=0;i<BOARD_SOURCE_LIMIT;i++){
        bool supported=i==PLATFORM_METRIC_COUNT ||
            (i==PLATFORM_METRIC_COUNT+1 ? LIGHTVM_WIFI : i<PLATFORM_METRIC_COUNT && platform_metric_supported((PlatformMetric)i));
        if(!supported)continue;
        InputSignal *s=&board->signals[board->count];board->metrics[board->count++]=i;
        strcpy(s->name,metrics[i].name);s->type=metrics[i].type;s->label=metrics[i].label;s->unit=metrics[i].unit;
        s->suggested_min=metrics[i].minimum;s->suggested_max=metrics[i].maximum;
    }
    registry->native=board->signals;registry->native_count=board->count;
}
void board_status_tick(BoardStatus *board,const Network *network,uint64_t now) {
    if(now<board->next_sample)return;
    board->next_sample=now+BOARD_SAMPLE_US;
    for(unsigned n=0;n<board->count;n++){
        InputSignal *s=&board->signals[n];unsigned metric=board->metrics[n];double value=0;
        bool valid=true;
        if(metric==PLATFORM_METRIC_COUNT)value=(double)now/1000000;
        else if(metric==PLATFORM_METRIC_COUNT+1)value=network_online(network);
        else if(metric==PLATFORM_METRIC_WIFI_RSSI && network->state!=NET_CONNECTED)valid=false;
        else valid=platform_metric_read((PlatformMetric)metric,&value);
        s->valid=valid&&isfinite(value) && (s->type!=T_FLOAT || (value>=-FLT_MAX&&value<=FLT_MAX)) && (s->type!=T_INT || (value>=INT32_MIN&&value<=INT32_MAX));
        if(!s->valid)continue;
        if(s->type==T_FLOAT)s->value.f=(float)value;
        else if(s->type==T_BOOL)s->value.u=value!=0;
        else s->value.i=(int32_t)value;
        s->received=now;if(++s->sequence==0)s->sequence=1;
    }
}
