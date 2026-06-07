#pragma once

#include "../core/event/app_events.h"

#include <stdint.h>

enum class SwipeDirection : uint8_t {
    None,
    Left,
    Right,
};

struct TouchPoint {
    int16_t x;
    int16_t y;
};

struct SwipeGestureStats {
    int dx;
    int dy;
    uint32_t duration_ms;
    uint16_t samples;
    bool edge_start;
};

struct SwipeGestureProgress {
    SwipeDirection direction = SwipeDirection::None;
    uint16_t per_mille = 0;
    int dx = 0;
    bool edge_start = false;
};

class SwipeGestureDetector {
public:
    void press(TouchPoint point, uint32_t tick_ms = 0);
    void move(TouchPoint point);
    bool progress(SwipeGestureProgress* progress) const;
    SwipeDirection release(TouchPoint point, uint32_t tick_ms = 0,
                           SwipeGestureStats* stats = nullptr);
    void reset();

private:
    bool pressed_ = false;
    TouchPoint start_{0, 0};
    TouchPoint latest_{0, 0};
    int16_t min_y_ = 0;
    int16_t max_y_ = 0;
    uint32_t start_tick_ms_ = 0;
    uint16_t samples_ = 0;
};

SwipeDirection detectSwipe(TouchPoint start, TouchPoint end);
ScreenId nextScreenForSwipe(ScreenId current, SwipeDirection direction);
void publishFeatureAction(ScreenId screen, uint8_t action_id);
