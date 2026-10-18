/* Absolute linker symbols use the very same SDK board geometry as platform.c. */
#include "pico.h"
#include "hardware/regs/addressmap.h"
.global __lightvm_storage_start
.equ __lightvm_storage_start, XIP_BASE + PICO_FLASH_SIZE_BYTES - STORAGE_BYTES
#if LIGHTVM_BOOT_SEQUENCE
.global __lightvm_boot_storage_start
.equ __lightvm_boot_storage_start, XIP_BASE + PICO_FLASH_SIZE_BYTES - STORAGE_BYTES - 8192
#endif
