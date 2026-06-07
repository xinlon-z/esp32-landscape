#ifndef ADC_BSP_H
#define ADC_BSP_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void adc_bsp_init(void);
void adc_example(void* parmeter);
esp_err_t adc_get_value(float *value,int *data);

#ifdef __cplusplus
}
#endif

#endif
