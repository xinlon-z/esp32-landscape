#include "app/features/music/util/music_visualizer.cpp"

#include <gtest/gtest.h>

TEST(MusicVisualizer, BarHeightRange)
{
    constexpr uint8_t kBars = 34;
    uint32_t playing_sum = 0;
    uint32_t paused_sum = 0;

    for (uint8_t i = 0; i < kBars; ++i) {
        const uint8_t playing = musicVisualizerBarHeight(i, kBars, 420, true);
        const uint8_t paused = musicVisualizerBarHeight(i, kBars, 420, false);
        if (playing < 4 || playing > 30 || paused < 4 || paused > 30) {
            FAIL() << "bar " << i << " out of range: playing=" << playing << " paused=" << paused;
        }
        playing_sum += playing;
        paused_sum += paused;
    }

    EXPECT_GT(playing_sum, paused_sum);
    EXPECT_EQ(musicVisualizerBarHeight(0, 0, 0, true), 0);
}
