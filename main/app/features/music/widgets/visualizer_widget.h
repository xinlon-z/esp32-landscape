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
    lv_obj_t* bars_[kBarCount] = {};
};
