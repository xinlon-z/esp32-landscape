#include "clock_presenter.h"

#include "app/core/event/event_bus.h"
#include "app/services/clock_background_service.h"
#include "app/services/network_service.h"
#include "app/services/power_service.h"
#include "app/services/time_service.h"

ClockPresenter::ClockPresenter(ClockView& view) : view_(view) {}

void ClockPresenter::start()
{
    running_ = true;
    model_.resetBattery();
    last_background_revision_ = 0;

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

    bool time_changed = false;
    bool power_changed = false;
    bool network_changed = false;
    AppEvent event{};
    while (EventBus::get().poll(&event)) {
        if (event.type == AppEventType::ClockTimeChanged &&
            event.payload.clock_time.revision != last_time_revision_) {
            last_time_revision_ = event.payload.clock_time.revision;
            time_changed = true;
        } else if (event.type == AppEventType::PowerStateChanged &&
                   event.payload.power_state.revision != last_power_revision_) {
            last_power_revision_ = event.payload.power_state.revision;
            power_changed = true;
        } else if (event.type == AppEventType::NetworkStateChanged &&
                   event.payload.network_state.revision != last_network_revision_) {
            last_network_revision_ = event.payload.network_state.revision;
            network_changed = true;
        }
    }

    if (power_changed) {
        renderPowerSnapshot();
    }
    if (time_changed) {
        renderTimeSnapshot();
    } else if (power_changed) {
        renderCachedTime();
    }
    if (network_changed) {
        renderNetworkSnapshot();
    } else if (power_changed) {
        renderCachedNetwork();
    }
    requestBackgroundIfNeeded();
    renderBackgroundSnapshot();
}

void ClockPresenter::renderAll()
{
    renderPowerSnapshot();
    renderTimeSnapshot();
    renderNetworkSnapshot();
    requestBackgroundIfNeeded();
    renderBackgroundSnapshot();
}

void ClockPresenter::requestBackgroundIfNeeded()
{
    if (!network_state_.wifi_connected) {
        return;
    }

    ClockBackgroundService::get().requestRefreshIfDue();
}

void ClockPresenter::renderBackgroundSnapshot()
{
    const ClockBackgroundState state = ClockBackgroundService::get().snapshot();
    if (state.revision == last_background_revision_) {
        return;
    }

    last_background_revision_ = state.revision;
    if (!state.has_pixels) {
        return;
    }

    lv_color_t* dst = view_.backgroundPixels();
    if (!dst) {
        return;
    }

    lv_img_dsc_t image{};
    uint32_t copied_revision = 0;
    if (ClockBackgroundService::get().copyPixels(dst,
                                                 ClockBackgroundService::kPixelCount,
                                                 &image,
                                                 &copied_revision)) {
        last_background_revision_ = copied_revision;
        view_.setPalette(state.palette);
        view_.showBackground(image);
        renderCachedTime();
        renderPowerSnapshot();
        renderCachedNetwork();
    }
}

void ClockPresenter::renderTimeSnapshot()
{
    const ClockSnapshot clock = TimeService::get().snapshot();
    time_state_ = model_.buildTime(clock.rtc_ok, clock.hour, clock.minute, clock.second,
                                   clock.week, clock.month, clock.day);
    renderCachedTime();
}

void ClockPresenter::renderPowerSnapshot()
{
    const PowerSnapshot power = PowerService::get().snapshot();
    dimmed_ = power.dimmed;
    external_power_ = power.external_power;

    BatteryDisplayState battery = model_.buildBattery(power.battery_percent);
    view_.renderBattery(battery, dimmed_);
}

void ClockPresenter::renderNetworkSnapshot()
{
    const NetworkSnapshot network = NetworkService::get().snapshot();
    network_state_.wifi_connected = network.wifi_connected;
    network_state_.sync_in_progress = network.sync_in_progress;
    network_state_.ntp_synced = network.ntp_synced;
    renderCachedNetwork();
}

void ClockPresenter::renderCachedTime()
{
    view_.renderTime(time_state_, dimmed_);
}

void ClockPresenter::renderCachedNetwork()
{
    network_state_.external_power = external_power_;
    view_.renderNetwork(network_state_, dimmed_);
}
