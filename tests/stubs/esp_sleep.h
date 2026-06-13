#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef enum {
    ESP_EXT1_WAKEUP_ANY_HIGH = 0,
    ESP_EXT1_WAKEUP_ANY_LOW = 1,
} esp_sleep_ext1_wakeup_mode_t;

inline uint64_t& espSleepExt1WakeMask()
{
    static uint64_t value = 0;
    return value;
}

inline esp_sleep_ext1_wakeup_mode_t& espSleepExt1WakeMode()
{
    static esp_sleep_ext1_wakeup_mode_t value = ESP_EXT1_WAKEUP_ANY_HIGH;
    return value;
}

inline int& espDeepSleepStartCount()
{
    static int value = 0;
    return value;
}

inline esp_err_t& espSleepEnableExt1Result()
{
    static esp_err_t value = ESP_OK;
    return value;
}

inline void espSleepReset()
{
    espSleepExt1WakeMask() = 0;
    espSleepExt1WakeMode() = ESP_EXT1_WAKEUP_ANY_HIGH;
    espDeepSleepStartCount() = 0;
    espSleepEnableExt1Result() = ESP_OK;
}

static inline esp_err_t esp_sleep_enable_ext1_wakeup_io(
    uint64_t io_mask,
    esp_sleep_ext1_wakeup_mode_t level_mode)
{
    espSleepExt1WakeMask() = io_mask;
    espSleepExt1WakeMode() = level_mode;
    return espSleepEnableExt1Result();
}

static inline void esp_deep_sleep_start()
{
    ++espDeepSleepStartCount();
}
