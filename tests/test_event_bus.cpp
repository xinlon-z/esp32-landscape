#include "../main/app/core/event/event_bus.cpp"

#include <stdio.h>

int main()
{
    EventBus::get().resetForTest();

    AppEvent event{};
    event.type = AppEventType::PowerStateChanged;
    event.payload.power_state.revision = 42;

    if (!EventBus::get().publish(event)) {
        printf("publish failed\n");
        return 1;
    }

    AppEvent out{};
    if (!EventBus::get().poll(&out)) {
        printf("poll failed\n");
        return 1;
    }
    if (out.type != AppEventType::PowerStateChanged || out.payload.power_state.revision != 42) {
        printf("event mismatch\n");
        return 1;
    }
    if (EventBus::get().poll(&out)) {
        printf("poll should return false on empty queue\n");
        return 1;
    }

    for (int i = 0; i < 16; ++i) {
        if (!EventBus::get().publish(event)) {
            printf("publish before reset failed\n");
            return 1;
        }
    }
    if (EventBus::get().publish(event)) {
        printf("publish should fail when queue is full\n");
        return 1;
    }
    if (EventBus::get().overflowCount() != 1) {
        printf("overflow count mismatch before reset\n");
        return 1;
    }

    EventBus::get().resetForTest();

    if (EventBus::get().poll(&out)) {
        printf("poll should return false after reset\n");
        return 1;
    }
    if (EventBus::get().overflowCount() != 0) {
        printf("overflow count should reset\n");
        return 1;
    }
    return 0;
}
