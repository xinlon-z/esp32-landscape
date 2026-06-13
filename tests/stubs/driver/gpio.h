#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef int gpio_num_t;

static constexpr gpio_num_t GPIO_NUM_0 = 0;
static constexpr gpio_num_t GPIO_NUM_16 = 16;
static constexpr int GPIO_INTR_DISABLE = 0;
static constexpr int GPIO_MODE_INPUT = 1;
static constexpr int GPIO_PULLUP_ENABLE = 1;
static constexpr int GPIO_PULLDOWN_DISABLE = 0;

typedef struct {
    int intr_type;
    int mode;
    uint64_t pin_bit_mask;
    int pull_up_en;
    int pull_down_en;
} gpio_config_t;

static inline esp_err_t gpio_config(const gpio_config_t*)
{
    return ESP_OK;
}

static inline int gpio_get_level(gpio_num_t)
{
    return 0;
}
