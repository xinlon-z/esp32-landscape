#pragma once

struct ClockDisplayState {
    char time[6] = "--:--";
    char weekday[4] = "RTC";
    char date[6] = "--/--";
};

struct BatteryDisplayState {
    int percent = -1;
    bool update_label = true;
};

struct NetworkDisplayState {
    bool wifi_connected = false;
    bool sync_in_progress = false;
    bool ntp_synced = false;
    bool external_power = false;
};

class ClockModel {
public:
    ClockDisplayState buildTime(bool rtc_ok, unsigned hour, unsigned minute, unsigned second,
                                unsigned week, unsigned month, unsigned day);
    BatteryDisplayState buildBattery(int percent);
    void resetBattery();

private:
    int battery_disp_pct_ = -1;
};
