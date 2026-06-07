#include "power_service.h"

#include "../core/event/event_bus.h"
#include "platform/power_mgr.h"

PowerService& PowerService::get()
{
    static PowerService service;
    return service;
}

PowerSnapshot PowerService::snapshot()
{
    return snapshot_;
}

void PowerService::poll()
{
    const PowerManager::State state = PowerManager::getState();
    if (state.external_power == snapshot_.external_power &&
        state.battery_percent == snapshot_.battery_percent &&
        state.dimmed == snapshot_.dimmed &&
        state.sleeping == snapshot_.sleeping) {
        return;
    }

    snapshot_.external_power = state.external_power;
    snapshot_.battery_percent = state.battery_percent;
    snapshot_.dimmed = state.dimmed;
    snapshot_.sleeping = state.sleeping;
    ++snapshot_.revision;

    AppEvent event{};
    event.type = AppEventType::PowerStateChanged;
    event.payload.power_state.revision = snapshot_.revision;
    EventBus::get().publish(event);
}
