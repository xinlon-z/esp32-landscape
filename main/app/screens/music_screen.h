#pragma once

#include "app/features/music/music_presenter.h"
#include "app/features/music/music_view.h"
#include "app/screens/screen.h"
#include "lvgl.h"

class MusicScreen : public app::Screen {
public:
    MusicScreen();
    void onEnter() override;
    void onExit() override;
    void onTick() override;

private:
    static void onFastTimer(lv_timer_t* timer);

    MusicView view_;
    MusicPresenter presenter_;
    lv_timer_t* fast_timer_ = nullptr;
};
