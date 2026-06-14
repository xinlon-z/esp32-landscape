#include <gtest/gtest.h>

#include "app/ui/fonts/clock_glyph_mask.h"

TEST(ClockGlyphMask, RemovesOnlyVeryLowAlphaEdgeNoise)
{
    EXPECT_EQ(clockGlyphMaskOpacity(0), 0);
    EXPECT_EQ(clockGlyphMaskOpacity(3), 0);
    EXPECT_EQ(clockGlyphMaskOpacity(4), 4);
    EXPECT_EQ(clockGlyphMaskOpacity(31), 31);
    EXPECT_EQ(clockGlyphMaskOpacity(128), 128);
    EXPECT_EQ(clockGlyphMaskOpacity(255), 255);
}
