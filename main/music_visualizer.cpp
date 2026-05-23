#include "music_visualizer.h"

uint8_t musicVisualizerBarHeight(uint8_t index, uint8_t count, uint32_t progress_permille, bool playing)
{
    if (count == 0) {
        return 0;
    }

    if (progress_permille > 1000u) {
        progress_permille = 1000u;
    }

    const uint8_t center = static_cast<uint8_t>(count / 2u);
    const uint8_t distance = index > center ? static_cast<uint8_t>(index - center)
                                            : static_cast<uint8_t>(center - index);
    const uint8_t envelope = static_cast<uint8_t>(distance > 18 ? 0 : 18 - distance);
    const uint8_t phase = static_cast<uint8_t>((index * 7u + progress_permille / 37u) % 13u);
    const uint8_t texture = static_cast<uint8_t>((phase * phase + index * 3u) % 17u);
    const uint8_t motion = playing ? texture : static_cast<uint8_t>(texture / 3u);

    uint8_t height = static_cast<uint8_t>(4u + (envelope * 3u) / 4u + motion / 2u);
    if (index > (count * 3u) / 5u) {
        height = static_cast<uint8_t>((height * 2u) / 3u);
    }
    if (height < 4u) {
        height = 4u;
    }
    if (height > 30u) {
        height = 30u;
    }
    return height;
}
