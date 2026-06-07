#include <gtest/gtest.h>

// Include the full service so pickJpegScale (anonymous-namespace) is visible.
#define private public
#define protected public
#include "app/services/cover_service.cpp"
#include "app/core/event/event_bus.cpp"
#undef private
#undef protected

namespace MusicMqtt {
void init() {}
} // namespace MusicMqtt

// pickJpegScale(w, h) must return the largest scale where both decoded
// dimensions are still >= kCoverSize (144), so that resampleCoverToSquare
// always downsamples rather than upsampling.

TEST(CoverScaleSelection, SmallImageBelowCoverSizeUsesScale0)
{
    // Already smaller than kCoverSize — scale 0 is the only non-degrading choice.
    EXPECT_EQ(pickJpegScale(128, 128), 0u);
    EXPECT_EQ(pickJpegScale(100, 200), 0u);
}

TEST(CoverScaleSelection, ImageExactlyCoverSizeUsesScale0)
{
    EXPECT_EQ(pickJpegScale(144, 144), 0u);
}

TEST(CoverScaleSelection, Image512x512UsesScale1)
{
    // 512>>1=256 >= 144, 512>>2=128 < 144 → optimal scale is 1.
    EXPECT_EQ(pickJpegScale(512, 512), 1u);
}

TEST(CoverScaleSelection, Image1024x1024UsesScale2)
{
    // 1024>>2=256 >= 144, 1024>>3=128 < 144 → optimal scale is 2.
    EXPECT_EQ(pickJpegScale(1024, 1024), 2u);
}

TEST(CoverScaleSelection, Image1152x1152UsesScale3)
{
    // 1152>>3=144 >= 144 → scale 3 still keeps both dims at the boundary.
    EXPECT_EQ(pickJpegScale(1152, 1152), 3u);
}

TEST(CoverScaleSelection, DecodedSizeIsAtLeastCoverSizeWhenOriginalIsLarger)
{
    // For any image whose side length is >= kCoverSize, the decoded size must
    // also be >= kCoverSize so that resample is always a downsample.
    for (uint16_t side : {144, 256, 288, 512, 1024, 2048}) {
        uint8_t s = pickJpegScale(side, side);
        uint16_t decoded = side >> s;
        EXPECT_GE(decoded, 144u) << "side=" << side << " scale=" << static_cast<int>(s)
                                 << " decoded=" << decoded;
    }
}

TEST(CoverScaleSelection, NonSquareImageLimitedBySmallestDimension)
{
    // 1024x256: 256>>1=128 < 144, so both dims can't clear kCoverSize at scale 1.
    // Scale must be 0 so the 256-side stays at 256.
    EXPECT_EQ(pickJpegScale(1024, 256), 0u);
}
