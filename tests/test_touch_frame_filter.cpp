#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "platform/touch_frame_filter.h"

namespace {

std::array<uint8_t, 8> frame(uint8_t points, uint16_t x, uint16_t y)
{
    return {
        0x00,
        points,
        static_cast<uint8_t>((x >> 8) & 0x0f),
        static_cast<uint8_t>(x & 0xff),
        static_cast<uint8_t>((y >> 8) & 0x0f),
        static_cast<uint8_t>(y & 0xff),
        0x00,
        0x00,
    };
}

} // namespace

TEST(TouchFrameFilter, IgnoresIsolatedValidFrame)
{
    TouchFrameFilter filter;

    const TouchFrameResult result = filter.process(frame(1, 0x120, 0x080).data());

    EXPECT_EQ(result.state, TouchFrameState::Candidate);
    EXPECT_EQ(result.x, 0x120);
    EXPECT_EQ(result.y, 0x080);
}

TEST(TouchFrameFilter, PromotesSecondConsecutiveValidFrameToPressed)
{
    TouchFrameFilter filter;
    filter.process(frame(1, 0x120, 0x080).data());

    const TouchFrameResult result = filter.process(frame(1, 0x110, 0x090).data());

    EXPECT_EQ(result.state, TouchFrameState::Pressed);
    EXPECT_EQ(result.x, 0x110);
    EXPECT_EQ(result.y, 0x090);
}

TEST(TouchFrameFilter, KeepsPressedForContinuousValidFrames)
{
    TouchFrameFilter filter;
    filter.process(frame(1, 0x120, 0x080).data());
    filter.process(frame(1, 0x110, 0x090).data());

    const TouchFrameResult result = filter.process(frame(1, 0x100, 0x0a0).data());

    EXPECT_EQ(result.state, TouchFrameState::Pressed);
    EXPECT_EQ(result.x, 0x100);
    EXPECT_EQ(result.y, 0x0a0);
}

TEST(TouchFrameFilter, ReleasesAndResetsOnInvalidPointCount)
{
    TouchFrameFilter filter;
    filter.process(frame(1, 0x120, 0x080).data());
    filter.process(frame(1, 0x110, 0x090).data());

    const TouchFrameResult release = filter.process(frame(0, 0x110, 0x090).data());
    const TouchFrameResult next = filter.process(frame(1, 0x100, 0x0a0).data());

    EXPECT_EQ(release.state, TouchFrameState::Released);
    EXPECT_EQ(next.state, TouchFrameState::Candidate);
}

TEST(TouchFrameFilter, ResetClearsActiveTouch)
{
    TouchFrameFilter filter;
    filter.process(frame(1, 0x120, 0x080).data());
    filter.process(frame(1, 0x110, 0x090).data());

    filter.reset();
    const TouchFrameResult result = filter.process(frame(1, 0x100, 0x0a0).data());

    EXPECT_EQ(result.state, TouchFrameState::Candidate);
}

