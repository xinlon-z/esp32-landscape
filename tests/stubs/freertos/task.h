#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"

typedef void* TaskHandle_t;

static inline TickType_t xTaskGetTickCount()
{
    return 0;
}

static inline void vTaskDelay(TickType_t)
{
}

static inline int xTaskCreatePinnedToCore(
    void (*)(void*),
    const char*,
    uint32_t,
    void*,
    unsigned,
    TaskHandle_t*,
    int)
{
    return 0;
}
