#pragma once

class PowerManager {
public:
    struct State {
        bool external_power = false;
        int battery_percent = -1;
        bool dimmed = false;
        bool sleeping = false;
    };

    static State getState() { return State{}; }
};
