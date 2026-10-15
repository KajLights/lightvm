#include "platform.h"
#include "config.h"
#include "runtime.h"
#include "port.h"
#include "ws2812_encoder.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_random.h"
#include <stdio.h>
#include "esp_partition.h"
#include "soc/soc_caps.h"
#include <stdlib.h>
#include <stdatomic.h>
#include "esp_attr.h"

static const PlatformInfo info={"esp32",LIGHTVM_BOARD_NAME,"ws2812",-1,0};
const PlatformInfo *platform_info(void) { return &info; }
uint64_t platform_time_us(void) { return (uint64_t)esp_timer_get_time(); }
void esp32_port_init(void) {}
void platform_identity(char *device,size_t capacity,char *boot,size_t boot_capacity) {
    uint8_t mac[6]; esp_read_mac(mac,ESP_MAC_WIFI_STA);
    snprintf(device,capacity,"esp32-%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    snprintf(boot,boot_capacity,"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
}
#define PIN(d,g) {"D" #d,g},{"GPIO" #g,g}
#if CONFIG_IDF_TARGET_ESP32S3
static const BoardPin pins[]={PIN(0,1),PIN(1,2),PIN(2,3),PIN(3,4),PIN(4,5),PIN(5,6),PIN(6,43),PIN(7,44),PIN(8,7),PIN(9,8),PIN(10,9)};
#elif CONFIG_IDF_TARGET_ESP32C3
static const BoardPin pins[]={PIN(0,2),PIN(1,3),PIN(2,4),PIN(3,5),PIN(4,6),PIN(5,7),PIN(6,21),PIN(7,20),PIN(8,8),PIN(9,9),PIN(10,10)};
#elif CONFIG_IDF_TARGET_ESP32C6
static const BoardPin pins[]={PIN(0,0),PIN(1,1),PIN(2,2),PIN(3,21),PIN(4,22),PIN(5,23),PIN(6,16),PIN(7,17),PIN(8,19),PIN(9,20),PIN(10,18)};
#else
#error "Unsupported ESP32 target"
#endif
const BoardPin *platform_pins(size_t *count) { *count=sizeof(pins)/sizeof(pins[0]); return pins; }
typedef struct Pixels {
    struct Pixels *next;
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;
    uint8_t *wire;
    PixelConfig config;
    bool enabled,queued;
    atomic_bool done;
    uint64_t deadline,not_before;
} Pixels;
static Pixels *active_outputs;
static bool IRAM_ATTR completed(rmt_channel_handle_t channel,const rmt_tx_done_event_data_t *event,void *context) {
    (void)channel;(void)event;Pixels *p=context;atomic_store_explicit(&p->done,true,memory_order_release);return false;
}
void platform_pixels_cancel(void *driver) {
    Pixels *p=driver;if(!p)return;
    if(p->enabled&&!p->queued&&!atomic_load_explicit(&p->done,memory_order_acquire)){(void)rmt_disable(p->channel);p->enabled=false;}
    p->queued=false;p->not_before=platform_time_us()+330;atomic_store(&p->done,true);
}
void platform_pixels_close(void *driver) {
    Pixels *p=driver; if (!p) return;
    if (p->enabled) (void)rmt_disable(p->channel);
    if (p->encoder) (void)rmt_del_encoder(p->encoder);
    if (p->channel) (void)rmt_del_channel(p->channel);
    (void)gpio_reset_pin(p->config.pin);
    Pixels **link=&active_outputs;while(*link&&*link!=p)link=&(*link)->next;
    if(*link)*link=p->next;
    free(p->wire); free(p);
}
bool platform_pixels_open(const PixelConfig *config,void **driver) {
    if (!GPIO_IS_VALID_OUTPUT_GPIO(config->pin)) return false;
    Pixels *p=calloc(1,sizeof(*p)); if (!p) return false;
    atomic_init(&p->done,true);p->config=*config; p->wire=calloc(config->count,3);
    if (!p->wire) { platform_pixels_close(p); return false; }
    const rmt_tx_channel_config_t rmt={
        .gpio_num=config->pin,.clk_src=RMT_CLK_SRC_DEFAULT,
        .resolution_hz=LIGHTVM_RMT_RESOLUTION_HZ,
        .mem_block_symbols=SOC_RMT_MEM_WORDS_PER_CHANNEL,
        .trans_queue_depth=1,.flags.with_dma=false
    };
    if (rmt_new_tx_channel(&rmt,&p->channel)!=ESP_OK || lightvm_new_ws2812_encoder(&p->encoder)!=ESP_OK ) {
        platform_pixels_close(p); return false;
    }
    const rmt_tx_event_callbacks_t callbacks={.on_trans_done=completed};
    if(rmt_tx_register_event_callbacks(p->channel,&callbacks,p)!=ESP_OK||rmt_enable(p->channel)!=ESP_OK){platform_pixels_close(p);return false;}
    p->enabled=true;p->next=active_outputs;active_outputs=p; *driver=p; return true;
}
bool platform_pixels_submit(void *driver,const uint8_t *rgb,size_t count) {
    Pixels *p=driver;if(!p||count!=p->config.count||!atomic_load(&p->done))return false;
    if(!p->enabled){if(rmt_enable(p->channel)!=ESP_OK)return false;p->enabled=true;}
    for(size_t i=0;i<count;i++)for(size_t c=0;c<3;c++){
        unsigned channel=p->config.order[c]=='R'?0:p->config.order[c]=='G'?1:2;
        p->wire[i*3+c]=(rgb[i*3+channel]*p->config.brightness+127u)/255u;
    }
    p->deadline=platform_time_us()+count*30u+100000u;p->queued=true;atomic_store(&p->done,false);return true;
}
int platform_pixels_poll(void *driver) {
    Pixels *p=driver;if(!p)return -1;if(atomic_load_explicit(&p->done,memory_order_acquire))return 1;
    if(p->queued){
        if(platform_time_us()<p->not_before)return 0;
        const rmt_transmit_config_t transmit={.loop_count=0};p->queued=false;
        if(rmt_transmit(p->channel,p->encoder,p->wire,p->config.count*3,&transmit)!=ESP_OK){atomic_store(&p->done,true);return -1;}
    }
    if(platform_time_us()<p->deadline)return 0;
    platform_pixels_cancel(p);return -1;
}
bool platform_pixels_show(void *driver,const uint8_t *rgb,size_t count) {
    platform_pixels_cancel(driver);if(!platform_pixels_submit(driver,rgb,count))return false;
    int status;do{status=platform_pixels_poll(driver);if(!status)runtime_checkpoint();}while(!status);return status>0;
}
static bool flash_ready(void) {
    for(Pixels *p=active_outputs;p;p=p->next){
        int status;do{status=platform_pixels_poll(p);}while(!status);
        if(status<0)return false;
    }
    return true;
}
static const esp_partition_t *partition;
bool platform_storage_available(void) {
    partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"lightvm");
    return partition && partition->size==STORAGE_BYTES && partition->erase_size==4096;
}
static bool bounds(uint32_t offset,size_t size) { return partition && offset<=STORAGE_BYTES && size<=STORAGE_BYTES-offset; }
bool platform_storage_read(uint32_t offset,void *bytes,size_t size) { return bounds(offset,size) && esp_partition_read(partition,offset,bytes,size)==ESP_OK; }
bool platform_storage_write(uint32_t offset,const void *bytes,size_t size) {
    return flash_ready() && bounds(offset,size) && offset%256==0 && size%256==0 && esp_partition_write(partition,offset,bytes,size)==ESP_OK;
}
bool platform_storage_erase(uint32_t offset,size_t size) {
    return flash_ready() && bounds(offset,size) && offset%4096==0 && size%4096==0 && esp_partition_erase_range(partition,offset,size)==ESP_OK;
}
bool platform_storage_sync(void) { return partition!=NULL; }
#if LIGHTVM_BOOT_SEQUENCE
static const esp_partition_t *boot_partition;
bool platform_boot_storage_available(void) {
    boot_partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"lightvm_boot");
    return boot_partition && boot_partition->size==BOOT_SEQUENCE_STORAGE_BYTES && boot_partition->erase_size==4096;
}
static bool boot_bounds(uint32_t offset,size_t size) {
    return boot_partition && offset<=BOOT_SEQUENCE_STORAGE_BYTES && size<=BOOT_SEQUENCE_STORAGE_BYTES-offset;
}
bool platform_boot_storage_read(uint32_t offset,void *bytes,size_t size) {
    return boot_bounds(offset,size) && esp_partition_read(boot_partition,offset,bytes,size)==ESP_OK;
}
bool platform_boot_storage_write(uint32_t offset,const void *bytes,size_t size) {
    return flash_ready() && boot_bounds(offset,size) && offset%256==0 && size%256==0 && esp_partition_write(boot_partition,offset,bytes,size)==ESP_OK;
}
bool platform_boot_storage_erase(uint32_t offset,size_t size) {
    return flash_ready() && boot_bounds(offset,size) && offset%4096==0 && size%4096==0 && esp_partition_erase_range(boot_partition,offset,size)==ESP_OK;
}
bool platform_boot_storage_sync(void) { return boot_partition!=NULL; }
#endif
void *platform_calloc(size_t count,size_t size) { return calloc(count,size); }
void *platform_realloc(void *memory,size_t size) { return realloc(memory,size); }
void platform_free(void *memory) { free(memory); }
