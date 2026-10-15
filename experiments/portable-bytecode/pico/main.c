/* Pico PoC glue: XIP bytecode, existing pixel driver, and USB CDC. */
#include "poc_platform.h"
#include "platform.h"
#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "pico/stdio_usb.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include <malloc.h>
#include <string.h>
#include <unistd.h>
_Static_assert(POC_PIXELS>0 && POC_PIXELS<=1024,"pixel capacity");
_Static_assert(POC_GPIO<=22 || (POC_GPIO>=26 && POC_GPIO<=28),"invalid strip pin");
_Static_assert(POC_BRIGHTNESS>=0 && POC_BRIGHTNESS<=255,"brightness range");
_Static_assert(PICO_FLASH_SIZE_BYTES>=0x200000,"requires 2 MiB flash");
static const PocFlash storage[2]={{0x100000,65536,NULL},{0x110000,65536,NULL}};
const PocFlash *poc_slots[2]={&storage[0],&storage[1]};
PocPlatform poc_platform={"pico_w",POC_ORDER,POC_GPIO,POC_BRIGHTNESS,0};
static void *pixel_driver;
static size_t low_heap=SIZE_MAX;
extern char __StackLimit, __end__;
size_t poc_heap_free(void){
    struct mallinfo m=mallinfo();
    size_t available=(uintptr_t)&__StackLimit-(uintptr_t)sbrk(0)+(size_t)m.fordblks;
    if(available<low_heap)low_heap=available;
    return available;
}
size_t poc_heap_min(void){(void)poc_heap_free();return low_heap;}
size_t poc_heap_total(void){return (uintptr_t)&__StackLimit-(uintptr_t)&__end__;}
void poc_init(void){
    if(strlen(POC_ORDER)!=3 || !strchr(POC_ORDER,'R') || !strchr(POC_ORDER,'G') || !strchr(POC_ORDER,'B'))panic("invalid wire order");
    stdio_init_all();poc_platform.cpu_mhz=clock_get_hz(clk_sys)/1000000;
    const PixelConfig config={.pin=POC_GPIO,.count=POC_PIXELS,.brightness=POC_BRIGHTNESS,.order=POC_ORDER};
    if(!platform_pixels_open(&config,&pixel_driver))panic("pixel driver unavailable");
    (void)poc_heap_free();
}
uint64_t poc_now(void){return time_us_64();}
void poc_yield(void){tight_loop_contents();}
void poc_idle(void){(void)poc_heap_free();sleep_us(10);}
void poc_restart(void){sleep_ms(30);watchdog_reboot(0,0,0);for(;;)tight_loop_contents();}
int poc_read(uint8_t *bytes,size_t size){return stdio_usb.in_chars((char *)bytes,(int)size);}
int poc_write(const char *bytes,size_t size){stdio_usb.out_chars(bytes,(int)size);return (int)size;}
void poc_prepare_pixels(uint8_t *pixels,size_t size){(void)pixels;(void)size;}
bool poc_output(const uint8_t *pixels,size_t size){
    /* Existing platform driver consumes native RGB8 and applies configured
     * order/brightness. It completes the transfer before returning. */
    return size==POC_PIXELS*3 && platform_pixels_show(pixel_driver,pixels,POC_PIXELS);
}
bool poc_output_done(void){return true;}
uint32_t poc_crc32(uint32_t crc,const void *bytes,size_t size){
    const uint8_t *p=bytes;crc=~crc;
    while(size--){crc^=*p++;for(unsigned k=0;k<8;k++)crc=(crc>>1)^(0xedb88320u&-(crc&1u));}
    return ~crc;
}
static bool bounds(const PocFlash *s,uint32_t at,size_t size){return at<=s->size&&size<=s->size-at;}
bool poc_flash_read(const PocFlash *s,uint32_t at,void *bytes,size_t size){
    if(!bounds(s,at,size))return false;
    memcpy(bytes,(const void *)(XIP_BASE+s->offset+at),size);return true;
}
bool poc_flash_write(const PocFlash *s,uint32_t at,const void *bytes,size_t size){
    if(!bounds(s,at,size))return false;
    const uint8_t *p=bytes;
    while(size){
        uint32_t page_at=at&~(FLASH_PAGE_SIZE-1u);size_t in_page=at-page_at,n=FLASH_PAGE_SIZE-in_page;
        if(n>size)n=size;
        uint8_t page[FLASH_PAGE_SIZE];poc_flash_read(s,page_at,page,sizeof(page));
        /* NOR programming can only clear bits. Preserve earlier partial writes. */
        for(size_t k=0;k<n;k++)if((page[in_page+k]&p[k])!=p[k])return false;
        memcpy(page+in_page,p,n);
        uint32_t irq=save_and_disable_interrupts();
        flash_range_program(s->offset+page_at,page,sizeof(page));restore_interrupts(irq);
        p+=n;at+=n;size-=n;
    }
    return true;
}
bool poc_flash_erase(const PocFlash *s,uint32_t at,size_t size){
    if(!bounds(s,at,size)||at%FLASH_SECTOR_SIZE||size%FLASH_SECTOR_SIZE)return false;
    uint32_t irq=save_and_disable_interrupts();flash_range_erase(s->offset+at,size);restore_interrupts(irq);return true;
}
const uint8_t *poc_flash_map(const PocFlash *s,uint32_t at,size_t size,uintptr_t *handle){
    *handle=0;return bounds(s,at,size)?(const uint8_t *)(XIP_BASE+s->offset+at):NULL;
}
void poc_flash_unmap(uintptr_t handle){(void)handle;}
int main(void){poc_run();}
