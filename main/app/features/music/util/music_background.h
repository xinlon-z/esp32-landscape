#pragma once

#include <stdint.h>

#include "lvgl.h"

bool musicGenerateBlurredBackground(const lv_color_t* cover, uint16_t cover_w, uint16_t cover_h,
                                    lv_color_t* output, uint16_t output_w, uint16_t output_h,
                                    lv_color_t* scratch);
