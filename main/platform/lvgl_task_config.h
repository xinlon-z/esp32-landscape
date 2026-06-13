#pragma once

#include <stdint.h>

#ifndef LVGL_TASK_STACK_BYTES
#define LVGL_TASK_STACK_BYTES (12u * 1024u)
#endif

#ifndef LVGL_STACK_WARN_BYTES
#define LVGL_STACK_WARN_BYTES (2u * 1024u)
#endif

constexpr uint32_t lvglTaskStackBytes()
{
    return LVGL_TASK_STACK_BYTES;
}

constexpr uint32_t lvglStackWarnBytes()
{
    return LVGL_STACK_WARN_BYTES;
}

