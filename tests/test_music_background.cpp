#include "app/features/music/util/music_background.cpp"

#include <gtest/gtest.h>

#include <vector>

static uint8_t redOf(lv_color_t color)
{
    return static_cast<uint8_t>((lv_color_to32(color) >> 16) & 0xff);
}

static uint8_t blueOf(lv_color_t color)
{
    return static_cast<uint8_t>(lv_color_to32(color) & 0xff);
}

TEST(MusicBackground, BlurredBackground)
{
    constexpr uint16_t cover_w = 16;
    constexpr uint16_t cover_h = 16;
    constexpr uint16_t out_w = 64;
    constexpr uint16_t out_h = 16;

    std::vector<lv_color_t> cover(cover_w * cover_h);
    std::vector<lv_color_t> output(out_w * out_h);
    std::vector<lv_color_t> scratch(out_w * out_h);

    for (uint16_t y = 0; y < cover_h; ++y) {
        for (uint16_t x = 0; x < cover_w; ++x) {
            cover[y * cover_w + x] = x < cover_w / 2
                                         ? lv_color_make(240, 20, 30)
                                         : lv_color_make(20, 40, 240);
        }
    }

    if (!musicGenerateBlurredBackground(cover.data(), cover_w, cover_h,
                                        output.data(), out_w, out_h, scratch.data())) {
        FAIL() << "background generation failed";
    }

    const lv_color_t left = output[out_h / 2 * out_w + 2];
    const lv_color_t center = output[out_h / 2 * out_w + out_w / 2];
    const lv_color_t right = output[out_h / 2 * out_w + out_w - 3];

    if (redOf(left) <= blueOf(left)) {
        FAIL() << "expected left side to keep red cover influence";
    }
    if (blueOf(right) <= redOf(right)) {
        FAIL() << "expected right side to keep blue cover influence";
    }
    if (redOf(center) == 0 || blueOf(center) == 0) {
        FAIL() << "expected blurred center to contain mixed color";
    }
}
