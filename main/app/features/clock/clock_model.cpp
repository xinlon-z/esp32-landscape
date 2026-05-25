#include "clock_model.h"

#include <stdio.h>

namespace {
const char* weekdayName(unsigned week)
{
    static const char* kNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return kNames[week % 7];
}

int absInt(int value)
{
    return value < 0 ? -value : value;
}
} // namespace

ClockDisplayState ClockModel::buildTime(bool rtc_ok, unsigned hour, unsigned minute, unsigned second,
                                        unsigned week, unsigned month, unsigned day)
{
    ClockDisplayState state{};
    if (!rtc_ok) {
        return state;
    }

    state.time[0] = static_cast<char>('0' + (hour / 10));
    state.time[1] = static_cast<char>('0' + (hour % 10));
    state.time[2] = (second % 2 == 0) ? ':' : ' ';
    state.time[3] = static_cast<char>('0' + (minute / 10));
    state.time[4] = static_cast<char>('0' + (minute % 10));
    state.time[5] = '\0';
    snprintf(state.weekday, sizeof(state.weekday), "%s", weekdayName(week));
    snprintf(state.date, sizeof(state.date), "%02u/%02u", month, day);
    return state;
}

BatteryDisplayState ClockModel::buildBattery(int percent)
{
    BatteryDisplayState state{};
    state.percent = percent;
    if (percent < 0) {
        battery_disp_pct_ = -1;
        state.update_label = true;
        return state;
    }
    state.update_label = battery_disp_pct_ < 0 || absInt(percent - battery_disp_pct_) >= 5;
    if (state.update_label) {
        battery_disp_pct_ = percent;
    }
    return state;
}

void ClockModel::resetBattery()
{
    battery_disp_pct_ = -1;
}
