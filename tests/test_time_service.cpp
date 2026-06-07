#include <gtest/gtest.h>
#include "app/services/time_service.cpp"
#include "app/core/event/event_bus.cpp"

static RtcDateTime_t rtc_time{};

RtcDateTime_t i2c_rtc_get(void)
{
    return rtc_time;
}

TEST(TimeService, PollAndSnapshot)
{
    rtc_time.hour = 12;
    rtc_time.minute = 34;
    rtc_time.second = 56;
    rtc_time.month = 5;
    rtc_time.day = 25;
    TimeService::get().poll();
    if (!TimeService::get().snapshot().rtc_ok) {
        FAIL() << "valid rtc time failed";
    }

    rtc_time.second = 60;
    TimeService::get().poll();
    if (TimeService::get().snapshot().rtc_ok) {
        FAIL() << "invalid rtc second failed";
    }
}
