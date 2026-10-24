#include "platform.h"
#include "hardware/adc.h"
#include "malloc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
TestAdc test_adc;
char __StackLimit;
static unsigned selected=2,initializations,conversions,gpio_reads;
static uint64_t elapsed;
static bool temperature_enabled,fail,timeout;
unsigned platform_cpu_hz(void) { return 125000000; }
struct mallinfo mallinfo(void) { return (struct mallinfo){.fordblks=100}; }
void *sbrk(intptr_t increment) { CHECK(!increment);return (void *)((uintptr_t)&__StackLimit-1024); }
uint64_t time_us_64(void) { return elapsed++; }
void adc_init(void) { initializations++; }
void adc_select_input(unsigned channel) { selected=channel; }
unsigned adc_get_selected_input(void) { return selected; }
void adc_set_temp_sensor_enabled(bool enabled) { temperature_enabled=enabled; }
void adc_gpio_init(unsigned pin) { CHECK(pin==29);gpio_reads++; }
void hw_set_bits(uint32_t *cs,uint32_t bits) {
    CHECK(cs==&test_adc.cs&&bits==ADC_CS_START_ONCE_BITS);
    conversions++;
    *cs=timeout?0:ADC_CS_READY_BITS|(fail?ADC_CS_ERR_BITS:0);
    test_adc.result=selected==4?876:2048;
}
int main(void) {
    double value=123;
    CHECK(!platform_metric_supported(PLATFORM_METRIC_WIFI_RSSI));
    CHECK(!platform_metric_read(PLATFORM_METRIC_WIFI_RSSI,&value));
    CHECK(!platform_metric_read(PLATFORM_METRIC_HEAP_MIN,&value));
    CHECK(!initializations&&!conversions&&value==123);
    CHECK(platform_metric_read(PLATFORM_METRIC_CPU_FREQUENCY,&value)&&value==125000000);
    CHECK(platform_metric_read(PLATFORM_METRIC_HEAP_FREE,&value)&&value==1124);
    CHECK(platform_metric_supported(PLATFORM_METRIC_SUPPLY_VOLTAGE)==!CYW43_USES_VSYS_PIN);
#if CYW43_USES_VSYS_PIN
    CHECK(!platform_metric_read(PLATFORM_METRIC_SUPPLY_VOLTAGE,&value));
    CHECK(!initializations&&!gpio_reads&&!conversions&&selected==2);
#else
    CHECK(platform_metric_read(PLATFORM_METRIC_SUPPLY_VOLTAGE,&value));
    CHECK(fabs(value-4.95)<0.0001&&gpio_reads==1&&conversions==8&&selected==2);
#endif
    CHECK(platform_metric_read(PLATFORM_METRIC_TEMPERATURE,&value));
    CHECK(value>26&&value<28&&!temperature_enabled&&selected==2&&initializations==1);
    fail=true;value=123;
    CHECK(!platform_metric_read(PLATFORM_METRIC_TEMPERATURE,&value));
    CHECK(value==123&&!temperature_enabled&&selected==2);
    fail=false;timeout=true;uint64_t before=elapsed;
    CHECK(!platform_metric_read(PLATFORM_METRIC_TEMPERATURE,&value));
    CHECK(elapsed-before<=102&&value==123&&!temperature_enabled&&selected==2);
    timeout=false;
    CHECK(platform_metric_read(PLATFORM_METRIC_TEMPERATURE,&value));
    CHECK(!temperature_enabled&&selected==2&&initializations==1);
    puts("Pico telemetry conversion, failure and shared-pin contracts passed");
}
