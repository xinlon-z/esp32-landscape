#pragma once

class PowerManager {
public:
    struct State {
        bool external_power = false;
        int battery_percent = -1;
        bool dimmed = false;
        bool screen_off = false;
        bool sleeping = false;
    };

    static State getState()
    {
        return State{};
    }

    static void init() {}
    static void noteActivity() {}
    static void requestWake() {}
    static void requestManualSleep() {}
    static void requestPowerOff() {}
    static bool isSleeping() { return false; }
    static bool isDisplayOff() { return false; }
};
