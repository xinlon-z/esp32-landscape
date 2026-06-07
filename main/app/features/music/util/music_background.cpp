#include "music_background.h"

#include "music_trace.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

namespace {

#if CLOCK_TRACE_MUSIC
constexpr const char* kMusicBgTag = "music_bg";
#endif
constexpr uint16_t kTransposeTile  = 16;
constexpr uint16_t kDownsampleMinW = 128;
constexpr uint16_t kDownsampleMinH = 64;

#if CLOCK_TRACE_MUSIC
inline uint32_t traceMs()
{
    return static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
}
#endif

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
                const uint32_t rgb = lv_color_to32(src[y * w + right]);
                sum_r += (rgb >> 16) & 0xff;
                sum_g += (rgb >> 8)  & 0xff;
                sum_b += rgb         & 0xff;
                ++right;
            }
            while (left < next_left) {
                const uint32_t rgb = lv_color_to32(src[y * w + left]);
                sum_r -= (rgb >> 16) & 0xff;
                sum_g -= (rgb >> 8)  & 0xff;
                sum_b -= rgb         & 0xff;
                ++left;
            }

            const uint16_t count = static_cast<uint16_t>(right - left);
            dst[y * w + x] = lv_color_make(static_cast<uint8_t>(sum_r / count),
                                           static_cast<uint8_t>(sum_g / count),
                                           static_cast<uint8_t>(sum_b / count));
        }
    }
}

// Pure 2D transpose with 16x16 tile blocking. dst gets src's dimensions
// swapped, so dst[col*h + row] = src[row*w + col]. Cache-friendly: each tile
// is staged through a 256-pixel local buffer in IRAM.
void transposeTiled(const lv_color_t* src, lv_color_t* dst, uint16_t w, uint16_t h)
{
    lv_color_t local[kTransposeTile * kTransposeTile];

    for (uint16_t rt = 0; rt < h; rt += kTransposeTile) {
        const uint16_t rows = (rt + kTransposeTile > h)
                                  ? static_cast<uint16_t>(h - rt) : kTransposeTile;
        for (uint16_t ct = 0; ct < w; ct += kTransposeTile) {
            const uint16_t cols = (ct + kTransposeTile > w)
                                      ? static_cast<uint16_t>(w - ct) : kTransposeTile;

            for (uint16_t r = 0; r < rows; ++r) {
                memcpy(&local[r * kTransposeTile],
                       &src[(rt + r) * w + ct],
                       cols * sizeof(lv_color_t));
            }

            for (uint16_t c = 0; c < cols; ++c) {
                lv_color_t* out_col = &dst[(ct + c) * h + rt];
                for (uint16_t r = 0; r < rows; ++r) {
                    out_col[r] = local[r * kTransposeTile + c];
                }
            }
        }
    }
}

void upscaleNearest2x(const lv_color_t* src, uint16_t src_w, uint16_t src_h,
                      lv_color_t* dst, uint16_t dst_w, uint16_t dst_h)
{
    for (uint16_t Y = 0; Y < dst_h; ++Y) {
        const lv_color_t* sy = &src[(Y >> 1) * src_w];
        lv_color_t* dy = &dst[Y * dst_w];
        for (uint16_t X = 0; X < dst_w; ++X) {
            dy[X] = sy[X >> 1];
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

// Three rounds of separable H+V blur using the transpose trick: each "vertical"
// pass becomes a transpose + horizontal blur + transpose-back. The horizontal
// blur is cache-friendly (sequential reads); the strided vertical reads that
// dominate the original implementation never happen.
//
// Mathematically equivalent to 3x alternating H,V on the input buffer. The
// caller's `out` buffer holds the final blurred image.
void blurThreePassesTransposed(lv_color_t* out, lv_color_t* tmp,
                               uint16_t w, uint16_t h, uint16_t radius)
{
    for (int i = 0; i < 3; ++i) {
        blurHorizontal(out, tmp, w, h, radius);
        transposeTiled(tmp, out, w, h);
        blurHorizontal(out, tmp, h, w, radius);
        transposeTiled(tmp, out, h, w);
    }
}

} // namespace

bool musicGenerateBlurredBackground(const lv_color_t* cover, uint16_t cover_w, uint16_t cover_h,
                                    lv_color_t* output, uint16_t output_w, uint16_t output_h,
                                    lv_color_t* scratch)
{
    if (!cover || !output || !scratch || cover_w == 0 || cover_h == 0 ||
        output_w == 0 || output_h == 0) {
        return false;
    }

    const bool downsample = (output_w >= kDownsampleMinW) &&
                            (output_h >= kDownsampleMinH) &&
                            ((output_w & 1u) == 0u) && ((output_h & 1u) == 0u);

    if (downsample) {
        // Render the cover at half resolution, blur there (1/4 the data, ~4x
        // less SPIRAM bandwidth), then upscale 2x back to full resolution.
        // The blur is heavy enough (radius ~9% of width) that the high-frequency
        // detail lost by 2x downsample is invisible in the output.
        const uint16_t ds_w = static_cast<uint16_t>(output_w >> 1);
        const uint16_t ds_h = static_cast<uint16_t>(output_h >> 1);

#if CLOCK_TRACE_MUSIC
        const uint32_t t0 = traceMs();
#endif
        renderCoverFill(cover, cover_w, cover_h, output, ds_w, ds_h);
#if CLOCK_TRACE_MUSIC
        const uint32_t t1 = traceMs();
#endif

        const uint16_t radius = blurRadiusFor(ds_w, ds_h);
        blurThreePassesTransposed(output, scratch, ds_w, ds_h, radius);
#if CLOCK_TRACE_MUSIC
        const uint32_t t2 = traceMs();
#endif

        upscaleNearest2x(output, ds_w, ds_h, scratch, output_w, output_h);
        memcpy(output, scratch, static_cast<size_t>(output_w) * output_h * sizeof(lv_color_t));
#if CLOCK_TRACE_MUSIC
        const uint32_t t3 = traceMs();

        MUSIC_TRACE_LOGI(kMusicBgTag,
                         "[trace] blur_internal(ds): fill=%u ms, blur=%u ms, "
                         "upscale+memcpy=%u ms, radius=%u, ds=%ux%u, full=%ux%u",
                         t1 - t0, t2 - t1, t3 - t2, radius, ds_w, ds_h, output_w, output_h);
#endif
    } else {
#if CLOCK_TRACE_MUSIC
        const uint32_t t0 = traceMs();
#endif
        renderCoverFill(cover, cover_w, cover_h, output, output_w, output_h);
#if CLOCK_TRACE_MUSIC
        const uint32_t t1 = traceMs();
#endif

        const uint16_t radius = blurRadiusFor(output_w, output_h);
        blurThreePassesTransposed(output, scratch, output_w, output_h, radius);
#if CLOCK_TRACE_MUSIC
        const uint32_t t2 = traceMs();

        MUSIC_TRACE_LOGI(kMusicBgTag,
                         "[trace] blur_internal(full): fill=%u ms, blur=%u ms, "
                         "radius=%u, %ux%u",
                         t1 - t0, t2 - t1, radius, output_w, output_h);
#endif
    }

    return true;
}
