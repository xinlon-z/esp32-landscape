#pragma once

#include "clock_model.h"
#include "lvgl.h"

class ClockView {
public:
    void create();
    void destroy();
    void renderTime(const ClockDisplayState& state, bool dimmed);
    void renderBattery(const BatteryDisplayState& state, bool dimmed);
    void renderNetwork(const NetworkDisplayState& state, bool dimmed);

private:
    lv_obj_t* digit_segs_[4][7] = {};
    lv_obj_t* colon_top_ = nullptr;
    lv_obj_t* colon_bottom_ = nullptr;
    lv_obj_t* weekday_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    lv_obj_t* wifi_icon_ = nullptr;
    lv_obj_t* sync_icon_ = nullptr;
    lv_obj_t* power_icon_ = nullptr;
    lv_obj_t* battery_shell_ = nullptr;
    lv_obj_t* battery_fill_ = nullptr;
    lv_obj_t* battery_cap_ = nullptr;

    void renderDigit(int slot, char c, bool dimmed);
};
