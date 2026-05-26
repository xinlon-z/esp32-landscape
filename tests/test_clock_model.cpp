#include "app/features/clock/clock_model.cpp"

#include <gtest/gtest.h>

TEST(ClockModel, BuildDisplayState)
{
    ClockModel model;
    ClockDisplayState invalid = model.buildTime(false, 0, 0, 0, 0, 0, 0);
    if (strcmp(invalid.time, "--:--") != 0 || strcmp(invalid.weekday, "RTC") != 0 || strcmp(invalid.date, "--/--") != 0) {
        FAIL() << "invalid RTC display failed";
    }

    ClockDisplayState valid = model.buildTime(true, 9, 5, 2, 1, 5, 24);
    if (strcmp(valid.time, "09:05") != 0 || strcmp(valid.weekday, "Mon") != 0 || strcmp(valid.date, "05/24") != 0) {
        FAIL() << "valid RTC display failed: " << valid.time << " " << valid.weekday << " " << valid.date;
    }

    ClockDisplayState blink = model.buildTime(true, 9, 5, 3, 1, 5, 24);
    if (strcmp(blink.time, "09 05") != 0) {
        FAIL() << "colon blink failed: " << blink.time;
    }

    BatteryDisplayState first = model.buildBattery(50);
    if (first.percent != 50 || !first.update_label) {
        FAIL() << "battery first display failed: " << first.percent << " " << first.update_label;
    }

    BatteryDisplayState held = model.buildBattery(53);
    if (held.percent != 50 || held.update_label) {
        FAIL() << "battery hysteresis hold failed: " << held.percent << " " << held.update_label;
    }

    BatteryDisplayState changed = model.buildBattery(56);
    if (changed.percent != 56 || !changed.update_label) {
        FAIL() << "battery hysteresis update failed: " << changed.percent << " " << changed.update_label;
    }

    BatteryDisplayState unknown = model.buildBattery(-1);
    if (unknown.percent != -1 || !unknown.update_label) {
        FAIL() << "battery reset failed: " << unknown.percent << " " << unknown.update_label;
    }

    BatteryDisplayState after_reset = model.buildBattery(53);
    if (after_reset.percent != 53 || !after_reset.update_label) {
        FAIL() << "battery after reset failed: " << after_reset.percent << " " << after_reset.update_label;
    }
}
