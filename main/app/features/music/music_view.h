#pragma once

#include "music_model.h"
#include "widgets/background_widget.h"
#include "widgets/cover_widget.h"
#include "widgets/visualizer_widget.h"
#include "lvgl.h"

class MusicView {
public:
    void create();
    void destroy();
    void render(const MusicDisplayState& state);
    bool renderDisplayCover();
    void renderCoverPlaceholder();
    void setDimmed(bool dimmed);

private:
    lv_obj_t* title_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    lv_obj_t* time_ = nullptr;
    lv_obj_t* play_pause_icon_ = nullptr;
    MusicDisplayState last_state_{};
    bool has_last_state_ = false;
    BackgroundWidget background_;
    CoverWidget cover_;
    VisualizerWidget visualizer_;
};
