#include "visualizer_widget.h"

#include "../../../../music_visualizer.h"

namespace {
constexpr int kSpectrumX = 214;
constexpr int kSpectrumY = 112;
constexpr int kSpectrumBarW = 4;
constexpr int kSpectrumGap = 3;

void clearStyle(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void setBg(lv_obj_t* obj, uint32_t color, lv_opa_t opa)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
}
} // namespace

void VisualizerWidget::create(lv_obj_t* parent)
{
    for (int i = 0; i < kBarCount; ++i) {
        lv_obj_t* bar = lv_obj_create(parent);
        lv_obj_set_size(bar, kSpectrumBarW, 8);
        lv_obj_set_pos(bar, kSpectrumX + i * (kSpectrumBarW + kSpectrumGap), kSpectrumY - 8);
        clearStyle(bar);
        lv_obj_set_style_radius(bar, 1, 0);
        setBg(bar, 0xffffff, i < 18 ? LV_OPA_COVER : LV_OPA_30);
        bars_[i] = bar;
    }
}

void VisualizerWidget::render(uint32_t progress_per_mille, bool playing)
{
    if (progress_per_mille > 1000u) {
        progress_per_mille = 1000u;
    }

    const int active_count = static_cast<int>((progress_per_mille * kBarCount) / 1000u);
    for (int i = 0; i < kBarCount; ++i) {
        lv_obj_t* bar = bars_[i];
        if (!bar) {
            continue;
        }
        const uint8_t height = musicVisualizerBarHeight(static_cast<uint8_t>(i),
                                                        static_cast<uint8_t>(kBarCount),
                                                        progress_per_mille,
                                                        playing);
        lv_obj_set_size(bar, kSpectrumBarW, height);
        lv_obj_set_y(bar, kSpectrumY - height);
        setBg(bar, 0xffffff, i < active_count ? LV_OPA_COVER : LV_OPA_30);
    }
}

void VisualizerWidget::clear()
{
    for (lv_obj_t*& bar : bars_) {
        bar = nullptr;
    }
}
