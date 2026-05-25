#pragma once

#include "music_state.h"

#include <stdint.h>

struct MusicDisplayState {
    char title[96] = "";
    char subtitle[200] = "";
    char time[24] = "";
    uint32_t progress_per_mille = 0;
    bool playing = false;
};

class MusicModel {
public:
    MusicDisplayState build(const MusicState& state, uint32_t elapsed_frames);
};
