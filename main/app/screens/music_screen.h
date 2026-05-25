#pragma once

#include "app/features/music/music_presenter.h"
#include "app/features/music/music_view.h"
#include "screen.h"

class MusicScreen : public app::Screen {
public:
    MusicScreen();
    void onEnter() override;
    void onExit() override;
    void onTick() override;

private:
    MusicView view_;
    MusicPresenter presenter_;
};
