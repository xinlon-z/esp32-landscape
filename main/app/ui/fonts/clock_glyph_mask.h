#pragma once

#include <stdint.h>

constexpr uint8_t kClockGlyphMaskMinVisibleAlpha = 4;

inline uint8_t clockGlyphMaskOpacity(uint8_t alpha)
{
    return alpha < kClockGlyphMaskMinVisibleAlpha ? 0 : alpha;
}
