/* Local USB fixture harness. No microphone, network, NVS or storage writes.
 * The transform is kaj-fi/main/fft.c, including its optimized-S3 build guard. */
#include "runtime.h"
#include "driver/usb_serial_jtag.h"
#include "esp_chip_info.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static KfAnalysis analysis;
static _Alignas(16) uint8_t payload[8+KF_HOP*sizeof(int16_t)];
static float row[74];
static bool initialized;
static uint32_t hops,max_us;

static uint32_t crc32(const void *bytes,size_t size){
    const uint8_t *p=bytes;uint32_t crc=UINT32_MAX;
    while(size--){crc^=*p++;for(unsigned i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
    return ~crc;
}
static void receive(void *bytes,size_t size){
    uint8_t *p=bytes;
    while(size){int count=usb_serial_jtag_read_bytes(p,size,pdMS_TO_TICKS(1000));if(count>0){p+=count;size-=count;}}
}
static void send_bytes(const void *bytes,size_t size){
    const uint8_t *p=bytes;
    while(size){int count=usb_serial_jtag_write_bytes(p,size,pdMS_TO_TICKS(1000));if(count>0){p+=count;size-=count;}}
}
static void reply(unsigned status,const void *bytes,size_t size){
    uint32_t header[4]={0x3150414b,status,(uint32_t)size,crc32(bytes,size)};
    send_bytes(header,sizeof(header));send_bytes(bytes,size);
}
void app_main(void){
    usb_serial_jtag_driver_config_t usb={.rx_buffer_size=4096,.tx_buffer_size=4096};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));kf_fft_init();
    for(;;){
        uint32_t header[4];receive(header,sizeof(header));
        if(header[0]!=0x3150414b||header[2]>sizeof(payload))esp_restart();
        receive(payload,header[2]);
        if(crc32(payload,header[2])!=header[3]){reply(1,NULL,0);continue;}
        if(header[1]==1&&header[2]==32){
            uint32_t profile,bands,refractory;float values[5];
            memcpy(&profile,payload,4);memcpy(&bands,payload+4,4);memcpy(values,payload+8,20);memcpy(&refractory,payload+28,4);
            KfAnalysisConfig config={values[0],values[1],values[3],bands,values[2],values[4],refractory};
            initialized=kf_analysis_init_profile(&analysis,config,kf_fft,profile);
            hops=max_us=0;memset(row,0,sizeof(row));
            reply(initialized?0:2,analysis.band_edges,initialized?(bands+1)*sizeof(uint16_t):0);
        }else if(header[1]==2&&header[2]==sizeof(payload)&&initialized){
            uint64_t end;memcpy(&end,payload,8);float strength=0;
            int64_t started=esp_timer_get_time();
            bool ready=kf_analysis_push(&analysis,(const int16_t *)(payload+8),end,row+1,&strength);
            uint32_t elapsed=(uint32_t)(esp_timer_get_time()-started);if(elapsed>max_us)max_us=elapsed;++hops;
            row[0]=ready?1:analysis.filled?0:-1;
            if(ready){memcpy(row+7,analysis.spectrum,sizeof(analysis.spectrum));row[71]=analysis.onset.score;row[72]=analysis.onset.threshold;row[73]=strength;}
            reply(0,row,sizeof(row));
        }else if(header[1]==3&&!header[2]){
            char text[384];esp_chip_info_t chip;esp_chip_info(&chip);uint8_t mac[6];ESP_ERROR_CHECK(esp_read_mac(mac,ESP_MAC_WIFI_STA));
            int size=snprintf(text,sizeof(text),"{\"chip\":\"esp32s3\",\"revision\":%u,\"mac\":\"%02x%02x%02x%02x%02x%02x\",\"idf\":\"%s\",\"fft\":\"aes3\",\"hops\":%lu,\"max_dsp_us\":%lu,\"free_heap\":%lu,\"min_heap\":%lu,\"stack_free\":%u}",
                chip.revision,mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],esp_get_idf_version(),(unsigned long)hops,(unsigned long)max_us,
                (unsigned long)esp_get_free_heap_size(),(unsigned long)esp_get_minimum_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(NULL));
            reply(0,text,(size_t)size);
        }else if(header[1]==4&&!header[2]&&initialized){kf_analysis_reset(&analysis);memset(row,0,sizeof(row));reply(0,NULL,0);}
        else reply(2,NULL,0);
    }
}
