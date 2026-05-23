#include "../main/music_visualizer.cpp"

#include <stdio.h>

int main()
{
    constexpr uint8_t kBars = 34;
    uint32_t playing_sum = 0;
    uint32_t paused_sum = 0;

    for (uint8_t i = 0; i < kBars; ++i) {
        const uint8_t playing = musicVisualizerBarHeight(i, kBars, 420, true);
        const uint8_t paused = musicVisualizerBarHeight(i, kBars, 420, false);
        if (playing < 4 || playing > 30 || paused < 4 || paused > 30) {
            printf("bar %u out of range: playing=%u paused=%u\n", i, playing, paused);
            return 1;
        }
        playing_sum += playing;
        paused_sum += paused;
    }

    if (playing_sum <= paused_sum) {
        printf("expected playing visualizer to be more energetic: %u <= %u\n",
               static_cast<unsigned>(playing_sum),
               static_cast<unsigned>(paused_sum));
        return 1;
    }

    if (musicVisualizerBarHeight(0, 0, 0, true) != 0) {
        printf("expected zero bars to return zero height\n");
        return 1;
    }

    return 0;
}
