#pragma once

typedef unsigned int TickType_t;
typedef unsigned int UBaseType_t;

#define pdMS_TO_TICKS(ms) (ms)
#define portTICK_PERIOD_MS 1
#define portMAX_DELAY ((TickType_t)0xffffffffu)
