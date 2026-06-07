#include "time_service.h"

#include "../core/event/event_bus.h"
#include "i2c_equipment.h"

TimeService& TimeService::get()
{
    static TimeService service;
    return service;
}

ClockSnapshot TimeService::snapshot()
{
    return snapshot_;
}

void TimeService::poll()
{
    const RtcDateTime_t now = i2c_rtc_get();
    ClockSnapshot next{};
    next.rtc_ok = !(now.hour > 23 || now.minute > 59 || now.second > 59 ||
                    now.month == 0 || now.month > 12 ||
                    now.day == 0 || now.day > 31);
    next.hour = now.hour;
    next.minute = now.minute;
    next.second = now.second;
    next.week = now.week;
    next.month = now.month;
    next.day = now.day;
    next.revision = snapshot_.revision + 1;
    snapshot_ = next;

    AppEvent event{};
    event.type = AppEventType::ClockTimeChanged;
    event.payload.clock_time.revision = snapshot_.revision;
    EventBus::get().publish(event);
}
