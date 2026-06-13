#pragma once

#include <stdint.h>

enum class ButtonEvent : uint8_t {
    None,
    ShortPress,
    LongPress,
};

class DebouncedButton {
public:
    DebouncedButton(uint8_t debounce_ticks, uint16_t long_press_ticks)
        : debounce_ticks_(debounce_ticks),
          long_press_ticks_(long_press_ticks)
    {
    }

    ButtonEvent update(bool pressed)
    {
        if (pressed != raw_pressed_) {
            raw_pressed_ = pressed;
            raw_ticks_ = 1;
        } else if (raw_ticks_ < debounce_ticks_) {
            ++raw_ticks_;
        }

        if (raw_ticks_ >= debounce_ticks_ && raw_pressed_ != stable_pressed_) {
            stable_pressed_ = raw_pressed_;
            if (stable_pressed_) {
                stable_press_ticks_ = 0;
                long_sent_ = false;
            } else {
                const bool was_short = stable_press_ticks_ < long_press_ticks_ && !long_sent_;
                stable_press_ticks_ = 0;
                long_sent_ = false;
                return was_short ? ButtonEvent::ShortPress : ButtonEvent::None;
            }
        }

        if (stable_pressed_) {
            if (stable_press_ticks_ < UINT16_MAX) {
                ++stable_press_ticks_;
            }
            if (!long_sent_ && stable_press_ticks_ >= long_press_ticks_) {
                long_sent_ = true;
                return ButtonEvent::LongPress;
            }
        }

        return ButtonEvent::None;
    }

    void reset()
    {
        raw_pressed_ = false;
        stable_pressed_ = false;
        raw_ticks_ = 0;
        stable_press_ticks_ = 0;
        long_sent_ = false;
    }

private:
    uint8_t debounce_ticks_;
    uint16_t long_press_ticks_;
    bool raw_pressed_ = false;
    bool stable_pressed_ = false;
    uint8_t raw_ticks_ = 0;
    uint16_t stable_press_ticks_ = 0;
    bool long_sent_ = false;
};
