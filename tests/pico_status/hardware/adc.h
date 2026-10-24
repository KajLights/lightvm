#ifndef TEST_PICO_STATUS_ADC_H
#define TEST_PICO_STATUS_ADC_H
#include <stdint.h>
#include <stdbool.h>
typedef struct { uint32_t cs, result; } TestAdc;
extern TestAdc test_adc;
#define adc_hw (&test_adc)
#define ADC_CS_START_ONCE_BITS 1u
#define ADC_CS_READY_BITS 2u
#define ADC_CS_ERR_BITS 4u
void adc_init(void);
void adc_select_input(unsigned);
unsigned adc_get_selected_input(void);
void adc_set_temp_sensor_enabled(bool);
void adc_gpio_init(unsigned);
void hw_set_bits(uint32_t *,uint32_t);
#endif
