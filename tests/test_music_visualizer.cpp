#include "app/features/music/util/music_visualizer.cpp"

#include <gtest/gtest.h>

namespace {
constexpr uint8_t kBars = 34;
} // namespace

TEST(MusicVisualizer, HeightStaysInRangeAcrossPhases)
{
    for (uint32_t phase = 0; phase < 5000u; phase += 73u) {
        for (uint8_t i = 0; i < kBars; ++i) {
            const uint8_t h_play = musicVisualizerBarHeight(i, kBars, 420, true, phase);
            const uint8_t h_paused = musicVisualizerBarHeight(i, kBars, 420, false, phase);
            EXPECT_GE(h_play, 4u);
            EXPECT_LE(h_play, 30u);
            EXPECT_GE(h_paused, 4u);
            EXPECT_LE(h_paused, 30u);
        }
    }
}

TEST(MusicVisualizer, PausedHeightIsPhaseInvariant)
{
    // When not playing, the bar height is the static envelope only — must
    // not vary with phase_ms so paused bars stay still.
    for (uint8_t i = 0; i < kBars; ++i) {
        const uint8_t h_a = musicVisualizerBarHeight(i, kBars, 420, false, 0);
        const uint8_t h_b = musicVisualizerBarHeight(i, kBars, 420, false, 1234);
        const uint8_t h_c = musicVisualizerBarHeight(i, kBars, 420, false, 60000);
        EXPECT_EQ(h_a, h_b) << "bar " << static_cast<int>(i);
        EXPECT_EQ(h_a, h_c) << "bar " << static_cast<int>(i);
    }
}

TEST(MusicVisualizer, PlayingVariesAcrossPhases)
{
    // The bouncy-peaks animation means at least one bar's height must change
    // as phase_ms advances. (The exact bar isn't promised — depends on the
    // per-bar period and target hash.)
    bool any_change = false;
    for (uint8_t i = 0; i < kBars && !any_change; ++i) {
        for (uint32_t phase = 0; phase < 1000u && !any_change; phase += 50u) {
            const uint8_t h_a = musicVisualizerBarHeight(i, kBars, 420, true, phase);
            const uint8_t h_b = musicVisualizerBarHeight(i, kBars, 420, true, phase + 50u);
            if (h_a != h_b) {
                any_change = true;
            }
        }
    }
    EXPECT_TRUE(any_change);
}

TEST(MusicVisualizer, PlayingTallerThanPausedOnAverage)
{
    // Bouncy peaks add amplitude on top of the static envelope, so over many
    // (bar, phase) samples the playing total must exceed the paused total.
    uint32_t playing_sum = 0;
    uint32_t paused_sum = 0;
    for (uint32_t phase = 0; phase < 3000u; phase += 53u) {
        for (uint8_t i = 0; i < kBars; ++i) {
            playing_sum += musicVisualizerBarHeight(i, kBars, 420, true, phase);
            paused_sum  += musicVisualizerBarHeight(i, kBars, 420, false, phase);
        }
    }
    EXPECT_GT(playing_sum, paused_sum);
}

TEST(MusicVisualizer, ZeroCountReturnsZero)
{
    EXPECT_EQ(musicVisualizerBarHeight(0, 0, 0, true, 100), 0);
    EXPECT_EQ(musicVisualizerBarHeight(0, 0, 0, false, 0), 0);
}
