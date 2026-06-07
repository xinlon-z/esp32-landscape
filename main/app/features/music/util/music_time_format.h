#pragma once

#include <stdint.h>
#include <stdio.h>

inline void formatMusicTimeDisplay(uint32_t elapsed_seconds,
                                   uint32_t duration_seconds,
                                   char* out,
                                   size_t out_size)
{
    snprintf(out, out_size, "%lu:%02lu/%lu:%02lu",
             static_cast<unsigned long>(elapsed_seconds / 60u),
             static_cast<unsigned long>(elapsed_seconds % 60u),
             static_cast<unsigned long>(duration_seconds / 60u),
             static_cast<unsigned long>(duration_seconds % 60u));
}
