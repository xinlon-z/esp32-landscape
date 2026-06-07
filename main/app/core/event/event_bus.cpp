#include "event_bus.h"

EventBus& EventBus::get()
{
    static EventBus bus;
    return bus;
}

bool EventBus::publish(const AppEvent& event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.publish(event);
}

bool EventBus::poll(AppEvent* event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.poll(event);
}

uint32_t EventBus::overflowCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.overflowCount();
}

void EventBus::resetForTest()
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_ = EventQueue<AppEvent, kCapacity>{};
}
