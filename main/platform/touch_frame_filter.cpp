#include "platform/touch_frame_filter.h"

TouchFrameResult TouchFrameFilter::process(const uint8_t frame[8])
{
    const uint8_t point_count = frame[1];
    const bool valid_point_count = point_count > 0 && point_count <= kMaxTouchPoints;
    if (!valid_point_count) {
        reset();
        return TouchFrameResult{};
    }

    if (valid_frame_count_ < kValidFramesBeforePress) {
        ++valid_frame_count_;
    }

    TouchFrameResult result{};
    result.x = (static_cast<uint16_t>(frame[2] & 0x0f) << 8) | frame[3];
    result.y = (static_cast<uint16_t>(frame[4] & 0x0f) << 8) | frame[5];
    result.state = valid_frame_count_ >= kValidFramesBeforePress
                       ? TouchFrameState::Pressed : TouchFrameState::Candidate;
    return result;
}

void TouchFrameFilter::reset()
{
    valid_frame_count_ = 0;
}

