#pragma once

#include <stdint.h>

enum class TouchFrameState {
    Released,
    Candidate,
    Pressed,
};

struct TouchFrameResult {
    TouchFrameState state = TouchFrameState::Released;
    uint16_t x = 0;
    uint16_t y = 0;
};

class TouchFrameFilter {
public:
    TouchFrameResult process(const uint8_t frame[8]);
    void reset();

private:
    static constexpr uint8_t kMaxTouchPoints = 1;
    static constexpr uint8_t kValidFramesBeforePress = 2;

    uint8_t valid_frame_count_ = 0;
};

