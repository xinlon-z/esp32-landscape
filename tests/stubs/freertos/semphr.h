#pragma once

#include "freertos/FreeRTOS.h"

typedef void* SemaphoreHandle_t;

#define pdTRUE 1

static inline SemaphoreHandle_t xSemaphoreCreateMutex()
{
    return reinterpret_cast<SemaphoreHandle_t>(1);
}

static inline int xSemaphoreTake(SemaphoreHandle_t, TickType_t)
{
    return pdTRUE;
}

static inline void xSemaphoreGive(SemaphoreHandle_t)
{
}
