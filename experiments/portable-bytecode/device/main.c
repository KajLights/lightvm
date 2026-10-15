/* ESP32-C3 services for the shared PoC runtime. */
#include "poc_platform.h"
#include "ws2812_encoder.h"
#include "driver/rmt_tx.h"
#include "driver/usb_serial_jtag.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_rom_crc.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include <stdlib.h>
static PocFlash storage[2];
const PocFlash *poc_slots[2]={&storage[0],&storage[1]};
PocPlatform poc_platform={"esp32c3","GRB",3,16,160};
static rmt_channel_handle_t channel;
static rmt_encoder_handle_t encoder;
static volatile bool done=true;
static bool IRAM_ATTR completed(rmt_channel_handle_t c,const rmt_tx_done_event_data_t *d,void *u){(void)c;(void)d;(void)u;done=true;return false;}
void poc_init(void){
    usb_serial_jtag_driver_config_t usb={.tx_buffer_size=4096,.rx_buffer_size=4096};ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    const rmt_tx_channel_config_t tx={.gpio_num=3,.clk_src=RMT_CLK_SRC_DEFAULT,.resolution_hz=20000000,.mem_block_symbols=SOC_RMT_MEM_WORDS_PER_CHANNEL,.trans_queue_depth=1,.flags.with_dma=false};
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx,&channel));ESP_ERROR_CHECK(lightvm_new_ws2812_encoder(&encoder));
    const rmt_tx_event_callbacks_t callbacks={.on_trans_done=completed};ESP_ERROR_CHECK(rmt_tx_register_event_callbacks(channel,&callbacks,NULL));ESP_ERROR_CHECK(rmt_enable(channel));
    for(unsigned n=0;n<2;n++){
        const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,n?"program_b":"program_a");
        if(!p)abort();
        storage[n]=(PocFlash){p->address,p->size,p};
    }
}
uint64_t poc_now(void){return (uint64_t)esp_timer_get_time();}
void poc_yield(void){taskYIELD();}
void poc_idle(void){static uint64_t yielded;uint64_t t=poc_now();if(t-yielded>=1000){vTaskDelay(1);yielded=poc_now();}}
void poc_restart(void){vTaskDelay(pdMS_TO_TICKS(30));esp_restart();}
int poc_read(uint8_t *bytes,size_t size){return usb_serial_jtag_read_bytes(bytes,size,0);}
int poc_write(const char *bytes,size_t size){return usb_serial_jtag_write_bytes(bytes,size,pdMS_TO_TICKS(20));}
void poc_prepare_pixels(uint8_t *p,size_t size){
    /* Same in-place transform used by the original C3 PoC. */
    for(size_t k=0;k<size;k+=3){uint8_t r=p[k],g=p[k+1];p[k]=(g*16u+127u)/255u;p[k+1]=(r*16u+127u)/255u;p[k+2]=(p[k+2]*16u+127u)/255u;}
}
bool poc_output(const uint8_t *pixels,size_t size){
    done=false;const rmt_transmit_config_t tx={.loop_count=0};
    if(rmt_transmit(channel,encoder,pixels,size,&tx)!=ESP_OK){done=true;return false;}return true;
}
bool poc_output_done(void){return done;}
uint32_t poc_crc32(uint32_t crc,const void *bytes,size_t size){return esp_rom_crc32_le(crc,bytes,size);}
bool poc_flash_read(const PocFlash *s,uint32_t at,void *bytes,size_t size){return esp_partition_read(s->native,at,bytes,size)==ESP_OK;}
bool poc_flash_write(const PocFlash *s,uint32_t at,const void *bytes,size_t size){return esp_partition_write(s->native,at,bytes,size)==ESP_OK;}
bool poc_flash_erase(const PocFlash *s,uint32_t at,size_t size){return esp_partition_erase_range(s->native,at,size)==ESP_OK;}
const uint8_t *poc_flash_map(const PocFlash *s,uint32_t at,size_t size,uintptr_t *handle){
    const void *mapped=NULL;esp_partition_mmap_handle_t h;
    if(esp_partition_mmap(s->native,at,size,ESP_PARTITION_MMAP_DATA,&mapped,&h)!=ESP_OK)return NULL;
    *handle=h;return mapped;
}
void poc_flash_unmap(uintptr_t handle){esp_partition_munmap(handle);}
size_t poc_heap_free(void){return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);}
size_t poc_heap_min(void){return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);}
size_t poc_heap_total(void){return heap_caps_get_total_size(MALLOC_CAP_INTERNAL);}
void app_main(void){poc_run();}
