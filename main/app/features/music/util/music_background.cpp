#include "music_background.h"

namespace {

uint8_t colorR(lv_color_t color)
{
    return static_cast<uint8_t>((lv_color_to32(color) >> 16) & 0xff);
}

uint8_t colorG(lv_color_t color)
{
    return static_cast<uint8_t>((lv_color_to32(color) >> 8) & 0xff);
}

uint8_t colorB(lv_color_t color)
{
    return static_cast<uint8_t>(lv_color_to32(color) & 0xff);
}

void renderCoverFill(const lv_color_t* cover, uint16_t cover_w, uint16_t cover_h,
                     lv_color_t* output, uint16_t output_w, uint16_t output_h)
{
    uint16_t crop_x = 0;
    uint16_t crop_y = 0;
    uint16_t crop_w = cover_w;
    uint16_t crop_h = cover_h;

    if (static_cast<uint32_t>(output_w) * cover_h >= static_cast<uint32_t>(output_h) * cover_w) {
        crop_h = static_cast<uint16_t>((static_cast<uint32_t>(cover_w) * output_h) / output_w);
        if (crop_h == 0) {
            crop_h = 1;
        }
        crop_y = static_cast<uint16_t>((cover_h - crop_h) / 2u);
    } else {
        crop_w = static_cast<uint16_t>((static_cast<uint32_t>(cover_h) * output_w) / output_h);
        if (crop_w == 0) {
            crop_w = 1;
        }
        crop_x = static_cast<uint16_t>((cover_w - crop_w) / 2u);
    }

    for (uint16_t y = 0; y < output_h; ++y) {
        uint16_t sy = static_cast<uint16_t>(crop_y + (static_cast<uint32_t>(y) * crop_h) / output_h);
        if (sy >= cover_h) {
            sy = static_cast<uint16_t>(cover_h - 1);
        }
        for (uint16_t x = 0; x < output_w; ++x) {
            uint16_t sx = static_cast<uint16_t>(crop_x + (static_cast<uint32_t>(x) * crop_w) / output_w);
            if (sx >= cover_w) {
                sx = static_cast<uint16_t>(cover_w - 1);
            }
            output[y * output_w + x] = cover[sy * cover_w + sx];
        }
    }
}

void blurHorizontal(const lv_color_t* src, lv_color_t* dst, uint16_t w, uint16_t h, uint16_t radius)
{
    for (uint16_t y = 0; y < h; ++y) {
        uint32_t sum_r = 0;
        uint32_t sum_g = 0;
        uint32_t sum_b = 0;
        uint16_t left = 0;
        uint16_t right = 0;

        for (uint16_t x = 0; x < w; ++x) {
            const uint16_t next_left = x > radius ? static_cast<uint16_t>(x - radius) : 0;
            uint16_t next_right = static_cast<uint16_t>(x + radius);
            if (next_right >= w) {
                next_right = static_cast<uint16_t>(w - 1);
            }

            while (right <= next_right) {
                const lv_color_t color = src[y * w + right];
                sum_r += colorR(color);
                sum_g += colorG(color);
                sum_b += colorB(color);
                ++right;
            }
            while (left < next_left) {
                const lv_color_t color = src[y * w + left];
                sum_r -= colorR(color);
                sum_g -= colorG(color);
                sum_b -= colorB(color);
                ++left;
            }

            const uint16_t count = static_cast<uint16_t>(right - left);
            dst[y * w + x] = lv_color_make(static_cast<uint8_t>(sum_r / count),
                                           static_cast<uint8_t>(sum_g / count),
                                           static_cast<uint8_t>(sum_b / count));
        }
    }
}

void blurVertical(const lv_color_t* src, lv_color_t* dst, uint16_t w, uint16_t h, uint16_t radius)
{
    for (uint16_t x = 0; x < w; ++x) {
        uint32_t sum_r = 0;
        uint32_t sum_g = 0;
        uint32_t sum_b = 0;
        uint16_t top = 0;
        uint16_t bottom = 0;

        for (uint16_t y = 0; y < h; ++y) {
            const uint16_t next_top = y > radius ? static_cast<uint16_t>(y - radius) : 0;
            uint16_t next_bottom = static_cast<uint16_t>(y + radius);
            if (next_bottom >= h) {
                next_bottom = static_cast<uint16_t>(h - 1);
            }

            while (bottom <= next_bottom) {
                const lv_color_t color = src[bottom * w + x];
                sum_r += colorR(color);
                sum_g += colorG(color);
                sum_b += colorB(color);
                ++bottom;
            }
            while (top < next_top) {
                const lv_color_t color = src[top * w + x];
                sum_r -= colorR(color);
                sum_g -= colorG(color);
                sum_b -= colorB(color);
                ++top;
            }

            const uint16_t count = static_cast<uint16_t>(bottom - top);
            dst[y * w + x] = lv_color_make(static_cast<uint8_t>(sum_r / count),
                                           static_cast<uint8_t>(sum_g / count),
                                           static_cast<uint8_t>(sum_b / count));
        }
    }
}

uint16_t blurRadiusFor(uint16_t w, uint16_t h)
{
    const uint16_t base = w < h ? w : h;
    uint16_t radius = static_cast<uint16_t>(base / 6u);
    if (radius < 2) {
        radius = 2;
    }
    if (radius > 28) {
        radius = 28;
    }
    return radius;
}

} // namespace

bool musicGenerateBlurredBackground(const lv_color_t* cover, uint16_t cover_w, uint16_t cover_h,
                                    lv_color_t* output, uint16_t output_w, uint16_t output_h,
                                    lv_color_t* scratch)
{
    if (!cover || !output || !scratch || cover_w == 0 || cover_h == 0 || output_w == 0 || output_h == 0) {
        return false;
    }

    renderCoverFill(cover, cover_w, cover_h, output, output_w, output_h);

    const uint16_t radius = blurRadiusFor(output_w, output_h);
    for (uint8_t i = 0; i < 3; ++i) {
        blurHorizontal(output, scratch, output_w, output_h, radius);
        blurVertical(scratch, output, output_w, output_h, radius);
    }

    return true;
}
