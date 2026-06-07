#pragma once

#include <stdint.h>

// Monitors battery level, external power presence, and screen dimming.
// The power task runs on Core 1 (priority 2) and updates a packed atomic
// state word every 500 ms. The LVGL UI on Core 0 reads this state via
// getState() without any mutex or cross-core lock.
class PowerManager {
public:
    enum class IdleMode {
        Active,
        Dimmed,
        Sleeping,
    };

    struct State {
        bool external_power  = false;
        int  battery_percent = -1;  // -1 = unknown
        bool dimmed          = false;
        bool sleeping        = false;
    };

    // Initialises GPIO, performs an initial battery sample so that the first
    // LVGL render shows a real battery value, then starts the power task on
    // Core 1.
    static void init();

    // Called from the LVGL touch callback (Core 0) to reset the inactivity
    // dim timer. Thread-safe: increments a relaxed atomic counter.
    static void noteActivity();

    // Returns a consistent snapshot of all power state fields in O(1) with
    // a single relaxed atomic load. Safe to call from any context.
    static State getState();

private:
    static void task(void*);
    static int  sampleBattery(float* filtered);
    static bool checkExternalPower();
    static int  voltageToPercent(float v);
    static IdleMode computeIdleMode(bool external_power, uint32_t idle_ms);
};
