#ifndef LIGHTVM_PICO_W_BOARD_CONFIG_H
#define LIGHTVM_PICO_W_BOARD_CONFIG_H
#define LIGHTVM_BOARD_NAME "pico_w"
/* Native image/map kernels contend with both interpreters in RP2040's
   shared XIP cache. Keep measured hot kernels in budgeted SRAM. */
#define LIGHTVM_DRAW_CODE __attribute__((section(".time_critical.lightvm_draw")))
#ifndef LIGHTVM_RENDER_SLICE_US
/* Amortize network/presentation service on RP2040 while keeping commands
   responsive. A 500 us slice starves mixed sprite/background workloads. */
#define LIGHTVM_RENDER_SLICE_US 2000u
#endif
#if LIGHTVM_WIFI
#if LIGHTVM_HTTP
#define VM_HEAP_LIMIT (64u * 1024u)
#else
#define VM_HEAP_LIMIT (96u * 1024u)
#endif
#ifndef LIGHTVM_HTTP_CLIENTS
#define LIGHTVM_HTTP_CLIENTS 2u
#endif
#endif
#endif
