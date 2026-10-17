#include "platform.h"
#include "config.h"
#include "port.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "pico/rand.h"
#include <stdio.h>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <stdlib.h>
#include <string.h>
static const PlatformInfo info={"pico",LIGHTVM_BOARD_NAME,"ws2812"};
const PlatformInfo *platform_info(void) { return &info; }
uint64_t platform_time_us(void) { return time_us_64(); }
void pico_port_init(void) {}
void platform_identity(char *device,size_t capacity,char *boot,size_t boot_capacity) {
    char unique[2*PICO_UNIQUE_BOARD_ID_SIZE_BYTES+1]; pico_get_unique_board_id_string(unique,sizeof(unique));
    snprintf(device,capacity,"pico-%s",unique);
    snprintf(boot,boot_capacity,"%08lx%08lx",(unsigned long)get_rand_32(),(unsigned long)get_rand_32());
}
#define PIN(n) {"GP" #n,n}
static const BoardPin pins[]={PIN(0),PIN(1),PIN(2),PIN(3),PIN(4),PIN(5),PIN(6),PIN(7),PIN(8),PIN(9),PIN(10),PIN(11),PIN(12),PIN(13),PIN(14),PIN(15),PIN(16),PIN(17),PIN(18),PIN(19),PIN(20),PIN(21),PIN(22),PIN(26),PIN(27),PIN(28)};
const BoardPin *platform_pins(size_t *count) { *count=sizeof(pins)/sizeof(pins[0]); return pins; }
#define STORE_OFFSET (PICO_FLASH_SIZE_BYTES-STORAGE_BYTES)
_Static_assert(PICO_FLASH_SIZE_BYTES>=STORAGE_BYTES,"flash too small for storage");
static bool bounds(uint32_t offset,size_t size) { return offset<=STORAGE_BYTES && size<=STORAGE_BYTES-offset; }
bool platform_storage_available(void) { return true; }
bool platform_storage_read(uint32_t offset,void *bytes,size_t size) {
    if (!bounds(offset,size)) return false;
    memcpy(bytes,(const void *)(XIP_BASE+STORE_OFFSET+offset),size); return true;
}
bool platform_storage_write(uint32_t offset,const void *bytes,size_t size) {
    if (!bounds(offset,size) || offset%FLASH_PAGE_SIZE || size%FLASH_PAGE_SIZE) return false;
    // Copy each page before disabling XIP: source may itself reside in flash.
    uint8_t page[FLASH_PAGE_SIZE]; const uint8_t *p=bytes;
    while (size) {
        memcpy(page,p,sizeof(page)); uint32_t irq=save_and_disable_interrupts();
        flash_range_program(STORE_OFFSET+offset,page,sizeof(page)); restore_interrupts(irq);
        offset+=sizeof(page); p+=sizeof(page); size-=sizeof(page);
    }
    return true;
}
bool platform_storage_erase(uint32_t offset,size_t size) {
    if (!bounds(offset,size) || offset%FLASH_SECTOR_SIZE || size%FLASH_SECTOR_SIZE) return false;
    uint32_t irq=save_and_disable_interrupts(); flash_range_erase(STORE_OFFSET+offset,size); restore_interrupts(irq); return true;
}
bool platform_storage_sync(void) { return true; }
#if LIGHTVM_BOOT_SEQUENCE
#define BOOT_STORE_OFFSET (STORE_OFFSET-BOOT_SEQUENCE_STORAGE_BYTES)
_Static_assert(PICO_FLASH_SIZE_BYTES>=STORAGE_BYTES+BOOT_SEQUENCE_STORAGE_BYTES,"flash too small for boot journal");
static bool boot_bounds(uint32_t offset,size_t size) {
    return offset<=BOOT_SEQUENCE_STORAGE_BYTES && size<=BOOT_SEQUENCE_STORAGE_BYTES-offset;
}
bool platform_boot_storage_available(void) { return true; }
bool platform_boot_storage_read(uint32_t offset,void *bytes,size_t size) {
    if (!boot_bounds(offset,size)) return false;
    memcpy(bytes,(const void *)(XIP_BASE+BOOT_STORE_OFFSET+offset),size); return true;
}
bool platform_boot_storage_write(uint32_t offset,const void *bytes,size_t size) {
    if (!boot_bounds(offset,size) || offset%FLASH_PAGE_SIZE || size%FLASH_PAGE_SIZE) return false;
    uint8_t page[FLASH_PAGE_SIZE]; const uint8_t *p=bytes;
    while (size) {
        memcpy(page,p,sizeof(page)); uint32_t irq=save_and_disable_interrupts();
        flash_range_program(BOOT_STORE_OFFSET+offset,page,sizeof(page)); restore_interrupts(irq);
        offset+=sizeof(page); p+=sizeof(page); size-=sizeof(page);
    }
    return true;
}
bool platform_boot_storage_erase(uint32_t offset,size_t size) {
    if (!boot_bounds(offset,size) || offset%FLASH_SECTOR_SIZE || size%FLASH_SECTOR_SIZE) return false;
    uint32_t irq=save_and_disable_interrupts();
    flash_range_erase(BOOT_STORE_OFFSET+offset,size); restore_interrupts(irq); return true;
}
bool platform_boot_storage_sync(void) { return true; }
#endif
void *platform_calloc(size_t count,size_t size) { return calloc(count,size); }
void *platform_realloc(void *memory,size_t size) { return realloc(memory,size); }
void platform_free(void *memory) { free(memory); }
