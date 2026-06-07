#pragma once

#include "freertos/FreeRTOS.h"

typedef void* SemaphoreHandle_t;
typedef int BaseType_t;

#define pdTRUE 1
#define pdFALSE 0

static inline SemaphoreHandle_t xSemaphoreCreateMutex()
{
    return reinterpret_cast<SemaphoreHandle_t>(1);
}

static inline SemaphoreHandle_t xSemaphoreCreateBinary()
{
    return reinterpret_cast<SemaphoreHandle_t>(2);
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t)
{
    return pdTRUE;
}

static inline void xSemaphoreGive(SemaphoreHandle_t)
{
}
