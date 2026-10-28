#include "platform.h"
#include "runtime.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include <malloc.h>
#include <unistd.h>
#if LIGHTVM_WIFI
#include "network_port.h"
#endif

bool platform_metric_supported(PlatformMetric metric) {
    switch(metric){
    case PLATFORM_METRIC_TEMPERATURE:
    case PLATFORM_METRIC_CPU_FREQUENCY:
    case PLATFORM_METRIC_HEAP_FREE:return true;
#ifdef PICO_VSYS_PIN
    case PLATFORM_METRIC_SUPPLY_VOLTAGE:return true;
#endif
#if LIGHTVM_WIFI
    case PLATFORM_METRIC_WIFI_RSSI:return true;
#endif
    default:return false;
    }
}
static bool sample(unsigned channel,double *voltage) {
    adc_select_input(channel);
    uint32_t total=0;
    // Discard initial conversions after switching the shared VSYS pin, then
    // average four samples. Each conversion has a bounded hardware wait.
    for(unsigned n=0;n<8;n++){
        hw_set_bits(&adc_hw->cs,ADC_CS_START_ONCE_BITS);
        uint64_t deadline=time_us_64()+100;
        while(!(adc_hw->cs&ADC_CS_READY_BITS))if(time_us_64()>=deadline)return false;
        if(adc_hw->cs&ADC_CS_ERR_BITS)return false;
        if(n>=4)total+=adc_hw->result;
    }
    *voltage=(double)total*(3.3/(4*4096));return true;
}
bool platform_metric_read(PlatformMetric metric,double *value) {
    static bool initialized;
    if(metric==PLATFORM_METRIC_CPU_FREQUENCY){*value=platform_cpu_hz();return true;}
    if(metric==PLATFORM_METRIC_HEAP_FREE){
        extern char __StackLimit;
        struct mallinfo heap=mallinfo();
        void *end=sbrk(0);if(end==(void *)-1)return false;
        *value=(double)((uintptr_t)&__StackLimit-(uintptr_t)end)+heap.fordblks;return true;
    }
#if LIGHTVM_WIFI
    if(metric==PLATFORM_METRIC_WIFI_RSSI)return pico_board_rssi(value);
#endif
    if(!platform_metric_supported(metric))return false;
    if(!initialized){adc_init();initialized=true;}
    unsigned previous=adc_get_selected_input();bool valid=false;double voltage;
    if(metric==PLATFORM_METRIC_TEMPERATURE){
        adc_set_temp_sensor_enabled(true);
        valid=sample(4,&voltage);
        adc_set_temp_sensor_enabled(false);
        if(valid)*value=27-(voltage-0.706)/0.001721;
    }
#ifdef PICO_VSYS_PIN
    else if(metric==PLATFORM_METRIC_SUPPLY_VOLTAGE){
#if CYW43_USES_VSYS_PIN && LIGHTVM_WIFI
        bool locked=pico_board_vsys_begin();
        // A disabled/uninitialized CYW43 cannot be woken to release the shared
        // pin. Report unavailable rather than publishing a loaded-pin voltage.
        if(locked){
#endif
        adc_gpio_init(PICO_VSYS_PIN);
        valid=sample(PICO_VSYS_PIN-26,&voltage);
        if(valid)*value=voltage*3;
#if CYW43_USES_VSYS_PIN && LIGHTVM_WIFI
        pico_board_vsys_end();
        }
#endif
    }
#endif
    adc_select_input(previous);return valid;
}
