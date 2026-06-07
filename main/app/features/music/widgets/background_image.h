#pragma once

#include "lvgl.h"

#include <stdint.h>

struct MusicBackgroundImage {
    lv_img_dsc_t image{};
    lv_color_t* pixels = nullptr;
};

void musicReleaseBackgroundImage(MusicBackgroundImage* background);
