#pragma once

#include "music_player_screen.h"
#include "screen.h"

class MusicScreen : public app::Screen {
public:
    void onEnter() override;
    void onExit() override;
    void onTick() override;

private:
    MusicPlayerScreen legacy_;
};
