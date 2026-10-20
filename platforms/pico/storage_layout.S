/* Absolute linker symbols use the very same SDK board geometry as platform.c. */
#include "pico.h"
#include "hardware/regs/addressmap.h"
#include "config.h"
/* Report the compiled resource profile and SDK memory geometry to the RAM gate. */
.global __lightvm_sram_start, __lightvm_sram_end, __lightvm_vm_heap, __lightvm_pixel_capacity, __lightvm_output_slots
.equ __lightvm_sram_start, SRAM_BASE
.equ __lightvm_sram_end, SRAM_END
.equ __lightvm_vm_heap, VM_HEAP_LIMIT
.equ __lightvm_pixel_capacity, PIXEL_CAPACITY
/* Output drivers use pio0/pio1. RP2040 networking consumes one of those SMs;
 * RP2350's SDK may place CYW43 on pio2, leaving all eight for outputs. */
#if LIGHTVM_WIFI && PICO_RP2040
.equ __lightvm_output_slots, 7
#else
.equ __lightvm_output_slots, 8
#endif
.global __lightvm_storage_start
.equ __lightvm_storage_start, XIP_BASE + PICO_FLASH_SIZE_BYTES - STORAGE_BYTES
#if LIGHTVM_BOOT_SEQUENCE
.global __lightvm_boot_storage_start
.equ __lightvm_boot_storage_start, XIP_BASE + PICO_FLASH_SIZE_BYTES - STORAGE_BYTES - 8192
#endif
