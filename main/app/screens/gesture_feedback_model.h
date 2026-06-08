#pragma once

#include <stdint.h>

#include "app/screens/gesture_manager.h"

constexpr int kGestureCueScreenW = 640;
constexpr int kGestureCueScreenH = 172;
constexpr int kGestureCueW = 74;
constexpr int kGestureCueHandleX = 9;
constexpr int kGestureCueHandleW = 9;
constexpr int kGestureCueHandleH = 58;
constexpr int kGestureCueArrowSlotX = 29;
constexpr int kGestureCueArrowSlotW = 32;
constexpr int kGestureCueArrowSlotH = 32;
constexpr int kGestureCueArrowTravel = 8;

struct GestureCueLayout {
    bool visible = false;
    bool swipe_right = true;
    int rail_x = 0;
    int rail_w = kGestureCueW;
    int rail_h = kGestureCueScreenH;
    int handle_x = 0;
    int handle_y = 0;
    int handle_w = kGestureCueHandleW;
    int handle_h = kGestureCueHandleH;
    int arrow_slot_x = 0;
    int arrow_slot_y = 0;
    int arrow_slot_w = kGestureCueArrowSlotW;
    int arrow_slot_h = kGestureCueArrowSlotH;
    uint8_t rail_opa = 0;
    uint8_t handle_opa = 0;
    uint8_t slot_opa = 0;
    uint8_t slot_border_opa = 0;
    uint8_t arrow_opa = 0;
};

inline uint16_t clampGestureCueProgress(uint16_t per_mille)
{
    return per_mille > 1000 ? 1000 : per_mille;
}

inline uint8_t interpolateGestureCueOpacity(uint16_t per_mille, uint8_t base, uint8_t span)
{
    return static_cast<uint8_t>(
        base + (static_cast<uint32_t>(clampGestureCueProgress(per_mille)) * span / 1000u));
}

inline GestureCueLayout buildGestureCueLayout(const SwipeGestureProgress& progress)
{
    GestureCueLayout layout{};
    if (progress.direction == SwipeDirection::None || progress.per_mille == 0) {
        return layout;
    }

    const uint16_t per_mille = clampGestureCueProgress(progress.per_mille);
    const int arrow_travel =
        static_cast<int>(static_cast<uint32_t>(per_mille) * kGestureCueArrowTravel / 1000u);

    layout.visible = true;
    layout.swipe_right = progress.direction == SwipeDirection::Right;
    layout.rail_x = layout.swipe_right ? 0 : kGestureCueScreenW - kGestureCueW;
    layout.handle_y = (kGestureCueScreenH - kGestureCueHandleH) / 2;
    layout.arrow_slot_y = (kGestureCueScreenH - kGestureCueArrowSlotH) / 2;

    if (layout.swipe_right) {
        layout.handle_x = layout.rail_x + kGestureCueHandleX;
        layout.arrow_slot_x = layout.rail_x + kGestureCueArrowSlotX + arrow_travel;
    } else {
        layout.handle_x = layout.rail_x + kGestureCueW - kGestureCueHandleX - kGestureCueHandleW;
        layout.arrow_slot_x =
            layout.rail_x + kGestureCueW - kGestureCueArrowSlotX - kGestureCueArrowSlotW -
            arrow_travel;
    }

    layout.handle_opa = interpolateGestureCueOpacity(per_mille, 72, 116);
    layout.arrow_opa = interpolateGestureCueOpacity(per_mille, 96, 136);
    return layout;
}
