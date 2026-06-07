#pragma once

#include <stdint.h>

struct ClockSnapshot {
    bool rtc_ok = false;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t week = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint32_t revision = 0;
};

class TimeService {
public:
    static TimeService& get();
    ClockSnapshot snapshot();
    void poll();
};
