#include "platform.h"
#include "config.h"
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
typedef struct {
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;
    uint8_t *wire;
    PixelConfig config;
    bool enabled;
} Pixels;
void platform_pixels_close(void *driver) {
    Pixels *p=driver; if (!p) return;
    if (p->enabled) (void)rmt_disable(p->channel);
    if (p->encoder) (void)rmt_del_encoder(p->encoder);
    if (p->channel) (void)rmt_del_channel(p->channel);
    (void)gpio_reset_pin(p->config.pin);
    free(p->wire); free(p);
}
bool platform_pixels_open(const PixelConfig *config,void **driver) {
    if (!GPIO_IS_VALID_OUTPUT_GPIO(config->pin)) return false;
    Pixels *p=calloc(1,sizeof(*p)); if (!p) return false;
    p->config=*config; p->wire=calloc(config->count,3);
    if (!p->wire) { platform_pixels_close(p); return false; }
    const rmt_tx_channel_config_t rmt={
        .gpio_num=config->pin,.clk_src=RMT_CLK_SRC_DEFAULT,
        .resolution_hz=LIGHTVM_RMT_RESOLUTION_HZ,
        .mem_block_symbols=SOC_RMT_MEM_WORDS_PER_CHANNEL,
        .trans_queue_depth=1,.flags.with_dma=false
    };
    if (rmt_new_tx_channel(&rmt,&p->channel)!=ESP_OK || lightvm_new_ws2812_encoder(&p->encoder)!=ESP_OK || rmt_enable(p->channel)!=ESP_OK) {
        platform_pixels_close(p); return false;
    }
    p->enabled=true; *driver=p; return true;
}
bool platform_pixels_show(void *driver,const uint8_t *rgb,size_t count) {
    Pixels *p=driver; if (!p || count!=p->config.count) return false;
    for (size_t i=0;i<count;++i) for (size_t c=0;c<3;++c) {
        unsigned channel=p->config.order[c]=='R' ? 0 : p->config.order[c]=='G' ? 1 : 2;
        p->wire[i*3+c]=(rgb[i*3+channel]*p->config.brightness+127u)/255u;
    }
    const rmt_transmit_config_t transmit={.loop_count=0};
    if (rmt_transmit(p->channel,p->encoder,p->wire,count*3,&transmit)!=ESP_OK) return false;
    int timeout_ms=(int)((count*30u+300u+999u)/1000u+100u);
    if (rmt_tx_wait_all_done(p->channel,timeout_ms)!=ESP_OK) {
        // Disable before the retained transmission buffer can be reused/freed.
        (void)rmt_disable(p->channel); p->enabled=false; return false;
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
    return bounds(offset,size) && offset%256==0 && size%256==0 && esp_partition_write(partition,offset,bytes,size)==ESP_OK;
}
bool platform_storage_erase(uint32_t offset,size_t size) {
    return bounds(offset,size) && offset%4096==0 && size%4096==0 && esp_partition_erase_range(partition,offset,size)==ESP_OK;
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
    return boot_bounds(offset,size) && offset%256==0 && size%256==0 && esp_partition_write(boot_partition,offset,bytes,size)==ESP_OK;
}
bool platform_boot_storage_erase(uint32_t offset,size_t size) {
    return boot_bounds(offset,size) && offset%4096==0 && size%4096==0 && esp_partition_erase_range(boot_partition,offset,size)==ESP_OK;
}
bool platform_boot_storage_sync(void) { return boot_partition!=NULL; }
#endif
void *platform_calloc(size_t count,size_t size) { return calloc(count,size); }
void *platform_realloc(void *memory,size_t size) { return realloc(memory,size); }
void platform_free(void *memory) { free(memory); }
