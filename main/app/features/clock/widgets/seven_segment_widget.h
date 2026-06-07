#pragma once

#include "lvgl.h"

class SevenSegmentWidget {
public:
    void bind(lv_obj_t* segments[7]);
    void render(char value, bool dimmed);

private:
    lv_obj_t* segments_[7] = {};
};
