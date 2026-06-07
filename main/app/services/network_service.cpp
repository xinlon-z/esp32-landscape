#include "network_service.h"

#include "../core/event/event_bus.h"
#include "platform/clock_net.h"

NetworkService& NetworkService::get()
{
    static NetworkService service;
    return service;
}

NetworkSnapshot NetworkService::snapshot()
{
    return snapshot_;
}

void NetworkService::poll()
{
    const ClockNet::Status status = ClockNet::getStatus();
    if (status.wifi_connected == snapshot_.wifi_connected &&
        status.sync_in_progress == snapshot_.sync_in_progress &&
        status.ntp_synced == snapshot_.ntp_synced) {
        return;
    }

    snapshot_.wifi_connected = status.wifi_connected;
    snapshot_.sync_in_progress = status.sync_in_progress;
    snapshot_.ntp_synced = status.ntp_synced;
    ++snapshot_.revision;

    AppEvent event{};
    event.type = AppEventType::NetworkStateChanged;
    event.payload.network_state.revision = snapshot_.revision;
    EventBus::get().publish(event);
}
