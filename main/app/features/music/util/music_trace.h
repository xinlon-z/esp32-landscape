#pragma once

#ifndef CLOCK_TRACE_MUSIC
#define CLOCK_TRACE_MUSIC 0
#endif

#if CLOCK_TRACE_MUSIC
#include "esp_log.h"
#define MUSIC_TRACE_LOGI(...) ESP_LOGI(__VA_ARGS__)
#else
#define MUSIC_TRACE_LOGI(...) do {} while (0)
#endif
