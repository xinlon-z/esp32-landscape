#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

static inline esp_err_t i2c_exio_set_output(uint8_t, bool)
{
    return ESP_OK;
}
