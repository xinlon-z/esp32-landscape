#include "seven_segment_widget.h"

namespace {
constexpr uint32_t kInk = 0x22282b;
constexpr uint32_t kDimInk = 0x363b3d;

constexpr bool kDigitMap[10][7] = {
    {true,  true,  true,  false, true,  true,  true },
    {false, false, true,  false, false, true,  false},
    {true,  false, true,  true,  true,  false, true },
    {true,  false, true,  true,  false, true,  true },
    {false, true,  true,  true,  false, true,  false},
    {true,  true,  false, true,  false, true,  true },
    {true,  true,  false, true,  true,  true,  true },
    {true,  false, true,  false, false, true,  false},
    {true,  true,  true,  true,  true,  true,  true },
    {true,  true,  true,  true,  false, true,  true },
};
} // namespace

void SevenSegmentWidget::bind(lv_obj_t* segments[7])
{
    for (int i = 0; i < 7; ++i) {
        segments_[i] = segments[i];
    }
}

void SevenSegmentWidget::render(char value, bool dimmed)
{
    for (int i = 0; i < 7; ++i) {
        bool active = false;
        if (value >= '0' && value <= '9') {
            active = kDigitMap[value - '0'][i];
        } else if (value == '-') {
            active = i == 3;
        }
        if (segments_[i]) {
            lv_obj_set_style_bg_color(segments_[i], lv_color_hex(dimmed ? kDimInk : kInk), 0);
            lv_obj_set_style_bg_opa(segments_[i], active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        }
    }
}
