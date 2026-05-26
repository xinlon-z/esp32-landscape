#pragma once

#include "app/features/music/music_state.h"

namespace MusicMqtt {

struct CoverImage {
    uint8_t* data = nullptr;
    uint32_t size = 0;
};

void init();
bool getState(MusicState* state);
bool takeCover(CoverImage* cover);


} // namespace MusicMqtt
