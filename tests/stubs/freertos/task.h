#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"

typedef void* TaskHandle_t;

inline TickType_t& xTaskGetTickCountStubValue()
{
    static TickType_t value = 0;
    return value;
}

static inline TickType_t xTaskGetTickCount()
{
    return xTaskGetTickCountStubValue();
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
