#include "../main/app/services/time_service.cpp"

#include <stdio.h>

static RtcDateTime_t rtc_time{};

RtcDateTime_t i2c_rtc_get(void)
{
    return rtc_time;
}

int main()
{
    rtc_time.hour = 12;
    rtc_time.minute = 34;
    rtc_time.second = 56;
    rtc_time.month = 5;
    rtc_time.day = 25;
    TimeService::get().poll();
    if (!TimeService::get().snapshot().rtc_ok) {
        printf("valid rtc time failed\n");
        return 1;
    }

    rtc_time.second = 60;
    TimeService::get().poll();
    if (TimeService::get().snapshot().rtc_ok) {
        printf("invalid rtc second failed\n");
        return 1;
    }

    return 0;
}
