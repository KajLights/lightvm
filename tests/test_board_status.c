#include "board_status.h"
#include "platform.h"
#include "bytecode_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static unsigned calls;
static double temperature=60,heap=10000;
static bool temperature_valid=true;
bool platform_metric_supported(PlatformMetric metric) {
    return metric==PLATFORM_METRIC_TEMPERATURE||metric==PLATFORM_METRIC_HEAP_FREE
#if LIGHTVM_WIFI
        ||metric==PLATFORM_METRIC_WIFI_RSSI
#endif
        ;
}
bool platform_metric_read(PlatformMetric metric,double *value) {
    ++calls;
    if(metric==PLATFORM_METRIC_TEMPERATURE){*value=temperature;return temperature_valid;}
    if(metric==PLATFORM_METRIC_WIFI_RSSI){*value=-55;return true;}
    CHECK(metric==PLATFORM_METRIC_HEAP_FREE);*value=heap;return true;
}
bool network_online(const Network *network) { return network->state==NET_CONNECTED||network->state==NET_AP_ACTIVE; }
int main(void) {
    BoardStatus board;InputRegistry registry={0};Network network={0};
    board_status_init(&board,&registry);
    CHECK(registry.native==board.signals&&registry.native_count==3+2*LIGHTVM_WIFI);
    InputSignal *uptime=&board.signals[2+LIGHTVM_WIFI];
    CHECK(!registry.count&&!registry.signals&&!registry.generation);
    CHECK(!strcmp(board.signals[0].name,"board.temperature_c"));
    CHECK(!strcmp(board.signals[0].unit,"°C"));
    CHECK(board.signals[0].type==T_FLOAT&&!board.signals[0].valid);
    board_status_tick(&board,&network,0);
    CHECK(calls==2&&board.signals[0].valid&&board.signals[0].value.f==60);
    CHECK(board.signals[1].type==T_INT&&board.signals[1].value.i==10000);
    CHECK(uptime->value.f==0&&uptime->valid);
    board_status_tick(&board,&network,BOARD_SAMPLE_US-1);CHECK(calls==2);
    temperature_valid=false;
    board_status_tick(&board,&network,BOARD_SAMPLE_US);
    CHECK(!board.signals[0].valid&&board.signals[0].received==0);
    CHECK(uptime->value.f==0.5f);
    temperature_valid=true;temperature=NAN;heap=INFINITY;
    board_status_tick(&board,&network,1000000);
    CHECK(!board.signals[0].valid&&!board.signals[1].valid);
    temperature=1e300;heap=2147483648.0;
    board_status_tick(&board,&network,1500000);
    CHECK(!board.signals[0].valid&&!board.signals[1].valid);
    temperature=0;heap=12000;
    board_status_tick(&board,&network,2000000);
    CHECK(board.signals[0].valid&&board.signals[0].value.f==0);
    CHECK(board.signals[0].received==2000000&&board.signals[0].sequence==2);
    CHECK(board.signals[1].valid&&board.signals[1].value.i==12000);
    CHECK(uptime->value.f==2);
#if LIGHTVM_WIFI
    InputSignal *rssi=&board.signals[2],*online=&board.signals[4];
    CHECK(!rssi->valid&&online->valid&&!online->value.u);
    network.state=NET_CONNECTED;
    board_status_tick(&board,&network,2500000);
    CHECK(rssi->valid&&rssi->value.i==-55&&online->value.u);
    network.state=NET_AP_ACTIVE;
    board_status_tick(&board,&network,3000000);
    CHECK(!rssi->valid&&online->value.u);
#endif
    puts("board sampling contract passed");
}
