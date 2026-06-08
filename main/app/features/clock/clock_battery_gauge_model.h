#pragma once

#include <stdint.h>

constexpr uint32_t kClockBatteryInk = 0x22282b;
constexpr uint32_t kClockBatteryDimInk = 0x363b3d;
constexpr uint32_t kClockBatteryLow = 0xa24535;

struct ClockBatteryGaugeState {
    int value = 0;
    uint32_t fill_color = kClockBatteryInk;
    uint32_t shell_color = kClockBatteryInk;
};

inline int clampClockBatteryPercent(int percent)
{
    if (percent < 0) {
        return 0;
    }
    if (percent > 100) {
        return 100;
    }
    return percent;
}

inline ClockBatteryGaugeState buildClockBatteryGaugeState(int percent, bool dimmed)
{
    ClockBatteryGaugeState state{};
    state.value = clampClockBatteryPercent(percent);
    state.shell_color = dimmed ? kClockBatteryDimInk : kClockBatteryInk;
    state.fill_color = (state.value > 0 && state.value <= 15)
        ? kClockBatteryLow
        : state.shell_color;
    return state;
}
