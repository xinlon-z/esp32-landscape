#include <gtest/gtest.h>

#include "app/ui/fonts/clock_glyph_mask.h"

TEST(ClockGlyphMask, RemovesOnlyVeryLowAlphaEdgeNoise)
{
    EXPECT_EQ(clockGlyphMaskOpacity(0), 0);
    EXPECT_EQ(clockGlyphMaskOpacity(31), 0);
    EXPECT_EQ(clockGlyphMaskOpacity(32), 32);
    EXPECT_EQ(clockGlyphMaskOpacity(128), 128);
    EXPECT_EQ(clockGlyphMaskOpacity(255), 255);
}
