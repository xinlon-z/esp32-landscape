#pragma once

#include "lvgl.h"

#include <stdint.h>

class VisualizerWidget {
public:
    static constexpr int kBarCount = 44;

    void create(lv_obj_t* parent);
    void render(uint32_t progress_per_mille, bool playing);
    void clear();

private:
    static void onAnimTimer(lv_timer_t* timer);
    void renderBars();

    lv_obj_t* bars_[kBarCount] = {};
    lv_timer_t* anim_timer_ = nullptr;
    uint32_t cached_progress_ = 0;
    bool cached_playing_ = false;
};
