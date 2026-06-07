#include "gesture_manager.h"

#include "../core/event/event_bus.h"

namespace {
constexpr int kMinSwipeX = 120;
constexpr int kMaxSwipeY = 54;
constexpr int kScreenW = 640;
constexpr int kEdgeStartPx = 96;
constexpr uint32_t kEdgeMaxDurationMs = 520;
constexpr uint32_t kCenterMaxDurationMs = 360;
constexpr uint32_t kEdgeMinSpeedPxPerSec = 650;
constexpr uint32_t kCenterMinSpeedPxPerSec = 1100;

int absInt(int v)
{
    return v < 0 ? -v : v;
}

bool edgeStartForDirection(TouchPoint start, int dx)
{
    if (dx < 0) {
        return start.x >= kScreenW - kEdgeStartPx;
    }
    if (dx > 0) {
        return start.x <= kEdgeStartPx;
    }
    return false;
}

uint16_t swipeProgressPerMille(int dx)
{
    const int progress = absInt(dx) * 1000 / kMinSwipeX;
    return progress >= 1000 ? 1000 : static_cast<uint16_t>(progress);
}
} // namespace

void SwipeGestureDetector::press(TouchPoint point, uint32_t tick_ms)
{
    pressed_ = true;
    start_ = point;
    latest_ = point;
    min_y_ = point.y;
    max_y_ = point.y;
    start_tick_ms_ = tick_ms;
    samples_ = 1;
}

void SwipeGestureDetector::move(TouchPoint point)
{
    if (!pressed_) {
        return;
    }
    latest_ = point;
    if (point.y < min_y_) {
        min_y_ = point.y;
    }
    if (point.y > max_y_) {
        max_y_ = point.y;
    }
    if (samples_ < UINT16_MAX) {
        ++samples_;
    }
}

bool SwipeGestureDetector::progress(SwipeGestureProgress* progress) const
{
    if (!pressed_ || !progress) {
        return false;
    }

    const int dx = latest_.x - start_.x;
    const SwipeDirection direction = dx < 0 ? SwipeDirection::Left :
        (dx > 0 ? SwipeDirection::Right : SwipeDirection::None);

    *progress = SwipeGestureProgress{
        direction,
        swipeProgressPerMille(dx),
        dx,
        edgeStartForDirection(start_, dx),
    };
    return true;
}

SwipeDirection SwipeGestureDetector::release(TouchPoint point, uint32_t tick_ms,
                                             SwipeGestureStats* stats)
{
    if (!pressed_) {
        return SwipeDirection::None;
    }

    move(point);
    pressed_ = false;
    const uint32_t duration = tick_ms - start_tick_ms_;
    const int dx = point.x - start_.x;
    const int dy = point.y - start_.y;
    const int vertical_travel = max_y_ - min_y_;
    const bool edge_start = edgeStartForDirection(start_, dx);

    if (stats) {
        *stats = SwipeGestureStats{dx, dy, duration, samples_, edge_start};
    }

    if (absInt(dx) < kMinSwipeX ||
        absInt(dy) > kMaxSwipeY ||
        vertical_travel > kMaxSwipeY) {
        return SwipeDirection::None;
    }

    if (duration > 0) {
        const uint32_t max_duration = edge_start ? kEdgeMaxDurationMs : kCenterMaxDurationMs;
        const uint32_t min_speed = edge_start ? kEdgeMinSpeedPxPerSec : kCenterMinSpeedPxPerSec;
        const uint32_t speed = static_cast<uint32_t>(absInt(dx)) * 1000U / duration;
        if (duration > max_duration || speed < min_speed) {
            return SwipeDirection::None;
        }
    }

    return dx < 0 ? SwipeDirection::Left : SwipeDirection::Right;
}

void SwipeGestureDetector::reset()
{
    pressed_ = false;
    start_ = {0, 0};
    latest_ = {0, 0};
    min_y_ = 0;
    max_y_ = 0;
    start_tick_ms_ = 0;
    samples_ = 0;
}

SwipeDirection detectSwipe(TouchPoint start, TouchPoint end)
{
    const int dx = end.x - start.x;
    const int dy = end.y - start.y;
    if (absInt(dx) < kMinSwipeX || absInt(dy) > kMaxSwipeY) {
        return SwipeDirection::None;
    }
    return dx < 0 ? SwipeDirection::Left : SwipeDirection::Right;
}

ScreenId nextScreenForSwipe(ScreenId current, SwipeDirection direction)
{
    if (current == ScreenId::Clock && direction == SwipeDirection::Left) {
        return ScreenId::Music;
    }
    if (current == ScreenId::Music && direction == SwipeDirection::Right) {
        return ScreenId::Clock;
    }
    return current;
}

void publishFeatureAction(ScreenId screen, uint8_t action_id)
{
    AppEvent event{};
    event.type = AppEventType::FeatureAction;
    event.payload.feature_action.screen_id = static_cast<uint8_t>(screen);
    event.payload.feature_action.action_id = action_id;
    EventBus::get().publish(event);
}
