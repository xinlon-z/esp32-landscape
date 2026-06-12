#pragma once

typedef unsigned int TickType_t;
typedef unsigned int UBaseType_t;
typedef int BaseType_t;

#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define portTICK_PERIOD_MS 1
#define portMAX_DELAY ((TickType_t)0xffffffffu)
