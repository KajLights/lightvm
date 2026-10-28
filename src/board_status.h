#ifndef LIGHTVM_BOARD_STATUS_H
#define LIGHTVM_BOARD_STATUS_H
#include "inputs.h"
#include "device/network.h"
#define BOARD_SOURCE_LIMIT 8u
#define BOARD_SAMPLE_US 500000u
typedef struct {
    InputSignal signals[BOARD_SOURCE_LIMIT];
    unsigned metrics[BOARD_SOURCE_LIMIT], count;
    uint64_t next_sample;
} BoardStatus;
void board_status_init(BoardStatus *,InputRegistry *);
void board_status_tick(BoardStatus *,const Network *,uint64_t);
#endif
