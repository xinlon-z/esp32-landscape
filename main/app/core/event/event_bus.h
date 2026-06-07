#pragma once

#include "app_events.h"
#include "event_queue.h"

#include <mutex>
#include <stdint.h>

class EventBus {
public:
    static EventBus& get();

    bool publish(const AppEvent& event);
    bool poll(AppEvent* event);
    uint32_t overflowCount() const;

    void resetForTest();

private:
    EventBus() = default;

    static constexpr size_t kCapacity = 16;
    mutable std::mutex mutex_;
    EventQueue<AppEvent, kCapacity> queue_;
};
