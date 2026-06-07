#include "app/core/event/event_bus.cpp"

#include <gtest/gtest.h>

TEST(EventBus, PublishPollReset)
{
    EventBus::get().resetForTest();

    AppEvent event{};
    event.type = AppEventType::PowerStateChanged;
    event.payload.power_state.revision = 42;

    if (!EventBus::get().publish(event)) {
        FAIL() << "publish failed";
    }

    AppEvent out{};
    if (!EventBus::get().poll(&out)) {
        FAIL() << "poll failed";
    }
    if (out.type != AppEventType::PowerStateChanged || out.payload.power_state.revision != 42) {
        FAIL() << "event mismatch";
    }
    if (EventBus::get().poll(&out)) {
        FAIL() << "poll should return false on empty queue";
    }

    for (int i = 0; i < 16; ++i) {
        if (!EventBus::get().publish(event)) {
            FAIL() << "publish before reset failed";
        }
    }
    if (EventBus::get().publish(event)) {
        FAIL() << "publish should fail when queue is full";
    }
    if (EventBus::get().overflowCount() != 1) {
        FAIL() << "overflow count mismatch before reset";
    }

    EventBus::get().resetForTest();

    if (EventBus::get().poll(&out)) {
        FAIL() << "poll should return false after reset";
    }
    if (EventBus::get().overflowCount() != 0) {
        FAIL() << "overflow count should reset";
    }
}
