#include "clock_presenter.h"

#include "app/core/event/event_bus.h"
#include "app/services/network_service.h"
#include "app/services/power_service.h"
#include "app/services/time_service.h"

ClockPresenter::ClockPresenter(ClockView& view) : view_(view) {}

void ClockPresenter::start()
{
    running_ = true;
    model_.resetBattery();

    TimeService::get().poll();
    PowerService::get().poll();
    NetworkService::get().poll();

    AppEvent event{};
    while (EventBus::get().poll(&event)) {
        if (event.type == AppEventType::ClockTimeChanged) {
            last_time_revision_ = event.payload.clock_time.revision;
        } else if (event.type == AppEventType::PowerStateChanged) {
            last_power_revision_ = event.payload.power_state.revision;
        } else if (event.type == AppEventType::NetworkStateChanged) {
            last_network_revision_ = event.payload.network_state.revision;
        }
    }

    renderAll();
}

void ClockPresenter::stop()
{
    running_ = false;
}

void ClockPresenter::tick()
{
    if (!running_) {
        return;
    }

    TimeService::get().poll();
    PowerService::get().poll();
    NetworkService::get().poll();

    bool dirty = false;
    AppEvent event{};
    while (EventBus::get().poll(&event)) {
        if (event.type == AppEventType::ClockTimeChanged &&
            event.payload.clock_time.revision != last_time_revision_) {
            last_time_revision_ = event.payload.clock_time.revision;
            dirty = true;
        } else if (event.type == AppEventType::PowerStateChanged &&
                   event.payload.power_state.revision != last_power_revision_) {
            last_power_revision_ = event.payload.power_state.revision;
            dirty = true;
        } else if (event.type == AppEventType::NetworkStateChanged &&
                   event.payload.network_state.revision != last_network_revision_) {
            last_network_revision_ = event.payload.network_state.revision;
            dirty = true;
        }
    }

    if (dirty) {
        renderAll();
    }
}

void ClockPresenter::renderAll()
{
    const ClockSnapshot clock = TimeService::get().snapshot();
    const PowerSnapshot power = PowerService::get().snapshot();
    const NetworkSnapshot network = NetworkService::get().snapshot();

    dimmed_ = power.dimmed;
    external_power_ = power.external_power;

    const ClockDisplayState time = model_.buildTime(clock.rtc_ok, clock.hour, clock.minute, clock.second,
                                                    clock.week, clock.month, clock.day);
    BatteryDisplayState battery = model_.buildBattery(power.battery_percent);
    NetworkDisplayState net{};
    net.wifi_connected = network.wifi_connected;
    net.sync_in_progress = network.sync_in_progress;
    net.ntp_synced = network.ntp_synced;
    net.external_power = external_power_;

    view_.renderTime(time, dimmed_);
    view_.renderBattery(battery, dimmed_);
    view_.renderNetwork(net, dimmed_);
}
