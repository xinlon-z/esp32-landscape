#pragma once

#include "esp_err.h"

extern "C" esp_err_t adc_get_value(float* value, int* data);
