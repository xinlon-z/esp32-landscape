#include "../main/app/features/clock/clock_presenter.cpp"
#include "../main/app/features/clock/clock_model.cpp"
#include "../main/app/core/event/event_bus.cpp"

#include <stdio.h>
#include <string.h>

namespace {
ClockSnapshot g_time_after_poll{true, 9, 5, 2, 1, 5, 24, 1};
PowerSnapshot g_power_after_poll{true, 50, true, false, 1};
NetworkSnapshot g_network_after_poll{true, true, false, 1};

int g_time_polls = 0;
int g_power_polls = 0;
int g_network_polls = 0;

ClockDisplayState g_rendered_time{};
BatteryDisplayState g_rendered_battery{};
NetworkDisplayState g_rendered_network{};
bool g_time_dimmed = false;
bool g_battery_dimmed = false;
bool g_network_dimmed = false;
} // namespace

TimeService& TimeService::get()
{
    static TimeService service;
    return service;
}

ClockSnapshot TimeService::snapshot()
{
    return g_time_polls > 0 ? g_time_after_poll : ClockSnapshot{};
}

void TimeService::poll()
{
    ++g_time_polls;
}

PowerService& PowerService::get()
{
    static PowerService service;
    return service;
}

PowerSnapshot PowerService::snapshot()
{
    return g_power_polls > 0 ? g_power_after_poll : PowerSnapshot{};
}

void PowerService::poll()
{
    ++g_power_polls;
}

NetworkService& NetworkService::get()
{
    static NetworkService service;
    return service;
}

NetworkSnapshot NetworkService::snapshot()
{
    return g_network_polls > 0 ? g_network_after_poll : NetworkSnapshot{};
}

void NetworkService::poll()
{
    ++g_network_polls;
}

void ClockView::create() {}
void ClockView::destroy() {}

void ClockView::renderTime(const ClockDisplayState& state, bool dimmed)
{
    g_rendered_time = state;
    g_time_dimmed = dimmed;
}

void ClockView::renderBattery(const BatteryDisplayState& state, bool dimmed)
{
    g_rendered_battery = state;
    g_battery_dimmed = dimmed;
}

void ClockView::renderNetwork(const NetworkDisplayState& state, bool dimmed)
{
    g_rendered_network = state;
    g_network_dimmed = dimmed;
}

int main()
{
    EventBus::get().resetForTest();

    ClockView view;
    ClockPresenter presenter(view);
    presenter.start();

    if (g_time_polls != 1 || g_power_polls != 1 || g_network_polls != 1) {
        printf("start did not poll services first: %d %d %d\n", g_time_polls, g_power_polls, g_network_polls);
        return 1;
    }

    if (strcmp(g_rendered_time.time, "09:05") != 0 ||
        strcmp(g_rendered_time.weekday, "Mon") != 0 ||
        strcmp(g_rendered_time.date, "05/24") != 0) {
        printf("start rendered stale time: %s %s %s\n",
               g_rendered_time.time, g_rendered_time.weekday, g_rendered_time.date);
        return 1;
    }

    if (g_rendered_battery.percent != 50 || !g_rendered_battery.update_label) {
        printf("start rendered stale battery: %d %d\n",
               g_rendered_battery.percent, g_rendered_battery.update_label);
        return 1;
    }

    if (!g_rendered_network.wifi_connected || !g_rendered_network.sync_in_progress ||
        g_rendered_network.ntp_synced || !g_rendered_network.external_power) {
        printf("start rendered stale network: %d %d %d %d\n",
               g_rendered_network.wifi_connected,
               g_rendered_network.sync_in_progress,
               g_rendered_network.ntp_synced,
               g_rendered_network.external_power);
        return 1;
    }

    if (!g_time_dimmed || !g_battery_dimmed || !g_network_dimmed) {
        printf("start did not render dimmed state: %d %d %d\n",
               g_time_dimmed, g_battery_dimmed, g_network_dimmed);
        return 1;
    }

    return 0;
}
