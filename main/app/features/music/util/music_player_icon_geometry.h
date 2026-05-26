#pragma once

struct MusicIconOffset {
    int x;
    int y;
};

constexpr MusicIconOffset musicPlayPauseIconOffset(bool playing)
{
    return playing ? MusicIconOffset{0, 0} : MusicIconOffset{2, 0};
}
