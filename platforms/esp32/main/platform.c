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
#include "esp_system.h"
#include <stdio.h>
#include "esp_partition.h"
#include "soc/soc_caps.h"
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/temperature_sensor.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_clk.h"
#if LIGHTVM_WIFI
#include "esp_wifi.h"
#endif

static const PlatformInfo info={"esp32",LIGHTVM_BOARD_NAME,"ws2812"};
const PlatformInfo *platform_info(void) { return &info; }
uint64_t platform_time_us(void) { return (uint64_t)esp_timer_get_time(); }
const char *platform_reset_reason(void) {
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    default: return "other";
    }
}
unsigned platform_main_stack_peak(void) {
    return CONFIG_ESP_MAIN_TASK_STACK_SIZE - uxTaskGetStackHighWaterMark(NULL);
}
void esp32_port_init(void) {}
void platform_identity(char *controller,size_t capacity,char *boot,size_t boot_capacity) {
    uint8_t mac[6]; esp_read_mac(mac,ESP_MAC_WIFI_STA);
    snprintf(controller,capacity,"esp32-%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
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
    rmt_tx_channel_config_t rmt={
        .gpio_num=config->pin,.clk_src=RMT_CLK_SRC_DEFAULT,
        .resolution_hz=LIGHTVM_RMT_RESOLUTION_HZ,
        .mem_block_symbols=SOC_RMT_MEM_WORDS_PER_CHANNEL,
        .trans_queue_depth=1,.flags.with_dma=false
    };
    esp_err_t channel_error;
#if defined(SOC_RMT_SUPPORT_DMA) && SOC_RMT_SUPPORT_DMA
    // S3 has one DMA-capable TX channel. Prefer it, then retain ordinary
    // asynchronous RMT for further outputs or constrained DMA resources.
    rmt.flags.with_dma=true;rmt.mem_block_symbols=1024;
    channel_error=rmt_new_tx_channel(&rmt,&p->channel);
    if(channel_error==ESP_ERR_NOT_FOUND||channel_error==ESP_ERR_NOT_SUPPORTED||channel_error==ESP_ERR_NO_MEM){
        rmt.flags.with_dma=false;rmt.mem_block_symbols=SOC_RMT_MEM_WORDS_PER_CHANNEL;
        channel_error=rmt_new_tx_channel(&rmt,&p->channel);
    }
#else
    channel_error=rmt_new_tx_channel(&rmt,&p->channel);
#endif
    if (channel_error!=ESP_OK || lightvm_new_ws2812_encoder(&p->encoder)!=ESP_OK ) {
        platform_pixels_close(p); return false;
    }
    const rmt_tx_event_callbacks_t callbacks={.on_trans_done=completed};
    if(rmt_tx_register_event_callbacks(p->channel,&callbacks,p)!=ESP_OK||rmt_enable(p->channel)!=ESP_OK){platform_pixels_close(p);return false;}
    p->enabled=true;p->next=active_outputs;active_outputs=p; *driver=p; return true;
}
bool platform_pixels_submit(void *driver,const uint8_t *rgb,size_t count) {
    Pixels *p=driver;if(!p||count!=p->config.count||!atomic_load(&p->done))return false;
    if(!p->enabled){if(rmt_enable(p->channel)!=ESP_OK)return false;p->enabled=true;}
    /* Full output skips scaling; wire order and RMT staging still apply. */
    if(p->config.brightness==255)for(size_t i=0;i<count;i++)for(size_t c=0;c<3;c++){
        unsigned channel=p->config.order[c]=='R'?0:p->config.order[c]=='G'?1:2;
        p->wire[i*3+c]=rgb[i*3+channel];
    }
    else for(size_t i=0;i<count;i++)for(size_t c=0;c<3;c++){
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
#if LIGHTVM_ESP32_OPTIMIZED
static const uint8_t *storage_mapping;
static esp_partition_mmap_handle_t storage_mapping_handle;
#endif
uint32_t platform_storage_capacity(void) { return partition ? partition->size : 0; }
bool platform_storage_available(void) {
#if LIGHTVM_ESP32_OPTIMIZED
    if (storage_mapping) return true;
#endif
    partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"lightvm");
    if (!partition || partition->size<16384 || partition->size%8192 || partition->erase_size!=4096) return false;
#if LIGHTVM_ESP32_OPTIMIZED
    // VM instructions, call records and immutable artwork are small, frequent
    // reads. Map the store once instead of entering the SPI driver for each.
    // IDF write/erase operations invalidate cached aliases before returning.
    const void *mapped;
    if (esp_partition_mmap(partition,0,partition->size,ESP_PARTITION_MMAP_DATA,
                           &mapped,&storage_mapping_handle)!=ESP_OK) return false;
    storage_mapping=mapped;
#endif
    return true;
}
static bool bounds(uint32_t offset,size_t size) { return partition && offset<=partition->size && size<=partition->size-offset; }
bool platform_storage_read(uint32_t offset,void *bytes,size_t size) {
    if (!bounds(offset,size)) return false;
#if LIGHTVM_ESP32_OPTIMIZED
    if (!storage_mapping) return false;
    memcpy(bytes,storage_mapping+offset,size);
    return true;
#else
    return esp_partition_read(partition,offset,bytes,size)==ESP_OK;
#endif
}
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

unsigned platform_cpu_hz(void) { return (unsigned)esp_clk_cpu_freq(); }
bool platform_metric_supported(PlatformMetric metric) {
    switch(metric){
    case PLATFORM_METRIC_TEMPERATURE: return SOC_TEMP_SENSOR_SUPPORTED;
    case PLATFORM_METRIC_CPU_FREQUENCY:
    case PLATFORM_METRIC_HEAP_FREE:
    case PLATFORM_METRIC_HEAP_MIN: return true;
#if LIGHTVM_WIFI
    case PLATFORM_METRIC_WIFI_RSSI: return true;
#endif
    default: return false; // No onboard supply-voltage measurement circuit.
    }
}
static bool temperature(double *value) {
    static temperature_sensor_handle_t sensor;
    static bool high_range;
    for(unsigned attempt=0;attempt<2;attempt++){
        if(!sensor){
            temperature_sensor_config_t config=TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10,80);
            if(high_range){config.range_min=50;config.range_max=125;}
            if(temperature_sensor_install(&config,&sensor)!=ESP_OK)return false;
        }
        if(temperature_sensor_enable(sensor)!=ESP_OK)return false;
        float celsius=0;
        esp_err_t result=temperature_sensor_get_celsius(sensor,&celsius);
        esp_err_t disabled=temperature_sensor_disable(sensor);
        if(result==ESP_OK&&disabled==ESP_OK){*value=celsius;return true;}
        if(disabled!=ESP_OK)return false;
        // Switch overlapping ranges to retain readings above 80 C. A failed
        // retry is reported invalid, never replaced by a plausible temperature.
        if(temperature_sensor_uninstall(sensor)!=ESP_OK)return false;
        sensor=NULL;high_range=!high_range;
    }
    return false;
}
bool platform_metric_read(PlatformMetric metric,double *value) {
    switch(metric){
    case PLATFORM_METRIC_TEMPERATURE:return temperature(value);
    case PLATFORM_METRIC_CPU_FREQUENCY:*value=platform_cpu_hz();return true;
    case PLATFORM_METRIC_HEAP_FREE:*value=heap_caps_get_free_size(MALLOC_CAP_8BIT);return true;
    case PLATFORM_METRIC_HEAP_MIN:*value=heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);return true;
#if LIGHTVM_WIFI
    case PLATFORM_METRIC_WIFI_RSSI:{wifi_ap_record_t ap;if(esp_wifi_sta_get_ap_info(&ap)!=ESP_OK)return false;*value=ap.rssi;return true;}
#endif
    default:return false;
    }
}
