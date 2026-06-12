#pragma once

#include "lvgl.h"

struct ClockFontVisualMetrics {
    int line_height = 0;
    int digit_top = 0;
    int digit_bottom = 0;
};

const lv_font_t* clockTimeFont();
ClockFontVisualMetrics clockTimeFontMetrics();
